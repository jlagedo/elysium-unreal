# Sky & ambience — the RE plan and the Unreal rework

**Status: investigation open; nothing here is landed.** The current sky-cube render path is
**known wrong** (the backdrop draws incorrectly — a standing defect), and the sky/ambience
exports are **not trusted** until the two orientation conventions (K1, K2 below) are settled
empirically. This doc is the working plan: the verified facts, the honest unknowns, the RE
tasks that close them, the rework tasks that follow, and the owner decisions each divergence
needs. Tasks graduate into `roadmap.md` (RE backlog / phase tasks) as they are picked up;
decisions get dated entries in `decisions.md` when made.

Related: `docs/lighting.md` (WORLDLIGHTS facts, Godot-banner), `docs/light-attribution.md`
(fixture-vs-fill), `docs/rendering-perf.md` (why Lumen is load-bearing), `docs/color_gamma.md`
(the LDR look), `docs/asset-enhancement.md` (upscaled sky faces as an A/B layer).

## Why this exists

VtMB's ambience was never explored as its own subject — the sky path was built bottom-up
(decode faces → cube → SkyLight + backdrop) on **assumed** conventions, and the ambience model
was inherited from the Godot prototype's calibration rather than derived from how the original
engine works. Two things force the revisit:

1. **The sky cube renders wrong.** The decoded faces are correct as images (upright, level
   horizon — verified by eye on `sp_tutorial_1`'s `la` set), so the defect is in the
   assembly/sampling chain, which stacks *two* unverified conventions (K1 × K2).
   `tools/sky_upscale.py` is standing evidence the first was never settled: it auto-detects
   the horizon-ring order and per-face flips by seam-error minimisation, with a comment saying
   this exists "so we don't have to hard-code the decode's face-orientation convention".
2. **We cannot say how the original engine produces its ambient light.** We know what the
   *compiler* consumed (lump 15) and what it *emitted* (lump 8); the runtime half — what the
   engine itself adds per frame, and to what — is inference from later Source versions, not RE.

## What we know (verified)

| Fact | Source |
|---|---|
| VtMB has no runtime GI and no global ambient constant; perceived ambience = baked radiosity (lump 8) + author-sprayed fill lights + optional sky pair + fog | `docs/lighting.md`, `docs/light-attribution.md` |
| The look is **bounce-dominated**: a direct-light model (even correctly shadowed) fits the baked lightmaps with R² ≈ 0 | `tools/probe_light_calibration.py`, `docs/rendering-perf.md` |
| WORLDLIGHTS (lump 15) carries the compiled light-source set; type 3 `emit_skylight` (sun) and type 5 `emit_skyambient` come from `light_environment` | `docs/lighting.md`, `tools/bsp.py` |
| v17 `dface_t` carries **two full bakes** — `day[8]` / `night[8]` style slots — plus `avgLightColor[8]` at offset 0; the modern ambient-cube lumps are empty in VtMB data | `tools/CLAUDE.md` (byte-probed) |
| ~20% of the 2,001 measured lights are GI-substitute fill; curation is by hand survey (decided 2026-07-26) | `docs/light-attribution.md` |
| Every one of the 10 exported maps has a `skyname` and 6 decodable 512×512 faces | `.env` sidecars, measured 2026-07-26 |
| 6 of 10 exported maps carry the sun+skyambient pair; `ch_temple_1` carries **three** of each (multiple `light_environment`s); `sm_hub_1` — the outdoor hub street — carries **none** | `.lights` sidecars, measured 2026-07-26 (supersedes `lighting.md`'s Godot-era "only 2 maps" count) |
| Sky faces live at `materials/skybox/<skyname>{up,dn,lf,rt,ft,bk}` in the standard TTH/TTZ pair | `tools/UE_bsp_to_scene.py` |
| `sky_camera` carries the fog rendered behind the 3D skybox; the 3D skybox transform is `world(v) = scale·(v − origin)` | exporter + `bake_map.py`, in-engine verified |
| `TOOLS/toolsskybox` faces mark where the engine draws sky; the exporter skips them | `tools/CLAUDE.md` |

The `sm_hub_1` datum matters beyond inventory: an outdoor night street whose sky contributes
**zero** light — its "sky glow" is entirely sprayed fill. That is the same map where the
fill-classifier fails and where the load-bearing-fill hypothesis lives
(`docs/light-attribution.md` → "Where to pick up"). The sky/ambience model and the fill
question are one subject.

## What we do not know (the unknowns)

Each unknown states what would settle it. Nothing downstream of an unknown ships as "faithful"
until it is closed.

- **K1 — the Source sky-face orientation convention.** Which compass direction each of
  `ft/bk/lf/rt` faces, and what per-face rotation/flip each face (including `up`/`dn`) needs
  when assembled into a continuous sky. Never settled; `sky_upscale.py` works around it.
  *Settled by:* RE-A2 (labelled-sky probe in the original game) and/or RE-A1 (decompile the
  sky draw). The seam-solver from `sky_upscale.py` gives relative orientation from images
  alone; anchoring to world axes needs one of the two.
- **K2 — Unreal's manual-cubemap conventions.** `BuildSkyCube` packs raw BGRA faces into
  platform-data slices in +X,−X,+Y,−Y,+Z,−Z order with **zero rotations**, and `M_Sky` samples
  by `−CameraVector`. Both the face-slice u/v orientation (D3D cube conventions) and the
  world-vector→face mapping under UE's shader are assumed, not verified. The header comment
  ("per-face rotation is irrelevant — IBL only") predates the cube's second job as the visible
  backdrop and is wrong for it. *Settled by:* RE-B1 (labelled-cube probe in our runtime).
- **K3 — how the VtMB engine lights models at runtime.** The ambient term dynamic entities
  receive: `avgLightColor` sample? a lightmap point sample? a worldlight sum? VtMB predates
  the ambient-cube lumps. *Settled by:* Ghidra on `StudioRender.dll`/`engine.dll` (RE-A3).
- **K4 — what selects the day vs night bake.** Two full lightmap sets exist per face; the
  selection mechanism (cvar? worldspawn key? hardcoded?) is unknown, and it decides which set
  is our ground truth. *Settled by:* RE-A4.
- **K5 — WORLDLIGHTS' actual runtime role.** Working hypothesis: the world is lit **only** by
  lightmaps at runtime and lump 15 exists for the compiler + dynamic-model lighting. If true,
  our rig re-purposes compiler input as runtime lighting — defensible (it is the authored
  source set), but the faithfulness claim changes: the *authored look* is lump 8, and the rig
  + Lumen is our reconstruction of it. *Settled by:* RE-A3 (same decompile pass).
- **K6 — VRAD's skylight/skyambient semantics.** How the 2003-era compiler applied the sky
  pair (sky-visibility test, hemisphere weighting) — what "skyambient" actually meant to the
  bake — and what multiple `light_environment`s mean (`ch_temple_1` has 3; both our bake and
  rig currently keep only the last type-5 row, silently). *Settled by:* RE-A5, plus a
  data check comparing lump 8 luxels on sky-visible vs enclosed faces.
- **K7 — the engine's 2D-sky draw itself.** Confirmed LDR, but the exact chain — texture →
  framebuffer scaling, gamma treatment, and how `sky_camera` fog blends over the sky at
  `fogend` — is unknown. This is what our `Brightness 4` constant hand-waves. *Settled by:*
  RE-A1 + reference captures (RE-A6).
- **K8 — the full-game inventory.** Sky pair presence, `skyname` distribution, fog params, and
  multiple-`light_environment` maps across all 108 maps (we have 10). *Settled by:* RE-A7, a
  data-only scan.

## Ground truth and instruments

The rework is calibrated against data we already hold plus the original game:

- **Lump 8 (night set, pending K4)** — the authored ambience, decoded by `tools/lightmap.py`;
  `probe_light_calibration.py` already fits against it.
- **The user's VtMB install, running** — reference screenshots at known map/camera positions
  (`elysium.campos` vantages have Source-space equivalents via the inverse transform). Loose
  files shadow the VPKs, so a labelled skybox drops in without repacking (RE-A2).
- **Our headless harnesses** — `shots.bat` (same vantages, PNG + manifest), `probe.bat`,
  `elysium.lightprobe`, the MCP screenshot/teleport tools for scripted A/Bs.
- **Ghidra** — `tools/ghidra/run.ps1`; targets `engine.dll` (sky draw, fog, day/night),
  `StudioRender.dll` (model ambient). Findings cited into this doc per `docs/CLAUDE.md`.

## Phase A — RE: settle the facts

- **RE-A1 — decompile the sky draw.** `engine.dll`: locate the skybox renderer (search the
  string pool for `skybox/%s` / face-suffix construction; the 2003 codebase names it
  `R_DrawSkyBox` or near). Extract: face→direction binding, vertex/texcoord assignment per
  face (K1), any colour scaling (K7), and the fog-over-sky blend (K7).
- **RE-A2 — labelled-sky probe in the original game.** Author six 512×512 faces, each with a
  unique colour, its name in large type, and distinct corner markers. Drop them as a loose
  `materials/skybox/` set shadowing a real skyname, launch VtMB on an exterior map, capture
  all four compass headings + up/down. Result: the complete empirical K1 answer, no
  decompilation required. (Owner-run: needs the game interactively.)
- **RE-A3 — model lighting + worldlight runtime role.** `StudioRender.dll` + `engine.dll`:
  how a model's ambient term is produced (`avgLightColor`? lightmap sample?) and whether any
  world-surface rendering reads lump 15 at runtime (K3, K5).
- **RE-A4 — day/night selection.** Find the reader of `dface_t.day/night` (K4). Check
  worldspawn keys and cvar tables first (cheap), then the decompile.
- **RE-A5 — VRAD skyambient semantics.** Data-first: compare lump 8 luxels on sky-visible vs
  enclosed faces of a sky-pair map (`sp_tutorial_1`) to see what the skyambient physically did
  to the bake; decompile VtMB's rad compiler only if data is ambiguous (K6). Also settle
  multiple-`light_environment` semantics (does the engine/compiler sum, or last-wins?).
- **RE-A6 — reference capture set.** From the user's install: screenshots at the `shots.bat`
  vantage equivalents on 3–4 sky maps (`sp_tutorial_1`, `sm_hub_1`, `ch_temple_1`,
  `sm_oceanhouse_1`), plus one sky-only view per skyname. The visual target for Phase B/C
  calibration. (Owner-run.)
- **RE-A7 — full-game inventory scan.** Offline script over all 108 BSPs: `skyname`,
  `light_environment` count, worldlight type histogram, `sky_camera` fog params, presence of
  `toolsskybox` faces. Output a table into this doc (K8).

## Phase B — sky rendering rework (needs K1 + K2)

- **B1 — labelled-cube probe in our runtime.** Feed the RE-A2-style labelled faces through
  `BuildSkyCube` + `M_Sky`, screenshot in-engine, record the observed slice→direction and
  per-face rotation behaviour. Closes K2 in one session and turns the current "renders
  completely wrong" into a specific, stated transform error.
- **B2 — canonical face orientation at export.** Extend the `UE_` contract to sky: the
  exporter emits `tex/sky_*.png` already rotated/flipped into a **documented canonical
  orientation** (derived from K1), so every consumer reads them verbatim — same philosophy as
  coordinates ("converted once, offline"). `sky_upscale.py` drops its seam-solver and reads
  the canonical orientation. Sidecar records the convention version.
- **B3 — correct cube assembly.** `BuildSkyCube` packs the canonical faces into UE slices with
  the exact per-slice transform B1 established, documented face by face in the code.
- **B4 — backdrop verification + brightness calibration.** `M_Sky` sampling verified against
  B1; replace the hand `Brightness 4` with a value calibrated against RE-A6 reference captures
  (sky-to-scene luminance ratio at matched vantages). Re-check `SkyLight` IBL after the fix —
  a correctly oriented cube changes the directional distribution the capture integrates.
- **B5 — upscaled faces as the enhancement A/B.** Runtime prefers `tex_hi/sky_*.png` when
  present, behind the planned `elysium.EnhancedTextures`-family toggle
  (`docs/asset-enhancement.md`); faithful default remains the 512 originals.
- **B6 — regression baselines.** `shots.bat` baselines for the sky maps once B1–B4 land; a
  sky regression is then a pixel diff, not an eyeball.

## Phase C — ambience rework (needs K3–K6)

- **C1 — skyambient with magnitude.** Both bake and rig currently keep only the type-5
  normalized colour and drop its magnitude; `SKYLIGHT_INTENSITY` is a flat 1.0. Derive
  SkyLight intensity from the type-5 magnitude (and define multi-`light_environment`
  behaviour per RE-A5), calibrated against lump 8.
- **C2 — interior SkyLight policy.** Maps whose `.env` has sky but whose worldlights carry no
  sky pair (e.g. `sm_hub_1`), and pure interiors, currently keep a cubemap-less constant-fill
  SkyLight from the bake. Decide per RE findings: faithful reading says sky contributes no
  light there — candidate: black/no-capture SkyLight, ambience carried by rig + Lumen alone.
- **C3 — Lumen art-direction knobs via a per-map PostProcessVolume.** The bake places a
  tagged, unbound PPV (pattern-matching SkyLight/Fog); `AdoptBakedLevel` adopts it. First
  payloads: **Skylight Leaking** (+ Full Skylight Leaking Distance) as the sanctioned
  replacement for load-bearing fill where Lumen has nothing to bounce, and **Indirect Lighting
  Intensity** as the bounce-strength A/B knob. Both are divergences → decision entries.
  (Ambient Cubemap stays banned: a flat occlusion-ignoring term is the contrast-killer both
  Epic and the charter warn against.)
- **C4 — calibration against the bake.** Extend `probe_light_calibration.py` to fit the
  ambient terms (SkyLight intensity, leaking, indirect intensity) against lump 8 (night set,
  pending K4) — the same by-data method that settled the point/spot falloff.
- **C5 — survey interplay.** Re-visit the `sm_hub_1` load-bearing-fill shortlist *after*
  C2/C3: with sky-glow ambience modelled correctly, the 15-light disagreement may resolve
  itself. Feeds back into `docs/light-attribution.md`.

## Decisions needed (each → dated `decisions.md` entry when made)

| # | Decision | Default per charter |
|---|---|---|
| D1 | The faithful-ambience target: lump 8 (day or night set per K4) at the shared vantages, measured — not "looks right" | Reproduce (measured target) |
| D2 | SkyLight-as-IBL is a modernization: VtMB's sky lights nothing at runtime on most maps (pending K5/K6). Keep it (Lumen needs a sky term for occlusion semantics) or zero it where the map carries no sky pair | Needs owner call after RE |
| D3 | Skylight Leaking / Indirect Lighting Intensity — non-physical knobs replacing author fill; adjudicated like the light survey (does it serve the direction?) | Needs owner call, per-map values recorded |
| D4 | Fog model: exponential height + volumetric vs Source's planar distance fog (existing accepted divergence — formalise it) | Modernize (presentation layer) |
| D5 | Enhanced (upscaled) sky faces as default-off A/B layer | Modernize behind toggle |
| D6 | Multi-`light_environment` maps: sum vs last-wins (currently silent last-wins) | Reproduce engine behaviour once RE'd |

## Sequencing

RE-A7 and RE-A5's data half are offline scripts — start anytime. B1 is one engine session and
unblocks the defect fix regardless of K1 (the UE half is independently measurable). The
critical path for "sky looks right" is **B1 → RE-A2 (or A1) → B2 → B3 → B4**; the critical
path for "ambience is right" is **RE-A3/A4/A5 → C1–C4**. Phase C without Phase A is the
current state: tuning by eye on top of unverified semantics — the thing this plan exists to
end.
