# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game — **remastered** — on **Unreal Engine 5.8 + C++**. The runtime loads
engine-neutral intermediates produced by this repo's own offline decode/export pipeline and
builds all engine objects in code at map-load time — no `.uasset` baking, no editor content
loop.

## Read first

- **`docs/roadmap.md`** — the single source of truth work tracker: phases P0–P10, per-task
  status, pipeline + RE backlogs, risk register. Its two companions carry the history:
  `docs/roadmap-archive.md` (the full as-built record of every completed task) and
  `docs/decisions.md` (the dated, append-only decision log). **Status and history live in
  that three-file set and nowhere else** — including this file.
- **`docs/rebuild-strategy.md`** — the strategy reference: north star, principles, the two
  tracks, sidecar contracts, per-system design targets.
- **`docs/remaster-direction.md`** — the direction charter: what may be modernized, what must
  be reproduced, who decides.

This file is the quick-orientation fact sheet. Directory-scoped facts live in sub-files that
load with the code they describe:

| File | Covers |
|---|---|
| `Source/ElysiumUE/CLAUDE.md` | the C++ runtime — module, key types, entity substrate, scripting hosts, debug layer, config |
| `tools/CLAUDE.md` | the offline Python pipeline — VtMB input formats and their decoders |
| `docs/CLAUDE.md` | how the documentation set is organised and maintained |
| `Content/CLAUDE.md` | the committed `.uasset`s and how they are regenerated |

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
comes first; a behavioural divergence needs the faithful behaviour known and recorded, plus a
dated decision in `docs/decisions.md`. Default resolves to reproduce. The **world** keeps its
faithful baseline (lightmap calibration, plus the planned `elysium.EnhancedTextures` A/B
toggle); only the UI drops its.
Full charter: `docs/remaster-direction.md`.

### Bring-your-own-game

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*;
their output (`tools/out/`) is gitignored and regenerable. This is the legal posture, not
a convenience — prior community rebuilds died to a C&D, not to technical failure. The only
assets in `Content/` are hand-authored and game-agnostic.

### The two clean halves

- **Offline — `tools/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into intermediates under `tools/out/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `UE_bsp_to_scene.py` is the map
  exporter; `export_all.py` batches. Runs against the user's install; needs Python +
  `tools/requirements.txt`.
- **Runtime — `Source/ElysiumUE/`** (C++): loads those intermediates from disk at map-load
  and builds geometry, materials, textures, and collision in code. Python is **never** run at
  runtime to produce content — the seam is file-based. (The embedded CPython 2.7 VM runs
  VtMB's *own* level scripts; it is game logic, not pipeline.)

### The `UE_` exporter convention

An exporter prefixed **`UE_`** (e.g. `UE_bsp_to_scene.py`) is verified to emit
**Unreal-native** output: centimetres, Z-up, left-handed, triangle winding pre-reversed —
so the C++ runtime reads every file 1:1 with **no coordinate conversion**. An exporter
**without** the prefix (`mdl.py`, `bsp_to_obj.py`, …) still emits the old Godot Y-up/metres
space (`source_to_godot`) and is **flagged for review** — do not consume its output as Unreal
space until it is converted and renamed (rename + update callers + docs in the same pass).
`mdl_gltf.py` is the one standing exemption: standard glTF 2.0 is self-describing, so
glTFRuntime reorients it at load. Details: `tools/CLAUDE.md`.

### Coordinates are read verbatim

The Source→Unreal math lives once in `tools/bsp.py` (`source_to_unreal` for positions,
`source_dir_to_unreal` for directions; the Y negation is a reflection, so the exporter
reverses winding at OBJ-write time). Never inline it, and never convert at runtime. Full
rules: `docs/rebuild-strategy.md` → "Coordinate conventions".

## What runs today

Map load builds world + 3D-skybox geometry as `UProceduralMeshComponent` actors with MIDs off
the world master-material set (`M_World_Opaque`/`_Masked`/`_Translucent`/`M_Additive`, picked per
surface by blend flag; bump, $envmap→Lumen roughness, and WorldVertexTransition blend on the lit
masters), DDS-preferred textures, `.hulls`/`.dispcol` brush collision as the walkable surface, ISM
static props, deferred `UDecalComponent` decals off `M_Decal`, `UCableComponent` overhead ropes, the real-time `UElysiumLightRig` on a
fully dynamic renderer, and `.env` sky/fog + `.cube` LUT.

The Track-B entity substrate runs with it: `.ents` → one entity per def through the class
registry → brush bodies → spawn pass, everything through the two chokepoints and one event
queue. Live classes cover the logic/trigger family, doors + buttons + the `+use` look-cursor
and use-icon HUD, `game_sign` popups, `ambient_generic` + SoundSchemes + mover sounds,
`prop_dynamic` static-mesh bodies (per-entity, addressable — hide/move/`Break`), NPC skeletal
bodies, and `trigger_changelevel` landmark travel. Scripting runs on an embedded CPython 2.7 VM (the
map's level script imports before the spawn pass, then merges into `__main__`, where payloads
evaluate), with an expression-evaluator fallback. Scripts hold **real entity objects**: an
attribute is either an entity input — fired through the same chokepoint a map's own I/O wire
uses — or a live field, one namespace, as VtMB's datamap reflection does it. The `ccmd`/`cvar`
console bridge is live too — `c.patchtype=""` runs the `cfg` alias and falls through to Python — so
the Unofficial Patch's real `vamputil.py` imports and the map-load `unhidePlus()`/`setPlus()` chain
arms the Plus gates.
Debug lives in the vendored Cog ImGui shell plus Source-style `elysium.ent_*` verbs, with an MCP
server (on by default in dev builds; `-NoElysiumMcp` to disable) exposing the same runtime state as
~20 `elysium_*` tools so an AI agent can drive QA and tests; automation tests run via `test.bat`.

`sp_tutorial_1` is the canonical vertical slice; `sm_pawnshop_1` and several other maps are
exported, so cross-map landmark travel is exercisable end to end.

**Per-task status, as-built detail, and what is next: `docs/roadmap.md`.** Runtime types and
where they live: `Source/ElysiumUE/CLAUDE.md`.

## Target hardware

- **Minimum floor:** NVIDIA **RTX 3060-class** desktop GPU (12 GB), **1080p** — the shipped
  config targets this floor (we develop against min-spec). **Recommended:** RTX 4070/5070-class.
- **DX12/SM6 is mandatory.** The render path is fully dynamic — HWRT Lumen, MegaLights, VSM
  (`Config/DefaultEngine.ini`). Every one of those silently disables under DX11/SM5 (no error,
  just a CPU-bound slideshow). The window title must read `PCD3D_SM6`. There is no non-RT
  fallback, so a DXR-capable GPU is required.
- **Lumen GI is load-bearing, not optional.** VtMB's look is indirect-bounce-dominated, so the
  bounce carries it; geometry is trivial (~20k tris/map) and MegaLights keeps hundreds of
  dynamic lights ~constant-cost.

Tuning, the SM6 setup, the MegaLights-engagement checklist, the floor budget, and the
calibration findings: **`docs/rendering-perf.md`**.

## Build & run (Windows)

Requires a UE 5.8 install; the `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit if yours
differs. Also requires the `tools/` pipeline to have exported at least `sp_tutorial_1` from
your VtMB install.

- `build.bat` — compile `ElysiumUEEditor` (Win64 Development) via UnrealBuildTool
  (`rebuild` / `clean` / `analyze` subcommands; extra args pass through).
- `content.bat` — rebuild all committed `Content/` assets in one headless editor session
  (`tools/build_content.py`). `python tools/export_all.py` invokes it at the end of a run
  (skip with `--no-content`), so a generator can't be forgotten and go stale.
- `editor.bat` — open the project in the Unreal editor (PIE via Play).
- `play.bat [map]` — launch standalone (`-game`, 1600×900); optional map name under
  `tools/out` (default `sp_tutorial_1`).
- `profile.bat [map] [cam]` — headless render profiling at 2560×1440/SM6: fixed vantages,
  warmup + 300-frame CSV capture (per-pass GPU ms), summary, exit — no interaction.
  `tools/profile_report.py` builds the table; results in `tools/out/_profile/`, baseline in
  `docs/rendering-perf.md` → "Profiling baseline". Add a vantage in-game with `elysium.campos`.
- `shots.bat [map] [cam]` — headless screenshot-regression capture at 2560×1440/SM6 over the
  **same** vantages as `profile.bat` (`-ElysiumShots`); PNGs + manifest under `tools/out/_shots/`
  (gitignored — game-derived baselines). Diff a run against a kept baseline to catch a look regression.
- `test.bat [filter]` — run the automation suite headless (`Substrate`/`Content` shorthands, or a
  full dotted test name; default = all). The `Substrate` tier runs under `-nullrhi`; the `Content`
  tier reads `tools/out` and self-skips unexported maps. JSON+HTML report under `tools/out/_tests/`.
  Requires the editor target built first.

**VS Code IntelliSense:** `python tools/setup_vscode.py` regenerates the local editor config —
it runs UBT's `-projectfiles -vscode` generator, then mirrors the module's include paths +
forced includes into `.vscode/settings.json` as `C_Cpp.default.*` (the fallback for every file
the compile database does not name, i.e. all headers). `settings.json` is separate because UBT
overwrites `c_cpp_properties.json` and the `.code-workspace` on every run but never touches it.
Re-run after adding a module dependency, plugin, or unresolvable source file. Open
`ElysiumUE.code-workspace`, not the bare folder — it mounts the engine tree as a second
workspace folder, which is what makes go-to-definition reach engine source. Forced includes are
build products, so the editor target must have been built at least once.

## Git workflow

Solo-dev project on GitHub (`jlagedo/elysium-unreal`, private). Work lands as commits
directly on `main` — do not create feature branches, pull requests, or merge requests
unless explicitly asked.

## Documentation index

`docs/` holds the reverse-engineering reference this project builds on — engine-neutral VtMB
facts, valid regardless of target engine. Organisation and maintenance rules: `docs/CLAUDE.md`.

| Doc | Topic |
|---|---|
| `roadmap.md` | **the work tracker** — phases, status, backlogs, risks |
| `roadmap-archive.md` | as-built records of completed roadmap tasks (same IDs) |
| `decisions.md` | the dated, append-only decision log |
| `rebuild-strategy.md` | tracks, milestone vocabulary, sidecar contracts, per-system design targets |
| `remaster-direction.md` | the direction charter — the three layers, the two adjudication tests |
| `engine-core.md` | the entity object model and its two-phase build plan |
| `entity_io.md` | the Source I/O bus — 7-field outputs, ScriptHide/Unhide, `use_icon` |
| `python_bridge.md` | the CPython embedding, datamap reflection, the four call paths, `G` |
| `game_runtime.md` | main loop, three-layer split, RPG data model, the opening flow |
| `animation_and_movers.md` | skeletal `.mdl` v2531 (Part A) + brush movers (Part B) |
| `mdl_v2531.md` | the static-geometry `.mdl` struct map |
| `audio_pipeline.md` | codecs, mixer, DSP, the SoundScheme system |
| `source_movement.md` | `CGameMovement` constants + formulas |
| `controls.md` | the input surface — keynames, bindable commands, default binds, cfg load order, control options UI |
| `input-architecture.md` | the Unreal input design — the four planes, Enhanced Input over the command bus, remapping, gamepad, reserved keys |
| `camera-view-modes.md` | the first↔third-person camera — blend weight, solver, cvars, the Unreal design |
| `level_transitions.md` | the three spawn mechanisms + the opening map chain |
| `savegame_format.md` | the `.sav` container, `.HL1/2/3` sections, and the game state they hold |
| `map-architecture.md` | the Unreal map load/unload/travel design |
| `rendering-perf.md` | the dynamic render path, perf cvars, MegaLights checklist |
| `debug-tooling.md` | the three-layer debug/dev-tooling architecture |
| `asset-enhancement.md` | the offline surface track (delight → upscale → PBR synthesis) |
| `vdata-catalog.md` | the `vdata/` rulebook inventory — each table → system → roadmap task |
| `recovered/dice-system.md` | the World-of-Darkness d10 resolver (verified: decompile + `DiceRolls.txt`) |

Godot-prototype reference docs carry a banner — see `docs/CLAUDE.md`.
`tools/ghidra/README.md` documents the headless-Ghidra RE workspace (the whole
`tools/ghidra*/` + `tools/re/` trees are gitignored, local-only).

## The Godot project (`E:\dev\elysium`)

A **read-only reference**: the first-attempt prototype. Consulted for proven designs and
exact data formats (Track A is a class-for-class port of its runtime) and for un-ported
system source at `E:\dev\elysium\game\src`. No further work lands there. When a doc here
mentions a bare `CLAUDE.md`, `docs/archive/…`, or `game/src/…` path, it means that repo.
