# Lumen scene coverage — spike

Whether the runtime-built world and props can be made visible to Lumen's **surface cache**, so
`$envmap` reflections (roadmap 7.5) and off-screen GI bounce come from the Lumen scene rather
than from screen traces alone.

Per-task status belongs in `roadmap.md`; this file is the spike's working record — the engine
facts, the measurements, what is implemented, and what is known not to work. Companion:
`rendering-perf.md` (the render path this changes).

All measurements are `sp_tutorial_1`, dev GPU **RTX 5070 Ti**, D3D12/SM6. The coverage vantage
is the alley at `(-35.56, -19032.22, -340.69)` yaw `85.91` — reachable with
`elysium_player_teleport` or `elysium.campos`-style coordinates.

---

## 1. The problem

Lumen's surface cache is empty for every runtime-built mesh. Confirmed in a live frame, camera
fixed, `r.Lumen.Visualize`:

| Mode | Result |
|---|---|
| `6` Geometry Normals — traced against the **ray-tracing scene** | the full scene draws |
| `3` Lumen Scene — the **surface cache** | pure black |
| `5` Surface Cache Coverage | entirely *"Yellow — culled Surface Cache"* |

Geometry is in the BVH and rays hit it; the cache behind those hits holds nothing, so every hit
resolves to black. `$envmap` surfaces can therefore only reflect what is already on screen.

The committed profiling baseline in `rendering-perf.md` reads the same way from the other side:
Lumen GI 0.01 ms and Lumen reflections 0.06 ms are "nothing in the Lumen scene" numbers, not
"fast card" numbers.

## 2. Engine facts (verified in UE 5.8 source)

### Three requirements for surface cache entry

A primitive enters the surface cache only if **all three** hold:

1. **Card representation.** `FLumenSceneData::AddMeshCards` reads
   `Proxy->GetMeshCardRepresentation()`; null takes the `else` branch and sets
   `bValidMeshCards = false` (`LumenMeshCards.cpp:919-955`). Only `FStaticMeshSceneProxy`,
   `FSkeletalMeshSceneProxy`, the Nanite proxies, and `FBaseDynamicMeshSceneProxy` override it.
2. **Static mesh batches.** `LumenScene::AddCardCaptureDraws` gathers work from
   `PrimitiveSceneInfo->StaticMeshRelevances` for the `EMeshPass::LumenCardCapture` pass
   (`LumenSceneCardCapture.cpp:760-790`). A proxy implementing only `GetDynamicMeshElements`
   contributes no draws, so its cards would capture nothing even with requirement 1 met.
3. **Large enough.** `TrackPrimitiveInstanceForLumenScene` (`LumenScene.cpp:876-885`) requires
   the largest bounds face area to exceed `LumenMeshCards::GetCardMinSurfaceArea`
   (`LumenMeshCards.cpp:128-132`), driven by **`r.LumenScene.SurfaceCache.MeshCardsMinSize`**.
   Below it the primitive is culled outright. Epic's docs surface this as *"Small meshes appear
   black in mirror reflections because Lumen culls small objects from Lumen Scene for
   performance"* — the post-process knob is **Lumen Scene Detail**.

Where our geometry stands:

| Component | Cards | Static batches | Bounds |
|---|---|---|---|
| `UProceduralMeshComponent` (world, sky, collision) | no | no — `GetDynamicMeshElements` only | one mesh per map |
| runtime `UStaticMesh` (props, world chunks) | no by default | **yes** | per mesh |
| `UDynamicMeshComponent` (not used here) | yes, from bounds | yes | per component |

**`UProceduralMeshComponent` can never work**, failing 1 and 2. That is why the world had to move
to `UStaticMeshComponent`.

### The injection point

`FStaticMeshLODResources::CardRepresentationData` is a public raw pointer
(`StaticMeshResources.h:428`) and `~FStaticMeshLODResources` deletes it (`StaticMesh.cpp:1468`),
so a runtime-built mesh can be handed a `new FCardRepresentationData()` and the LOD owns it. It
must be set **before** the scene proxy is created — the proxy copies the pointer in its
constructor (`StaticMeshSceneProxy.cpp:338`).

`MeshCardRepresentation::SetCardsFromBounds` is `ENGINE_API` (`MeshCardRepresentation.h:36`), so
the whole path is reachable from the game module with no engine edit. `UDynamicMeshComponent`
uses exactly this call with `bMostlyTwoSided = true`.

### What bounds cards actually are — and their ceiling

`SetCardsFromBounds` (`MeshCardRepresentation.cpp:150-188`) always emits **six cards, one per
face of the bounding box**, each looking inward, each extended by `CardZOffset = 5`. Thin bounds
are safe (the offset prevents degeneracy).

A card captures only the **nearest** surface along its direction. Six box faces therefore cannot
represent a volume holding several surfaces at different depths — the down-card hits the roof and
the floor beneath it gets nothing. This is the root cause of every artifact in section 4.

Epic states the workflow rule directly:

> "Only meshes with simple interiors can be supported — **walls, floors, and ceilings should all
> be separate meshes.** Importing an entire room, which includes furniture, in a single mesh is
> not expected to work with Lumen."

> "Significant pink areas in Lumen Surface Cache view mode should be solved by raising the **Max
> Lumen Mesh Cards** in the Static Mesh settings, or **splitting the mesh into multiple parts**."

Real static meshes get up to **12 cards (default, raisable)** placed by an offline surfel-fitting
builder. Runtime meshes get **6 on a box**. That gap is the whole remaining problem.

### Nanite is not available and not needed

`NaniteBuilder` is an editor module — referenced in `Engine.Build.cs` only inside the
`if (Target.bBuildEditor)` block, and its own `Build.cs` opens with
`// NaniteBuilder module is an editor module`. There is no runtime Nanite build path, so Nanite
requires baking `.uasset`s, which the project's architecture rules out.

It also buys nothing here: it accelerates captures for high-poly meshes, and VtMB maps are ~20k
tris. The one place it may matter is **ISM components** — Epic's docs say *"Foliage and Instanced
Static Mesh Components can only be supported if the mesh is using Nanite."* Our GAME_LUMP props
are ISMs. **Unverified** — `FInstancedStaticMeshSceneProxy` inherits `GetMeshCardRepresentation`
from `FStaticMeshSceneProxy`, and no Nanite gate is visible in the capture path, so the doc line
may be stale. Hit lighting covers these regardless.

### Hit lighting is a fallback, not an alternative

`LumenReflectionHardwareRayTracing.usf:289`:

```hlsl
if (Result.bIsHit && (HitLightingForceEnabled != 0 || !Result.bIsRadianceCompleted))
```

A ray re-traces with hit lighting when hit lighting is force-enabled **or** the surface-cache
lookup came back incomplete. Setting `r.Lumen.Reflections.HardwareRayTracing.HitLighting=1` does
**not** change the ray lighting mode (`EHitLightingMode` stays `SurfaceCache`, so
`HitLightingForceEnabled` stays 0) — it only lets a *miss* fall through to a second trace. Cost
therefore scales with the uncovered fraction and falls as coverage improves.

**Config note:** the shipped ini previously set `r.Lumen.HardwareRayTracing.Reflections.HitLighting`,
which is not a real cvar name. The correct one is
**`r.Lumen.Reflections.HardwareRayTracing.HitLighting`** (`LumenReflectionHardwareRayTracing.cpp:53`,
default 0). `Config/DefaultEngine.ini` now sets it to 1.

`r.Lumen.HardwareRayTracing.MaxTraceDistance`, two lines below it in the same ini block, is
**also not a real cvar** and is a dead line. Real neighbours are
`r.LumenScene.FarField.MaxTraceDistance` and
`r.Lumen.Reflections.RadianceCache.MaxTraceDistance`; Lumen's overall trace distance is a
post-process setting. Left alone pending an owner call on which was intended.

## 3. What is implemented

Gated by **`elysium.LumenCards`** (default 1) and **`elysium.LumenCardCellCm`** (default 2048),
both read at map-build time — `elysium.reload` or re-travel to A/B.

- **Props** — `FElysiumStaticMeshBuilder::Build` calls `AttachLumenCards` after
  `BuildFromMeshDescriptions`, injecting a bounds-derived `FCardRepresentationData` with
  `bMostlyTwoSided = true` and `ELumenCardDilationMode::DilateOneTexel` (the pairing the engine's
  own runtime path uses).
- **World** — `AElysiumMapActor::BuildWorldChunks` replaces the single world PMC with chunked
  `UStaticMeshComponent`s. Triangles bin by **(spatial cell × face-normal axis)**, so each bucket
  is a set of same-facing surfaces — a wall bucket, a floor bucket, a ceiling bucket — which is
  Epic's modular rule derived procedurally from the BSP normals. Each bucket's component sits at
  the bucket's own bounds centre with vertices written relative to it, so local bounds hug the
  geometry. One MID per material, shared across buckets.
- **Fallback** — chunks only stand in when `elysium.LumenCards` is on **and** `bBrushCollision` is
  true (with `elysium.BrushCollision 0` the world collider *is* the render mesh, which only the
  PMC path builds). `BuildWorldChunks` returning 0 falls back to `BuildMeshFromObj`.
- **`CookObjSections`** — the section-cooking half of `BuildMeshFromObj`, split out and shared by
  both paths (`.emc` cache behaviour unchanged).
- **Click-picking** — `BuildWorldChunks` retains a per-chunk CPU triangle soup
  (`FWorldChunkPickSoup`, non-Shipping, ~2.5 MB on the tutorial), the arrangement the props
  already use, because a runtime `UStaticMesh` keeps nothing on the CPU to cast against. Its
  sections are the chunk's material slots 1:1 and its vertex indices are remapped from the source
  OBJ section, preserving exactly where that section shared them — which is what the face flood
  runs on. `ElysiumPick` casts both world shapes and resolves either through one view (index +
  position accessors), so the flood and the highlight are written once.
- **`elysium.pick`** — runs `ElysiumPick::Trace` down the aim ray and logs the result. The
  Inspector's ray comes from the mouse, so this is the only way a test or an agent (over
  `elysium_console_exec`) can exercise the pick at all.

Files: `ElysiumStaticMesh.{h,cpp}`, `ElysiumMapActor.cpp`, `ElysiumMapActor.h`,
`ElysiumPick.{h,cpp}`, `ElysiumCardGen.cpp` (new), `ElysiumUE.Build.cs`,
`Config/DefaultEngine.ini`.

## 4. Measurements

### Geometry facts (`sp_tutorial_1` world OBJ)

29,522 triangles. Longest-axis extent: median 163 cm, 90th 650, 99th 1260, max 2093. 5% exceed a
1024 cm cell. Chunk bounds inflation over the nominal cell (spatial-only binning, 1024): median
1.27×, 90th 1.85×, max 2.22× — **not** the cause of the coverage failures.

### Coverage sweep (visual, at the alley vantage)

Spatial cells only:

| cell | chunks | coverage |
|---|---|---|
| 256 cm | 3316 | collapses — nearly all pink (falls under `MeshCardsMinSize`) |
| 1024 cm | 285 / 1561 sections | partial — large pink patches on floors and walls |
| 2048 cm | 91 / 947 sections | near-full |

Cell × face normal:

| cell | buckets | coverage |
|---|---|---|
| 1024 cm | 998 / 3135 sections | worse than spatial-1024 — too many meshes, culled |
| **2048 cm** | **391 / 2091 sections** | **best — near-full, visibly sharper cache detail** |
| 4096 cm | fewer | worse — one bucket spans same-facing surfaces at different depths |

An optimum in the middle, for a different reason on each side. Hence the 2048 default.

### GPU cost (`profile.bat`, 2560×1440, ms)

Committed pre-spike baseline (`rendering-perf.md`): totals 4.87 / 5.25 / 5.32 / 5.72 / 5.43;
Lumen reflections 0.06–0.07; Lumen GI 0.01.

The 2×2, spatial cells at 1024:

| Config | spawn | t1 | t2 | t3 | t4 |
|---|---|---|---|---|---|
| **Reflections** cards off, hit on | 0.49 | 0.58 | 0.60 | 0.78 | 0.75 |
| cards on, hit on | 0.42 | 0.54 | 0.63 | 0.70 | 0.67 |
| cards on, hit **off** | 0.21 | 0.18 | 0.22 | 0.17 | 0.18 |
| **Total GPU** cards off, hit on | 5.76 | 6.48 | 6.49 | 7.24 | 6.91 |
| cards on, hit on | 5.32 | 6.44 | 6.49 | 7.07 | 7.01 |
| cards on, hit off | 5.06 | 6.09 | 6.14 | 6.51 | 6.52 |

- **Surface cache coverage is free** — `LumenSceneUpdate` runs 0.05–0.07 ms; totals flat or
  slightly better.
- Adding cards makes reflections **cheaper** (0.49 → 0.42, 0.78 → 0.70) — fewer misses need the
  hit-lighting second trace, the self-scaling described in §2.
- Hit lighting costs **~0.35–0.55 ms** with coverage in place, ~3× the reflections pass.

Normal bucketing at 2048 vs spatial cells at 1024 (both cards + hit on):

| | spawn | t1 | t2 | t3 | t4 |
|---|---|---|---|---|---|
| Total GPU | 5.32 → 5.62 | 6.44 → 6.57 | 6.49 → 6.55 | 7.07 → 7.16 | 7.01 → 7.25 |
| Reflections | 0.42 → 0.37 | 0.54 → 0.58 | 0.63 → 0.69 | 0.70 → 0.76 | 0.67 → **0.86** |

+0.1–0.3 ms for more components. The t4 reflection regression is unexplained.

### Look

Bounds cards leak. With coverage on, the scene reads brighter and flatter than the PMC baseline —
shadows fill in, contrast drops. Normal bucketing at 2048 is a clear improvement over spatial
cells at 1024 but does not restore the baseline's noir. This lands on the property
`rendering-perf.md` calls load-bearing: VtMB's baked luminance spans an 82× range.

The leak is inherent to boxing: a bucket containing an exterior wall captures outdoor light and
re-emits it inward.

## 5. What does not work (do not retry)

- **Shrinking cells to tighten cards.** 256 cm collapses coverage — the buckets fall under
  `r.LumenScene.SurfaceCache.MeshCardsMinSize` and are culled. Coverage wants *few, large*
  meshes; leak wants *many, tight* ones. Bounds cards cannot give both.
- **Enlarging cells to reduce card count.** 4096 cm reintroduces the depth-layering failure.
- **Giving `UProceduralMeshComponent` a card representation.** It has no static mesh batches, so
  the capture pass would draw nothing (§2 requirement 2).
- **Nanite.** Editor-only builder; no runtime path (§2).
- **A hidden "Lumen proxy" mesh alongside the visible PMC.** The surface-cache lookup is keyed by
  the *hit* instance (`GetMeshCardsIndexFromSceneInstanceIndex`), so cards must live on the
  primitive the ray actually hits.

## 6. Card baking — the probe (passes)

The real builder is `IMeshUtilities::GenerateCardRepresentationData` (`MeshUtilities.h:176`):
surfel clustering that fits up to `MaxLumenMeshCards` to the actual surface. It ray-traces the
mesh through **Embree** (`MeshCardRepresentationUtilities.cpp:35, 116`), so it exists only in the
editor — which is the argument for baking offline rather than porting it: the sidecar route keeps
Embree out of the shipping build entirely.

The distance field is **optional** — `MeshCardRepresentationUtilities.cpp:1113` falls back to the
mesh bounds when null. We are HWRT-only and need no SDF, so the chain is just
OBJ → `UStaticMesh` → card builder → serialize.

`ElysiumCardGen.cpp` (`elysium.cards.probe [maxcards]`, editor builds, `ELYSIUM_WITH_CARDGEN`)
builds a 5 m room with **one face left open** through `BuildFromMeshDescriptions` with
`bFastBuild` + `bAllowCpuAccess`, then runs the builder:

```
probe: mesh built - 20 verts, 30 indices, 1 sections
probe: OK - 5 cards (max 12) in 83.9 ms
  card 0: dir 0, origin (-483 0 0), extent (501 501 28)
  card 1: dir 1, origin ( 483 0 0), extent (501 501 28)
  card 2: dir 3, origin (0  483 0), extent (501 501 28)
  card 3: dir 4, origin (0 0 -483), extent (501 501 28)
  card 4: dir 5, origin (0 0  483), extent (501 501 28)
```

**Five cards, not six** — the builder skipped the open face — and they sit at ±483 with extent 28,
i.e. **on the wall surfaces**, not on the bounding box at ±501. It runs on a mesh constructed in
code, in a `-game` process on the editor target. That was the one unknown behind the offline
route, and it is answered.

Cost: 84 ms for a trivial mesh → ~35 s per map at ~400 chunks, ~1 hour for a 108-map export.
Parallelizable; only reruns when geometry changes.

## 7. The sidecar — built

`<map>.cards` under `tools/out/<map>/`, written by a headless engine run, deserialized at map
build. Entries are addressed by **identity + geometry hash**, never by file position:

- a world chunk by its bucket key (cell x/y/z + the face-normal axis it was binned on)
- a prop model by its OBJ stem

so a cell-size change moves the keys, a re-export moves the hashes, and either way the lookup
misses and that mesh falls back to bounds cards. Nothing serves cards fitted to geometry that no
longer exists. The load logs coverage once per map —
`cards: 603 meshes on fitted cards, 0 on bounds cards (0 absent, 0 stale)`.

**The bake runs the real map load.** `-ElysiumCards` (`FElysiumCardRun`, sibling to the profile
and shots harnesses) loads each map exactly as the game does and fits cards to the meshes the
*game* built. There is no second chunker to keep in step with `BuildWorldChunks` — which is what
§7's original plan would have required, and why `elysium.LumenCardCellCm` stays live.

This settles the first open decision: `.cards` is **not** a pipeline contract. The exporter does
not write it and could not — `IMeshUtilities::GenerateCardRepresentationData` ray-traces through
Embree and exists only in the editor. It lives beside the export because a packaged build cannot
regenerate it, but it is engine-derived, like the `.emc` parse cache.

Files: `ElysiumCardBake.{h,cpp}` (format, store, hash, fit), `ElysiumCardRun.{h,cpp}` (the
harness), `cards.bat`, `export_all.py`, plus the install seams in `ElysiumStaticMesh.{h,cpp}` and
`ElysiumMapActor.{h,cpp}`.

### Engine facts confirmed while building it

- **`FLumenCardBuildData::operator<<` does not serialize `DilationMode`** — Epic treats the whole
  thing as unversioned derived data. Harmless here: the surfel builder never sets it (it stays
  `Disabled`), and the only `DilateOneTexel` in the project is the bounds fallback, constructed at
  runtime and never written. A baked round-trip is lossless.
- The card builder needs `bAllowCpuAccess` on the built LOD, so `ElysiumCardBake::IsBaking()`
  turns it on for a bake run only. Nothing else about the build differs, which is what makes the
  fitted meshes the same meshes.

### Cost — the ~1 hour estimate was wrong by an order of magnitude

`sp_tutorial_1`: **391 chunks + 202 prop models = 593 meshes, 2,162 cards, 4.1 s of fitting**, a
184 KB sidecar. The ~35 s/map figure extrapolated from the 84 ms probe overshot badly — the probe
paid one-off module and Embree setup that amortizes across a map. A 108-map export adds roughly
**7 minutes**, not an hour, which is why the bake is wired into `export_all.py` by default
(`--no-cards` opts out) rather than left as a manual step.

Fitted cards are also *fewer* than bounds cards: 2,162 over 593 meshes is **3.6 per mesh** against
the flat 6 a bounding box always produces.

### Measurement — the payoff is not demonstrated

`profile.bat sp_tutorial_1`, both configs on the same build, A/B'd with `elysium.LumenCardsBaked`:

| | spawn | t1 | t2 | t3 | t4 |
|---|---|---|---|---|---|
| **Total GPU** bounds cards | 3.68 | 4.61 | 4.52 | 4.78 | 4.81 |
| baked cards | 3.74 | 4.69 | 4.62 | 4.99 | 4.94 |
| **Reflections** bounds cards | 0.274 | 0.427 | 0.491 | 0.670 | 0.824 |
| baked cards | 0.267 | 0.440 | 0.462 | 0.717 | 0.730 |

**Cost is a wash** — +0.06 to +0.21 ms total, reflections mixed (t4 better, t3 worse). And at the
§4 alley vantage the two are visually near-identical; the flat, leaked look §4 describes does not
visibly recover.

The likely reason is in §4's own result: **cell × face-normal bucketing already did most of the
work the surfel fit would do.** A bucket of same-facing surfaces is a thin slab, so its bounding
box already sits close to the geometry — there is little gap left for a fitted card to close. The
fit's remaining value should be where a box is genuinely wrong (props: a chair, a railing), and
that is exactly where §8's unverified ISM question bites — if GAME_LUMP prop ISMs are not in the
surface cache at all, baking their cards buys nothing.

So the mechanism is complete and free, but **the case for keeping it is unproven**. What would
settle it, in order: (1) resolve the ISM coverage question, since props are where a bounds box is
worst; (2) A/B a prop-dense interior rather than the alley; (3) re-test at a larger cell, where
bucketing helps less and the fit should matter more.

### Still open

- **Whether to keep the bake at all** — see the three tests above. It is complete and costs
  nothing at runtime, but nothing yet shows it beating bounds cards on this map.
- **Whether to keep hit lighting on.** Untouched by this work and still undecided: it is
  self-scaling insurance that can never regress into black holes, but ~0.4 ms is real against the
  RTX 3060 floor, where `rendering-perf.md` budgets ~3–6 ms for everything outside Lumen.
  Measuring it wants the coverage question settled first.

## 8. Known gaps

- **The face-flood highlight covers only the picked chunk's share of a face.** A BSP face wider
  than the cell is binned into several chunks, which are separate meshes sharing no vertex
  indices, so the flood stops at the chunk seam. Measured at `(-35.56, -17200, -340)` yaw 0: the
  same wall resolves to the same material, hit point and distance on both paths, but the
  highlight is 2 triangles chunked against 6 on the PMC. The pick itself is exact; only the
  outline is clipped.
- **Draw calls up ~3.5×** — 2091 sections across 391 components vs ~355 on one PMC. Trivial at 20k
  tris; unmeasured on larger maps.
- **Coverage judged at one vantage only.** The `profile.bat` vantages were never eyeballed; the t4
  reflection regression may be worse coverage somewhere unexamined.
- **No controlled A/B of bucketing at a fixed cell size** — bucketed-2048 was compared against
  spatial-1024, so cell size and bucketing moved together.
- **ISM prop coverage unverified** (§2) — now the load-bearing unknown, because it decides whether
  the §7 bake is worth keeping. The bake fits cards for all 202 prop models and the runtime
  installs every one, but if `FInstancedStaticMeshSceneProxy` is excluded from the surface cache
  without Nanite (as Epic's docs claim and the 5.8 capture path does not appear to enforce), that
  work reaches only the per-entity `prop_dynamic` components.
- The **`[VSM] Non-Nanite Marking Job Queue overflow`** warning still appears, as it does on the
  PMC path (`rendering-perf.md` notes it as transient).

## 9. Repro

```
build.bat
cards.bat sp_tutorial_1                 # bake this map's cards (no arg = every exported map)
play.bat sp_tutorial_1                  # or the editor-target exe with -ElysiumMap=

elysium.LumenCards 0|1                  # A/B the whole path (needs elysium.reload)
elysium.LumenCardsBaked 0|1             # A/B baked fits vs bounds cards (needs elysium.reload)
elysium.LumenCardCellCm <cm>            # chunk size (needs elysium.reload; re-bake after)
elysium.LumenCardMax <n>                # cards per mesh the bake may fit (bake-time)
elysium.reload

r.Lumen.Visualize 3                     # Lumen Scene — what the surface cache holds
r.Lumen.Visualize 5                     # Surface Cache Coverage — pink/yellow = what hit lighting pays for
r.Lumen.Visualize 4                     # Reflection View
r.Lumen.Visualize 6                     # Geometry Normals — the ray-tracing scene
r.Lumen.Visualize.CardPlacement 1       # card boxes in the world
r.Lumen.Reflections.HardwareRayTracing.HitLighting 0|1

elysium.cards.probe [maxcards]          # the offline-card-builder probe (editor builds)
elysium.pick                            # run the click-pick down the aim ray and log the hit

profile.bat sp_tutorial_1               # per-pass GPU ms vs the rendering-perf.md baseline
shots.bat sp_tutorial_1                 # look regression at the fixed vantages
```

`LiveCoding.Compile` recompiles function bodies without dropping the session — useful for
iterating on the chunker with the camera parked. It cannot re-run a static initializer, so a cvar
*default* change still needs a full build (set the cvar at runtime instead).
