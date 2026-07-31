# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game — **remastered** — on **Unreal Engine 5.8 + C++**. Everything the runtime consumes
is produced by this repo's own offline decode/export pipeline from the user's own install: the
world's *look* is baked into a gitignored `.uasset` plugin mount and adopted at load, while
collision, entities, scripting, audio and NPCs are built in code at map-load time from
engine-neutral intermediates.

## Read first

- **`docs/project/roadmap.md`** — the single source of truth work tracker: the playable-path ladder
  (PP0–PP6, the master sequence), phases P0–P13, per-task status, pipeline + RE backlogs, risk
  register. **Status lives there and nowhere else** — including this file. There is no as-built
  archive and no decision log; git history is the as-built record, and a decision's outcome is a
  present-tense fact in the doc that owns the system.
- **`docs/project/rebuild-strategy.md`** — the strategy reference: north star, principles, the two
  tracks, sidecar contracts, per-system design targets.
- **`docs/project/remaster-direction.md`** — the direction charter: what may be modernized, what must
  be reproduced, who decides.

Directory-scoped facts live in sub-files that load with the code they describe:

| File | Covers |
|---|---|
| `Source/ElysiumUE/CLAUDE.md` | the C++ runtime — module/plugin list, folder → layer map, build/test loop, gotchas |
| `pipeline/CLAUDE.md` | the offline Python pipeline — boundaries, paths, formats and exporters |
| `research/CLAUDE.md` | reproducible research cases and tooling |
| `docs/CLAUDE.md` | how the documentation set is organised and maintained |
| `Content/CLAUDE.md` | licensed source fonts and generated local package policy |

## Load-bearing rules

### Remaster direction

Keep VtMB's tone, ambience, feel and logic; raise the craft. **Not** a pixel-perfect
recreation. Three change layers, three rules:

- **Presentation** (UI, type, HUD, textures, post) — modernize freely, adjudicated by *does it
  serve VtMB's grimy gothic-punk direction (or fix a technical deficit), or invent/override an
  artist decision?* The **UI has no classic mode**: VtMB's screen structure is kept and
  re-skinned with vector type on a resolution-independent Slate/UMG stack — no VGUI port, no
  640×480 canvas, no `.fnt` bitmap atlas at runtime.
- **Feel** (movement, camera, combat) — build the RE'd original first, keep it A/B-able, polish
  one delta at a time by explicit owner call.
- **Logic & content** (entity semantics, I/O, scripts, dialogue, stats, saves) — reproduce.

**The governing rule: only change what we understand, and only on an explicit owner call.** RE
comes first; a behavioural divergence needs the faithful behaviour known and recorded **in the doc
that owns the system**, stated beside the divergence and marked as one. Default resolves to
reproduce. The **world** keeps its
faithful baseline (lightmap calibration, plus the planned `elysium.EnhancedTextures` A/B
toggle); only the UI drops its.
Full charter: `docs/project/remaster-direction.md`.

### Bring-your-own-game

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*; their
output (`$ELYSIUM_EXPORT_ROOT/`) is gitignored and regenerable, and so is everything derived from it —
including the baked `.uasset` mount `Plugins/ElysiumBaked/Content/` (only the `.uplugin` is
committed). This is the legal posture, not a convenience — prior community rebuilds died to a
C&D, not to technical failure. The only tracked `Content/` inputs are licensed loose fonts,
their licences, and directory policy; Unreal packages are generated locally and ignored.

### The two clean halves

- **Offline — `pipeline/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into intermediates under `$ELYSIUM_EXPORT_ROOT/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `UE_bsp_to_scene.py` is the map
  exporter; `export_all.py` batches. Runs against the user's install; its Python package and
  dependencies are declared in `pipeline/pyproject.toml`. A second offline stage, `dev/elysium.ps1 bake` →
  `pipeline/unreal/bake_map.py`, turns each
  exported map's *look* into real assets and a `.umap` on the `/ElysiumBaked` mount — an editor
  commandlet, so an editor build is a prerequisite for content, never for running.
- **Runtime — `Source/ElysiumUE/`** (C++): opens the baked level and **adopts** its actors
  (bucketed by the tags the bake stamped), then builds everything else in code from the
  intermediates on disk — collision, ropes, the sky cubemap, the entity substrate, NPCs, audio,
  scripting. Light values are re-derived from `.lights` at load rather than adopted, so live
  calibration always wins. Python is **never** run at runtime to produce content — the seam is
  file-based. (The embedded CPython 2.7 VM runs VtMB's *own* level scripts; it is game logic, not
  pipeline.) The architecture and what it costs: `docs/architecture/uasset-bake-spike.md`.

### The `UE_` exporter convention

An exporter prefixed **`UE_`** (e.g. `UE_bsp_to_scene.py`) is verified to emit
**Unreal-native** output: centimetres, Z-up, left-handed, triangle winding pre-reversed —
so the C++ runtime reads every file 1:1 with **no coordinate conversion**. An exporter
**without** the prefix (`mdl.py`, `bsp_to_obj.py`, …) still emits the old Godot Y-up/metres
space (`source_to_godot`) and is **flagged for review** — do not consume its output as Unreal
space until it is converted and renamed (rename + update callers + docs in the same pass).
`mdl_gltf.py` is the one standing exemption: standard glTF 2.0 is self-describing, so
glTFRuntime reorients it at load. Details: `pipeline/CLAUDE.md`.

### Coordinates are read verbatim

The Source→Unreal math lives once in `pipeline/src/elysium_pipeline/formats/bsp.py` (`source_to_unreal` for positions,
`source_dir_to_unreal` for directions; the Y negation is a reflection, so the exporter
reverses winding at OBJ-write time). Never inline it, and never convert at runtime. Full
rules: `docs/project/rebuild-strategy.md` → "Coordinate conventions".

### Docs describe design, RE, and status — not the current build

Documentation exists for what the code cannot say for itself: design intent, VtMB
reverse-engineering facts, and `docs/project/roadmap.md` status. The source is the as-built record — read
it rather than paraphrasing it. The five directory orientation `CLAUDE.md` files (`Source/ElysiumUE/
CLAUDE.md`, `pipeline/CLAUDE.md`, `research/CLAUDE.md`, `docs/CLAUDE.md`, `Content/CLAUDE.md`) point at *where* something
lives — module/plugin list, folder → layer map, build/test commands — never *how* the current
implementation behaves. The one exception is a hard-won gotcha: a non-obvious trap (lazy-init
order, a silent side effect, an easy-to-undo fix) that a source read would not reliably surface on
its own. A VtMB **format or behaviour fact** (byte layouts, discovered engine rules) belongs in
the `docs/` topic file that owns it (`docs/CLAUDE.md` → "Where a given fact belongs") — **never**
in a `CLAUDE.md`, including `pipeline/CLAUDE.md`, no matter how much it reads like "how something
works," because it describes VtMB, not our own implementation.

### CLAUDE.md carries no history

Every `CLAUDE.md` in this repo — this file and its four sub-files (`Source/ElysiumUE/CLAUDE.md`,
`pipeline/CLAUDE.md`, `research/CLAUDE.md`, `docs/CLAUDE.md`, `Content/CLAUDE.md`) — states present-tense facts only, same
as every other doc (`docs/CLAUDE.md` → "House rules"). **Never** write a roadmap task-ID
parenthetical (`(11.9)`, `roadmap 8.6`, `(PL13)`), a date, or a change/migration narrative
("was X, now Y") into any of them — task tracking lives only in `docs/project/roadmap.md`. Cite a doc by
name, with no date or task number attached.

## What runs today

**Per-task status, as-built detail, and what is next: `docs/project/roadmap.md`.** Runtime types and
where they live: `Source/ElysiumUE/CLAUDE.md`.

## Target hardware

- **Minimum floor:** NVIDIA **RTX 4060-class** desktop GPU with **16 GB**, **1440p native** — the
  shipped config targets this floor. **Recommended:** RTX 4070/5070-class.
- **DX12/SM6 is mandatory.** The render path is fully dynamic — HWRT Lumen, MegaLights, VSM
  (`Config/DefaultEngine.ini`). Every one of those silently disables under DX11/SM5 (no error,
  just a CPU-bound slideshow). The window title must read `PCD3D_SM6`. There is no non-RT
  fallback, so a DXR-capable GPU is required.
- **Lumen GI is load-bearing, not optional.** VtMB's look is indirect-bounce-dominated, so the
  bounce carries it; geometry is trivial (~20k tris/map) and MegaLights keeps hundreds of
  dynamic lights ~constant-cost.

Tuning, the SM6 setup, the MegaLights-engagement checklist, the floor budget, and the
calibration findings: **`docs/architecture/rendering-perf.md`**.

## Build & run (Windows)

Copy `dev/paths.example.env` to `.elysium.local.env`, configure the UE, VtMB and work roots,
then use `dev/elysium.ps1` as the only public command surface.

- `bootstrap` restores pinned external plugins and fetched SDKs.
- `doctor` checks repository policy, local paths, dependency ownership and generated prerequisites.
- `build [rebuild|clean|analyze]` drives UnrealBuildTool.
- `content` generates local `/Game/Elysium` and `/Game/VtMB/**` packages.
- `export [maps] [options]` runs the offline pipeline into the $ELYSIUM_EXPORT_ROOT.
- `bake [map] [stages]` generates `/ElysiumBaked/<map>/**` from pre-exported files.
- `test [filter]` runs the `Substrate`, `Content`, or fully qualified automation tier.
- `editor`, `play`, `profile`, `probe`, `shots`, `move`, `greenroom`, and `modelroom` expose
  the Unreal development and acceptance harnesses.
- `research <case>`, `ide vscode`, and `mcp` expose research, IDE and control tooling.

The running game receives `-ElysiumContentRoot` and reads the export corpus from disk. It never
runs the offline Python pipeline. Generated reports and captures remain below `ELYSIUM_WORK_ROOT`.
## Git workflow

Solo-dev project on GitHub (`jlagedo/elysium-unreal`, private). Work lands as commits
directly on `main` — do not create feature branches, pull requests, or merge requests
unless explicitly asked.

## Documentation index

`docs/index.yaml` is the ownership and kind index. The documentation set is divided into:

- `docs/project/` — the roadmap, strategy and direction charter;
- `docs/architecture/` — Unreal system designs;
- `docs/vtmb/` — engine-neutral VtMB formats and behavior;
- `docs/recovered/` — explicitly uncertain reconstructions;
- `docs/operations/` — repository, build, research and Git procedures.

`research/CLAUDE.md` documents tracked research specifications and instruments. Ghidra projects,
decompilation, dumps, captures, third-party source and generated evidence live outside the checkout
under `ELYSIUM_WORK_ROOT`.
