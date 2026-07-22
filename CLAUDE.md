# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt as a
playable game on **Unreal Engine 5.8 + C++**. The runtime loads engine-neutral
intermediates produced by this repo's own offline decode/export pipeline and builds all
engine objects in code at map-load time — no `.uasset` baking, no editor content loop.

**`docs/rebuild-strategy.md` is the master plan** (north star, the two-track roadmap, the
M0–M6 milestones). Read it first. This file is the quick-orientation fact sheet.

## Bring-your-own-game (load-bearing)

**Nothing game-sourced is committed.** The decoders read *the user's own VtMB install*;
their output (`tools/out/`) is gitignored and regenerable. This is the legal posture, not
a convenience — prior community rebuilds died to a C&D, not to technical failure. The only
assets in `Content/` are hand-authored and game-agnostic.

## The two clean halves

- **Offline — `tools/`** (Python): decodes VtMB's proprietary formats (BSP v17, MDL v2531,
  TTH/TTZ, VPK, VMT, `.fnt`, `.res`) into portable intermediates under `tools/out/<map>/`
  (OBJ+MTL+PNG/DDS, glTF `.glb`, plain-text/JSON sidecars). `bsp_to_scene.py` is the map
  exporter; `export_all.py` batches. Formats + decoders are documented in `tools/CLAUDE.md`.
  Runs against the user's install; needs Python + `tools/requirements.txt`.
- **Runtime — `Source/ElysiumUE/`** (C++): loads those intermediates from disk at map-load
  and builds `UProceduralMeshComponent` geometry, materials (MIDs off master materials),
  textures, and collision in code. Python is **never** run at runtime — the seam is
  file-based.

## Current state — M0 (verified)

Boot into an empty persistent level → `UElysiumMapSubsystem::Travel` (synchronous) loads a
map as world + 3D-skybox PMC actors, MIDs off the single master material `M_VtMB_World`,
DDS-preferred textures (PNG fallback), per-section trimesh collision, `.emc` parse-cache,
`.spawn`/`.sky` placement, a Character-movement FPS pawn with noclip, a Canvas debug HUD,
and `elysium.map` / `elysium.maps` / `elysium.debug` console commands. Only `sp_tutorial_1`
is exported today. Everything past M0 (collision from `.hulls`, light rig, entity/I-O layer,
scripting, audio, menu, dialogue) is planned, not built — see `docs/rebuild-strategy.md`.

## Repository facts

- **Engine:** UE 5.8. Module `ElysiumUE` (Runtime, Default loading phase). Plugins:
  `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline scaffolding only).
  Module deps: ProceduralMeshComponent, ImageWrapper, ImageCore, RenderCore, RHI,
  EnhancedInput, Slate, SlateCore.
- **Source layout:** `Source/ElysiumUE/Public/*.h` + `Private/*.cpp,*.h`,
  `Source/*.Target.cs`, `Source/ElysiumUE/ElysiumUE.Build.cs`. Key types:
  `UElysiumMapSubsystem`, `AElysiumMapActor`, `AElysiumGameMode`, `AElysiumHUD`,
  `AElysiumPawn`, `UElysiumGameInstance`, `FElysiumObjModel`, `FElysiumTextureCache`,
  `FElysiumMaterialFactory`, `FElysiumContentPaths`.
- **Committed content (only these):** `Content/Elysium.umap` (empty boot persistent level,
  regenerable via `tools/make_boot_map.py`) and `Content/VtMB/Materials/M_VtMB_World.uasset`
  (master material). No converted game content, no vendored Python.
- **Content root:** `FElysiumContentPaths::Root()` = `FPaths::ProjectDir()/"tools/out"`
  (in-repo, gitignored). Packaged builds later read a `content/` folder next to the exe.
- **Config:** `Config/DefaultEngine.ini` (boot map `/Game/Elysium`, `AElysiumGameMode`
  default, `UElysiumGameInstance`); `Config/DefaultInput.ini` (legacy axis/action mappings —
  EnhancedInput is configured but unused).

## Build & run (Windows)

Requires a UE 5.8 install; the `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit if yours
differs. Also requires the `tools/` pipeline to have exported at least `sp_tutorial_1` from
your VtMB install.

- `build.bat` — compile `ElysiumUEEditor` (Win64 Development) via UnrealBuildTool
  (`rebuild` / `clean` / `analyze` subcommands; extra args pass through).
- `editor.bat` — open the project in the Unreal editor (PIE via Play).
- `play.bat [map]` — launch standalone (`-game`, 1600×900); optional map name under
  `tools/out` (default `sp_tutorial_1`). WASD + mouse to fly.

## Coordinate conventions

The pipeline emits two spaces; the runtime owns both conversions via named helpers
(`GodotToUE()`, `SourceToUE()`, `GodotDirToUE()`). Full rules: `docs/rebuild-strategy.md`
→ "Coordinate conventions". Never inline the axis-swap math; every sidecar reader states
which space its file is in.

## Documentation map

`docs/` — the reverse-engineering reference this project builds on. **Engine-neutral VtMB
facts** (valid regardless of target engine):

- `rebuild-strategy.md` — **master plan** (tracks, milestones, sidecar contracts).
- `game_runtime.md` — main loop, three-layer split, RPG data model, the opening flow.
- `entity_io.md` — the Source I/O bus (7-field outputs, ScriptHide/Unhide, `use_icon`).
- `python_bridge.md` — the CPython 2.1 embedding, datamap reflection, the four call paths, `G`.
- `animation_and_movers.md` — skeletal `.mdl` v2531 (Part A) + brush movers (Part B).
- `mdl_v2531.md` — the static-geometry `.mdl` struct map.
- `audio_pipeline.md` — codecs, mixer, DSP, the SoundScheme system.
- `source_movement.md` — `CGameMovement` constants + formulas.
- `level_transitions.md` — the three spawn mechanisms + the opening map chain.
- `map-architecture.md` — the Unreal map load/unload/travel design.
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
