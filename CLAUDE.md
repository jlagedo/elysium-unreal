# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game on **Unreal Engine 5.8 + C++**. The runtime loads engine-neutral
intermediates produced by this repo's own offline decode/export pipeline and builds all
engine objects in code at map-load time — no `.uasset` baking, no editor content loop.

**`docs/roadmap.md` is the single source of truth work tracker** (all phases, tasks,
status, pipeline/RE backlogs, risks, decision log). **`docs/rebuild-strategy.md` is the
strategy reference** (north star, principles, the two tracks, sidecar contracts, system
designs). Read those two first. This file is the quick-orientation fact sheet.

## Bring-your-own-game (load-bearing)

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*;
their output (`tools/out/`) is gitignored and regenerable. This is the legal posture, not
a convenience — prior community rebuilds died to a C&D, not to technical failure. The only
assets in `Content/` are hand-authored and game-agnostic.

## The two clean halves

- **Offline — `tools/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into intermediates under `tools/out/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `UE_bsp_to_scene.py` is the map
  exporter; `export_all.py` batches. Formats + decoders are documented in `tools/CLAUDE.md`.
  Runs against the user's install; needs Python + `tools/requirements.txt`.
- **Runtime — `Source/ElysiumUE/`** (C++): loads those intermediates from disk at map-load
  and builds `UProceduralMeshComponent` geometry, materials (MIDs off master materials),
  textures, and collision in code. Python is **never** run at runtime — the seam is
  file-based.

## Exporter status — the `UE_` convention (load-bearing)

An exporter prefixed **`UE_`** (e.g. `UE_bsp_to_scene.py`) is verified to emit
**Unreal-native** output: centimetres, Z-up, left-handed, triangle winding pre-reversed —
so the C++ runtime reads every file 1:1 with **no coordinate conversion**. There is no
Godot legacy left in a `UE_` exporter. Any exporter **without** the `UE_` prefix
(`mdl.py`, `mdl_gltf.py`, `bsp_to_obj.py`, …) still emits the old Godot Y-up/metres space
(`source_to_godot`) and is **flagged for review** — do not consume its output as Unreal
space until it is converted and renamed. When you convert one, rename it `UE_*` and update
its callers + docs in the same pass.

## Current state — M0 verified, M1 in progress

Boot into an empty persistent level → `UElysiumMapSubsystem::Travel` (synchronous) loads a
map as world + 3D-skybox PMC actors, MIDs off the single master material `M_VtMB_World`
(albedo + alpha-masked `$selfillum` emissive via `map_Ke` → `Emissive`/`EmissiveScale`,
tunable by `elysium.EmissiveScale`), DDS-preferred textures (PNG fallback), brush collision
(`.hulls` convex + `.dispcol` trimesh, render-trimesh fallback), `.emc` parse-cache,
`.spawn`/`.sky` placement, a Character-movement FPS pawn with noclip, a Canvas debug HUD,
and `elysium.map` / `elysium.maps` / `elysium.debug` / `elysium.lights` / `elysium.campos`
console commands.

M1 landed so far: the `.env` sky/fog + `.cube` LUT (`M_Sky`), and the **real-time
`UElysiumLightRig`** — one Unreal light per WORLDLIGHTS source from `.lights` (point/spot
soft exponent falloff with specular killed (VtMB is pure Lambert), directional sun,
skyambient tint, lightstyle animation), with a
fully dynamic renderer (HWRT Lumen + MegaLights + VSM; `Config/DefaultEngine.ini`). Static
props also load: `.props` + `props/*.obj` build one runtime `UStaticMesh` per unique model
(`FElysiumStaticMeshBuilder`, `BuildFromMeshDescriptions`), drawn as one
`UInstancedStaticMeshComponent` per (model, solidity) bucket with MIDs off `M_VtMB_World` and
convex collision on solid props (`elysium.props` toggles; 809 instances / 161 models for the
tutorial). **Brush collision** also lands: `AElysiumMapActor` loads `.hulls` (one convex
`FKConvexElem` per solid world brush, invisible PLAYERCLIP volumes included) and `.dispcol`
(displacement terrain trimesh) onto collision-only PMCs, replacing the render-mesh trimesh as
the walkable surface (`elysium.BrushCollision` toggles back to trimesh for A/B). Still
planned: Source movement, the rest of the master-material
set, the entity/I-O layer, scripting, audio, menu, dialogue — see `docs/rebuild-strategy.md`.
Only `sp_tutorial_1` is exported today.

## Repository facts

- **Engine:** UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase). Plugins:
  `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline scaffolding only),
  `Cog` (vendored MIT debug-UI shell under `Plugins/Cog/`; main plugin only — CogImgui/Cog/
  CogEngine/CogCommon/CogDebug/CogDebugEditor + bundled ImGui/ImPlot/NetImgui; stripped from
  Shipping via `ENABLE_COG`). Module deps: ProceduralMeshComponent, ImageWrapper, ImageCore,
  RenderCore, RHI, MeshDescription, StaticMeshDescription, PhysicsCore, EnhancedInput, Slate,
  SlateCore, CogCommon (all configs) + Cog/CogDebug/CogEngine/CogImgui (non-Shipping only).
- **Source layout:** `Source/ElysiumUE/Public/*.h` + `Private/*.cpp,*.h`,
  `Source/*.Target.cs`, `Source/ElysiumUE/ElysiumUE.Build.cs`. Key types:
  `UElysiumMapSubsystem`, `AElysiumMapActor`, `AElysiumGameMode`, `AElysiumHUD`,
  `AElysiumPawn`, `UElysiumGameInstance`, `FElysiumObjModel`, `FElysiumTextureCache`,
  `FElysiumMaterialFactory`, `FElysiumStaticMeshBuilder`, `FElysiumContentPaths`,
  `UElysiumLightRig`, `FElysiumProfileRun` (the headless profiling harness, `-ElysiumProfile`),
  `UElysiumCogSubsystem` (world subsystem that registers the stock Cog debug windows; `#if ENABLE_COG`).
- **Committed content (only these):** `Content/Elysium.umap` (empty boot persistent level,
  regenerable via `tools/make_boot_map.py`) and `Content/VtMB/Materials/M_VtMB_World.uasset`
  (master material). No converted game content, no vendored Python.
- **Content root:** `FElysiumContentPaths::Root()` = `FPaths::ProjectDir()/"tools/out"`
  (in-repo, gitignored). Packaged builds later read a `content/` folder next to the exe.
- **Config:** `Config/DefaultEngine.ini` (boot map `/Game/Elysium`, `AElysiumGameMode`
  default, `UElysiumGameInstance`); `Config/DefaultInput.ini` (legacy axis/action mappings —
  EnhancedInput is configured but unused).

## Target hardware

- **Minimum floor:** NVIDIA **RTX 3060-class** desktop GPU (12 GB), **1080p**. The shipped
  config targets this floor (we develop against min-spec).
- **Recommended:** **RTX 4070 / 5070-class**.

The render path is **fully dynamic** — HWRT Lumen (GI + reflections), MegaLights, Virtual
Shadow Maps (`Config/DefaultEngine.ini`). Two load-bearing facts:

- **DX12/SM6 is required.** Every one of those features silently disables under DX11/SM5 (no
  error, just a CPU-bound slideshow on the many lights). The window title must read
  `PCD3D_SM6`. HWRT also needs the GPU skin cache. There is no non-RT fallback in the shipped
  config, so a DXR-capable GPU is mandatory.
- **Lumen GI is not optional.** The dominant cost is light shadowing + Lumen GI, not geometry
  (VtMB is ~20k tris/map); the calibration (`tools/probe_light_calibration.py`) proves VtMB's
  look is indirect-bounce-dominated, so the bounce carries it. MegaLights keeps the
  many-light cost ~constant (hundreds of dynamic lights per map via `UElysiumLightRig`).

All tuning, the SM6 setup, the MegaLights-engagement checklist, the floor-budget reality, and
the calibration findings live in **`docs/rendering-perf.md`** (kept out of this fact sheet).

## Build & run (Windows)

Requires a UE 5.8 install; the `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit if yours
differs. Also requires the `tools/` pipeline to have exported at least `sp_tutorial_1` from
your VtMB install.

- `build.bat` — compile `ElysiumUEEditor` (Win64 Development) via UnrealBuildTool
  (`rebuild` / `clean` / `analyze` subcommands; extra args pass through).
- `editor.bat` — open the project in the Unreal editor (PIE via Play).
- `play.bat [map]` — launch standalone (`-game`, 1600×900); optional map name under
  `tools/out` (default `sp_tutorial_1`). WASD + mouse to fly.
- `profile.bat [map] [cam]` — **headless render profiling** (roadmap 0.1/0.2). Drives the
  `-ElysiumProfile` harness (`ElysiumProfiler.cpp`) at 2560×1440/SM6: fixed vantages near
  spawn, warmup + 300-frame CSV capture (per-pass GPU ms via `-csvGpuStats`), summary, exit —
  no interaction. `tools/profile_report.py` builds the table; results in `tools/out/_profile/`,
  baseline in `docs/roadmap.md` appendix. Add a vantage in-game with `elysium.campos`.

## Coordinate conventions

`UE_bsp_to_scene.py` emits **Unreal space directly** — centimetres, Z-up, left-handed,
winding pre-reversed — so the runtime reads geometry and every sidecar verbatim into
`FVector`, with no swap, scale, or winding flip. The Source→Unreal math lives once in
`tools/bsp.py` (`source_to_unreal` for positions, `source_dir_to_unreal` for directions;
the Y negation is a reflection, so the exporter reverses winding at OBJ-write time). Full
rules: `docs/rebuild-strategy.md` → "Coordinate conventions". Legacy non-`UE_` exporters
still emit Godot Y-up/metres (`source_to_godot`) — see the `UE_` convention above.

## Documentation map

`docs/` — the reverse-engineering reference this project builds on. **Engine-neutral VtMB
facts** (valid regardless of target engine):

- `roadmap.md` — **the work tracker** (single source of truth: phases P0–P10, task status,
  pipeline + RE backlogs, risk register, decision log). All other docs' plan sections
  point here.
- `rebuild-strategy.md` — strategy reference (tracks, milestone vocabulary, sidecar
  contracts, per-system design targets).
- `game_runtime.md` — main loop, three-layer split, RPG data model, the opening flow.
- `entity_io.md` — the Source I/O bus (7-field outputs, ScriptHide/Unhide, `use_icon`).
- `python_bridge.md` — the CPython 2.1 embedding, datamap reflection, the four call paths, `G`.
- `animation_and_movers.md` — skeletal `.mdl` v2531 (Part A) + brush movers (Part B).
- `mdl_v2531.md` — the static-geometry `.mdl` struct map.
- `audio_pipeline.md` — codecs, mixer, DSP, the SoundScheme system.
- `source_movement.md` — `CGameMovement` constants + formulas.
- `level_transitions.md` — the three spawn mechanisms + the opening map chain.
- `map-architecture.md` — the Unreal map load/unload/travel design.
- `rendering-perf.md` — the fully-dynamic render path, the shipped perf cvars, and the
  MegaLights-engagement checklist.
- `debug-tooling.md` — the three-layer debug/dev-tooling architecture (engine built-ins,
  the vendored Cog ImGui shell, Source-style `ent_*` verbs on the B2 chokepoints).
- `engine-core.md` — the entity object model (the "object language": plain-C++ entities
  with Unreal bodies, class registry, handles, one clock/queue, two chokepoints) and the
  two-phase build plan (core substrate, then the debug layer).
- `recovered/dice-system.md` — the World-of-Darkness d10 resolver (unverified; needs a
  golden test against the running game).

**Godot-prototype reference docs** (carry a banner; describe the read-only Godot prototype's
implementation, not Elysium-Unreal — the VtMB facts inside are still valid, the C#/Godot
detail is porting reference): `lighting.md`, `entity_visuals.md`, `color_gamma.md`,
`m0_menu_build.md`, and the "Mapping to Godot" sections of `audio_pipeline.md` /
`source_movement.md` / `animation_and_movers.md`.

`tools/CLAUDE.md` — the VtMB input formats and their standalone decoders.
`tools/ghidra/README.md` — the headless-Ghidra RE workspace.

## The Godot project (`E:\dev\elysium`)

A **read-only reference**: the first-attempt prototype. Consulted for proven designs and
exact data formats (Track A is a class-for-class port of its runtime) and for un-ported
system source at `E:\dev\elysium\game\src`. No further work lands there. When a doc here
mentions a bare `CLAUDE.md`, `docs/archive/…`, or `game/src/…` path, it means that repo.
