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
the walkable surface (`elysium.BrushCollision` toggles back to trimesh for A/B). The **Track-B
entity substrate** also runs at map load: `.ents` parse → one `FElysiumEntity` per def through the
class registry → spawn pass → per-brush-entity `UElysiumBrushComponent` collision/overlap bodies
(185 on the tutorial) → spawn pass, all through the two chokepoints with the I/O ring buffer + log
sinks. The **starter leaf classes** also run (`ElysiumStarterClasses.cpp`): `logic_auto` fires
`OnMapLoad` on its first-think ignition; `logic_relay` re-fires `OnTrigger` (Enable/Disable/Toggle-
gated); `trigger_multiple`/`trigger_once` (over a shared `CBaseTrigger` chain node) turn a brush
body's begin/end overlap into `OnStartTouch`/`OnEndTouch`/`OnTrigger`, filtered to the ALLOW_CLIENTS
spawnflag (the player toucher), with `trigger_once` self-`Kill`ing after first touch. Every
runtime spawn path also carries an editor-only (`#if WITH_EDITOR`, compiled out of Shipping) World
Outliner label via `ElysiumEditorObjectName` (`ElysiumEditorLabels.h`): the map actor is
`Map:<name>` in an `Elysium` folder, brush bodies are `Body_<idx>_<name>_<class>` (plus the exact
`#<idx> <name>(<class>)` debug string as a `ComponentTag`), lights are `Light_<idx>_<kind>`, prop
ISMs are `Props_<model>_<solidity>`; the same canonical debug string (`FElysiumEntity::DebugString`)
threads every I/O log line. The **P4.1 mover base** also runs (`ElysiumMover.h/.cpp`):
`FElysiumMoverBase` is the CBaseToggle primitive — `LinearMove`/`AngularMove` drive the entity's
brush body at constant velocity (no easing) toward a target transform on the substrate clock/think
(R4, no engine timers), moving swept so a solid kinematic body pushes the pawn + reports blockers,
and firing `MoveDone()` on arrival (angular rotation pivots about the def origin = the hinge).
`FElysiumDoorBase` (registered as the `CBaseDoor` chain node) layers the CBaseDoor 4-state machine
(`m_toggle_state` AT_TOP/AT_BOTTOM/GOING_UP/GOING_DOWN) — `Open`/`Close`/`Toggle`/`Lock`/`Unlock`/`Use`
inputs, `OnOpen`/`OnClose`/`OnFullyOpen`/`OnFullyClosed`/`OnLockedUse`/`OnBlockedClosing` outputs,
`wait` autoclose (`-1` = stay open), the locked path, and blocked-while-closing (deal `dmg` via
`ApplyDamage`, reverse, `OnBlockedClosing`) — with `speed`/`distance`/`wait`/`lip`/`dmg` as chain
fields; the prototype leaf `func_door_rotating` swings `distance°` about yaw/Z around the hinge.
Doors are driven through the I/O inputs (`ent_fire <door> Open`/`Unlock`) until `+use` lands. Still
planned: the rest of the mover family (`func_button`, sliding `func_door`, elevators, the full
spawnflag table + `+use`), Source movement, the rest of the master-material set, deeper scripting,
audio, menu, dialogue — see `docs/rebuild-strategy.md`. Only `sp_tutorial_1` is exported today.

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
  `UElysiumCogSubsystem` (world subsystem that registers the 15 stock CogEngine debug windows plus the
  custom Elysium windows under an `Elysium` F1-menu group; `#if ENABLE_COG`), `FElysiumCogWindow` (the
  base for those custom windows — hands them `GetMapActor`/`GetEntityWorld`/`GetGameState` since Track-B
  entities are plain C++, invisible to Cog's UObject inspector, plus a shared `static` browser→inspector
  entity selection), `FElysiumCogWindow_Status` (a live read-only map + entity-substrate summary),
  `FElysiumCogWindow_Entities` (a filter/histogram/dormancy browser over every record; sets the shared
  selection), `FElysiumCogWindow_Inspector` (the selected entity's identity, chain-walked live fields, raw
  keyvalues, 7-field outputs, and a fire-button-per-input test harness — and doubles as a **live crosshair
  inspector**: left open it keeps updating while you play (Cog renders visible windows with the menu closed),
  draws an imgui reticle, and traces the camera ray each frame to report whatever it hits — surface
  (actor/component/mesh/material + textures) *and* the entity, sticky-selected — with Text/Box/Messages
  overlay + breakpoint toggles that drive `UElysiumEntityDebugSubsystem`), `FElysiumCogWindow_EventQueue`
  (pending queue + I/O history ring buffer + pause/step), and `FElysiumCogWindow_WorldViz` (the P2.4
  world-visualization control panel: entity-gizmo off/visible/all + labels + distance sliders + color
  legend, show-triggers by class/state, I/O-beam toggle + fade window — flipping the same
  `UElysiumEntityDebugSubsystem::Viz()` state the tick renders); the inspector's fire buttons and the `ent_fire`
  verb both inject through `FElysiumEntityWorld::EnqueueInput` (a hand-made input queued via the real chokepoint).
  `UElysiumEntityDebugSubsystem` (a `UTickableWorldSubsystem`, `#if !UE_BUILD_SHIPPING`) hosts the Source-style
  `elysium.ent_*` verbs — `ent_fire` (targetname/classname/crosshair-picker, discovery-lists inputs when none
  given), `ent_dump`/`ent_info` (off the class tables), `ent_pause`/`ent_step`, `ent_break`, and the
  `ent_text`/`ent_bbox`/`ent_messages` per-entity `DrawDebug` overlay bitmask (`ENABLE_DRAW_DEBUG`) — with a
  multi-trace crosshair picker (nearest brush body, else the bodiless logic ent nearest the aim ray);
  `ent_break` + the `ent_messages` capture ride a `FElysiumDebugTapSink` it installs into each world epoch
  through `FElysiumEntityWorld::AddSink`. The same subsystem also hosts the **P2.4 world-visualization
  layers** as a `FVizSettings` block its always-running tick renders (so a layer left on stays on while you
  play): color-keyed entity gizmos, wireframe trigger-hull AABBs (by class or enabled/dormant state), and
  fading caller→target I/O beam arrows captured at the `TapDelivered` chokepoint — driven from the World Viz
  Cog window (primary) and the `elysium.ent_gizmos`/`showtriggers`/`ent_beams` verbs (echo). The gizmos are a
  **retained** layer (`FElysiumGizmoLayer`, `#if !UE_BUILD_SHIPPING`): one `UInstancedStaticMeshComponent` of
  unit cubes built once per epoch (one instance per entity), colour packed into per-instance custom data read
  by `M_Gizmo`/`M_Gizmo_XRay` (off/visible=depth-tested/all=x-ray via material swap) — so idle frames cost
  only the instanced draw, and a dormancy/liveness flip re-uploads just that one instance through
  `FElysiumEntityWorld::SetVisualChangedHook` (fired from `FElysiumEntity::OnDormancyChanged`/`Kill`), never a
  per-frame rebuild. Only the gizmo labels stay immediate-mode (distance-culled; no instanced text). The primary
  interactive surface is the live crosshair inspector
  (the Cog Entity Inspector above); the verbs are the scriptable echo. The Track-B entity substrate (plain C++, no reflection): `FElysiumVariant`
  (tagged Void/Bool/Int/Float/String/Vector/Handle), `FElysiumEntityHandle` (`{Index, Epoch}`),
  `FElysiumGameClock`, `UElysiumGameStateSubsystem` (GI subsystem: the `G` store, quest map, clock),
  `FElysiumEntityDef`/`FElysiumEntityDefs` (immutable parsed `.ents` records), `FElysiumEntity`
  (the live base entity: CBaseEntity keyfields + `Kill`/`ScriptHide`/`ScriptUnhide` + one-switch
  dormancy that gates the brush body + per-output `times` counters + a `World` back-pointer and
  `FireOutput` seam + `OnTouchStart`/`OnTouchEnd` overlap hooks), `FElysiumClassDesc`/`FElysiumClassRegistry` (the
  per-classname descriptor — factory, base-chain link, input + typed field tables — with
  case-folded chain lookup and an inert-record fallback for unregistered classnames),
  `FElysiumEntityWorld` (the substrate: one entity per def, name/class indices, spawn pass that
  also builds brush bodies, generation-checked `Resolve`, the `AcceptInput` + event-queue
  chokepoints, output firing, `RouteBrushTouch` overlap routing, think-first tick, epoch teardown,
  an `AddSink` seam for extra debug taps; owned by `AElysiumMapActor` via `TPimplPtr`),
  `UElysiumBrushComponent` (the per-brush-entity
  body: collision-only `UPrimitiveComponent` with a convex `UBodySetup` cooked from the def hulls,
  handle-carrying, dormancy-gated, solidity by classname — trigger/solid/none — routing begin/end
  overlaps back to the world; `elysium.BrushBodies` A/Bs it),
  `FElysiumEventQueue`/`FElysiumIOEvent` (the one time-sorted queue, R4), `IElysiumIOSink` with
  the always-on `FElysiumRingBufferSink` (1,000-entry I/O history) + `FElysiumLogSink`
  (`LogElysiumIO` + VLOG), and `IElysiumScriptHost`/`FElysiumNullScriptHost` (the M4 field-6
  Python seam, on `UElysiumGameStateSubsystem`).
- **Committed content (only these):** `Content/Elysium.umap` (empty boot persistent level),
  `Content/VtMB/Materials/M_VtMB_World.uasset` (world master material), `M_Sky.uasset` (2D-skybox
  cube master material), and `M_Gizmo.uasset` + `M_Gizmo_XRay.uasset` (the P2.4 entity-gizmo ISM
  masters — unlit/two-sided/translucent, colour+opacity from per-instance custom data; XRay disables
  the depth test for the x-ray mode). No converted game content, no vendored Python. A UMaterial graph
  and a `.umap` can only be compiled by the editor, so these are authored offline by generators under
  `tools/` and rebuilt as one batch by `content.bat` → `tools/build_content.py` (which the export runs
  — see Build & run).
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
- `content.bat` — rebuild all committed `Content/` assets in one headless editor session
  (`tools/build_content.py` runs every offline asset generator: world + sky master materials,
  boot map). `python tools/export_all.py` invokes this at the end of a run (skip with
  `--no-content`), so a generator can't be forgotten and go stale.
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
- `asset-enhancement.md` — the offline, code-driven remaster track (delight → upscale → PBR
  synthesis), the adjudication test, pipeline hooks, and VRAM budget. Design/not-yet-scheduled.
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
`tools/ghidra/README.md` — the headless-Ghidra RE workspace (local-only; the whole
`tools/ghidra*/` + `tools/re/` trees are gitignored RE references, not in the repo).

## The Godot project (`E:\dev\elysium`)

A **read-only reference**: the first-attempt prototype. Consulted for proven designs and
exact data formats (Track A is a class-for-class port of its runtime) and for un-ported
system source at `E:\dev\elysium\game\src`. No further work lands there. When a doc here
mentions a bare `CLAUDE.md`, `docs/archive/…`, or `game/src/…` path, it means that repo.
