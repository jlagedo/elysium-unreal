# Elysium-Unreal — Rebuild Strategy

North star: rebuild VtMB as a **playable game** in Unreal Engine 5.8 + C++, consuming
engine-neutral intermediates produced by this repo's own decode/export pipeline (`tools/`).
**No original game content is ever converted into `.uasset`s.** The only assets committed to
`Content/` are hand-authored, game-agnostic scaffolding (master materials, input configs,
empty maps).

Unreal is the committed implementation. The Godot project (`E:\dev\elysium`) was the first
attempt and is now a **read-only reference**: no further work lands there. It is consulted for
proven designs and exact data formats until each system it covers is superseded here, then it
falls away entirely.

The plan has two tracks that run in parallel:

- **Track A — parity**: reach the Godot prototype's rendering/walking state
  (world, props, lights, water, decals, movement, menu). The Godot viewer is the
  reference implementation; every system here has a proven design and exact data
  formats to copy.
- **Track B — the game layer**: entities, Source I/O, triggers, movers, +use,
  travel, NPCs, scripting, dialogue, the main game loop. The Godot prototype
  **never built this** — it exists only as reverse-engineering docs and exported
  data. Unreal is the lead implementation here, designed Unreal-native from day one.

## Core principles

1. **Two clean halves**:
   - **Offline**: the Python decoders in this repo's `tools/` decode VtMB's proprietary
     formats (BSP v17, MDL v2531, TTH/TTZ, VPK, VMT, .fnt, .res) into portable intermediates
     under `tools/out/<map>/` (gitignored — regenerable from the user's install) — OBJ+MTL+
     PNG/DDS, glTF `.glb`, and plain-text sidecars. `tools/UE_bsp_to_scene.py` is the exporter;
     `tools/export_all.py` batches it.
   - **Runtime**: the C++ module `ElysiumUE` loads those intermediates from disk at
     map-load time and builds engine objects in code. No import step, no bake, no
     editor involvement. Python is **never run at runtime** — the seam between the
     halves is purely file-based.
2. **The Unreal editor is never in the content loop.** Editor Python exists only for
   offline scaffolding (`make_boot_map.py`).
3. **The pipeline lives in this repo.** `tools/` is the single home for the decoders; fixes
   and new sidecar formats land here. It is not forked from Godot — it is moved here, the
   Godot copy retired. The intermediates stay engine-neutral so the format work is not
   Unreal-specific, but there is only one consumer now.
4. **Bring-your-own-game holds.** Nothing game-sourced is committed to this repo. This is
   the load-bearing legal posture — prior community rebuilds died to a C&D, not to
   technical failure.
5. **Prove everything on `sp_tutorial_1`** (1,226 entities, 75 classnames — VtMB's own
   vertical slice exercising every system), then scale horizontally across the ~100 maps.
6. **Reference documentation lives in this repo's `docs/`.** The deep RE work this plan
   builds on is here: `entity_io.md` (I/O surface), `python_bridge.md` (scripting),
   `animation_and_movers.md` (skeletal + doors/buttons/elevators), `game_runtime.md` (main
   loop, RPG data, dialogue format), `audio_pipeline.md`, `source_movement.md`, `lighting.md`,
   `mdl_v2531.md`, `entity_visuals.md`, `color_gamma.md`, `level_transitions.md`,
   `m0_menu_build.md`, and `recovered/dice-system.md`. Do not re-derive what those already
   state. Un-ported system *source* (for class-for-class porting) remains in the read-only
   Godot repo at `E:\dev\elysium\game\src`.
7. **Faithful baseline, opt-in enhancement layer.** The committed baseline reproduces VtMB
   faithfully (geometry 1:1, dynamic GI anchored to the baked-lightmap calibration). On top of
   it, an **offline, code-driven remaster track** may raise asset fidelity *while preserving the
   art direction* — always as an A/B toggle, never a fork. The adjudication test for any such
   pass: *does it serve VtMB's grimy gothic-punk direction (or fix a technical deficit that
   fights the dynamic relight — e.g. delighting albedo), or is it inventing/overriding an artist
   decision?* Serve/fix → in; invent/override → out. Full plan, tiers, and pipeline hooks:
   `asset-enhancement.md`. Not yet scheduled (P10-ish); scaffolding exists in `tools/`.

## Current implementation state (M0 verified; M1 in progress)

Working today: boot into the empty persistent level → `UElysiumMapSubsystem::Travel`
(synchronous) loads a map as two `UProceduralMeshComponent` actors (world 355 sections +
3D skybox 84 sections for `sp_tutorial_1`), one MID per material off the single master
`M_VtMB_World`, DDS-preferred textures with PNG fallback, brush collision (`.hulls` convex
hulls + `.dispcol` displacement trimesh) as the walkable surface with a render-trimesh fallback
(async-cooked; pawn held until ground exists), `.emc` parse-cache (~1.3s warm load),
`.sky` transform, `.spawn` placement, Character-movement FPS pawn with noclip, Canvas
debug HUD, engine-console commands `elysium.map` / `elysium.maps` / `elysium.debug` /
`elysium.lights`.

All intermediates are read **verbatim** — `UE_bsp_to_scene.py` emits Unreal cm/Z-up/
left-handed with winding pre-reversed, so no runtime coordinate conversion happens.

The map actor reads `.env` and `.cube` (`ApplyEnvironment`, backed by
`ElysiumEnvironment.{h,cpp}`): the `.cube` grade becomes a per-map color-grading LUT (33³
Adobe LUT resampled to Unreal's 16³ neutral-LUT layout) on an unbound `UPostProcessComponent`;
the six `.env` sky faces build one runtime `UTextureCube` driving the visible 2D backdrop —
a large unlit inward box (`SkyDomeMesh`) running a `M_Sky` MID that samples the cube by view
direction, with a tunable `Brightness` scalar (the night skyboxes are near-black); `.env` fog
drives a `UExponentialHeightFogComponent` (off for the tutorial).

Lighting is the real-time `UElysiumLightRig` (`ElysiumLightRig.{h,cpp}`): it reads `.lights`
and spawns one Unreal light per WORLDLIGHTS source (396 for the tutorial — point/spot on
a soft exponent falloff (specular off — VtMB is pure Lambert) with `radius → reach`, a directional sun,
skyambient tinting the `SkyLight`, lightstyle patterns ticked as intensity curves). The flat
fallback sun is switched off once real lights load, and the `SkyLight` drops to a dim ambient
fill. The renderer runs fully dynamic — HWRT Lumen + MegaLights + VSM (`Config/DefaultEngine.ini`),
which **requires DX12/SM6** (every one of those features silently disables under DX11/SM5).

Calibrating the rig against VtMB's own baked lightmaps (`tools/probe_light_calibration.py`)
established that VtMB's look is **indirect-bounce-dominated** — a direct-light model, even
with correct occlusion, has zero correlation with the baked result. So Lumen GI is
load-bearing, not optional, and the dynamic lights are a modest contributor feeding it. This
also makes **baking VtMB's lump-8 lighting** the natural low-end/floor path (free GI at
runtime, the data VtMB shipped). Full render-path notes, the SM6 requirement, tuning, and the
floor-budget reality are in `docs/rendering-perf.md`.

Static props load: `.props` + `props/*.obj` build one `UStaticMesh` per unique model at
runtime (`FElysiumStaticMeshBuilder`, `BuildFromMeshDescriptions`), rendered as one
`UInstancedStaticMeshComponent` per (model, solidity) bucket, materials off `M_VtMB_World`,
convex collision on solid props. 809 instances / 161 models for the tutorial; `elysium.props`
toggles them.

Not yet built (sidecars exist unread in `tools/out`): `.sprites`, `_decals.obj`,
`.water`, `.ents`; and texlight (type-0) clustering in the rig.
`Travel`'s landmark parameter is accepted and ignored.
Input bindings are legacy axis/action mappings (EnhancedInput is configured as the
player-input class but unused). Two of the planned master materials exist (`M_VtMB_World`,
`M_Sky`); the rest are unbuilt. Movement is stock `UCharacterMovementComponent` (not the
Source math yet). `docs/map-architecture.md`'s async load state machine and Slate console are
design, not code.

## M1 — remaining tasks

Done this milestone: tooling migration; the **`UE_` coordinate conversion** (the map
exporter, now `UE_bsp_to_scene.py`, emits Unreal cm/Z-up/left-handed with winding
pre-reversed — the runtime reads every file raw); `.env` sky/fog + `.cube` LUT + `M_Sky`;
and the **light rig + lightstyles** — `UElysiumLightRig` on the map actor spawns one Unreal
light per `.lights` source (396 for the tutorial: 224 point, 170 spot, 1 sun, 1 skyambient),
point/spot on a soft exponent falloff with specular killed (`radius → reach`), lightstyle
patterns as per-frame intensity curves, skyambient tinting the `SkyLight`, and the flat
fallback sun retired when real lights exist. The renderer is fully dynamic (HWRT Lumen +
MegaLights + VSM, `Config/DefaultEngine.ini`); `elysium.lights` toggles the rig,
`elysium.LightScale` tunes point/spot intensity. Texlight (type 0) clustering is the one
deferred rig piece (none in the tutorial). Reference: `docs/lighting.md`, Godot
`LightRig.cs` + `Lightstyles.cs`.

Also done: **`.hulls`/`.dispcol` brush collision** — `AElysiumMapActor::LoadHulls`/`LoadDispCol`
build one convex `FKConvexElem` per solid `.hulls` brush (via `SetCollisionConvexMeshes`, one
cook; invisible PLAYERCLIP volumes included) and the `.dispcol` triangle soup as a displacement
trimesh, both on collision-only `UProceduralMeshComponent`s. When they load, the world render
mesh is built with no collision so the brushes are the walkable surface; `elysium.BrushCollision`
flips back to the render-trimesh for A/B. The sidecars are already Unreal cm (read verbatim).
Reference: Godot `BrushCollision.cs`.

The remaining M1 tasks and the cross-cutting polish items are tracked in
**`docs/roadmap.md`** (the single source of truth work tracker): Source movement → 4.7,
master-material set → 7.4, texture prewarm → 3.8, texlight clustering → 3.4, colour-grade
fidelity + sky orientation + A/B toggles → 3.7, repo hygiene → 0.7.

## Coordinate conventions

`UE_bsp_to_scene.py` emits **Unreal space** for everything it writes — centimetres, Z-up,
left-handed — so the runtime reads geometry and sidecars **verbatim** into `FVector`, with
no swap, scale, or winding flip:

- **Positions** (OBJ world/sky/decals/props, and the `.lights`/`.sprites`/`.ents`/`.hulls`/
  `.dispcol`/`.props` origins, `.spawn`, `.sky`) = `source_to_unreal(sx,sy,sz) = (sx, -sy, sz) * 2.54`.
  Both spaces are Z-up; the Y negation flips handedness (Source is right-handed).
- **Winding**: the Y negation is a reflection (det −1), so `UE_bsp_to_scene.py` (and
  `mdl.write_obj_scene(ue_space=True)` for prop meshes) reverses triangle winding once, at
  OBJ-write time. `ElysiumObjModel.cpp` reads tris as-is.
- **Directions** (`.lights` beam vectors) = `source_dir_to_unreal(x,y,z) = (x, -y, z)`,
  re-normalised (no scale).
- **Rotations** (`.props` prop angles) = `source_angles_to_unreal_quat(pitch,yaw,roll)`: the
  Source QAngle matrix conjugated by the handedness reflection `M=diag(1,-1,1)` (`R_u = M·R·M`),
  emitted as a unit quaternion `qx qy qz qw` and read straight into `FQuat` — no runtime math.
- **Scalars**: radii, sprite sizes, and fog distances are emitted in centimetres
  (`INCH_TO_CM`); `.spawn` yaw is emitted already negated (the Y flip reverses yaw sense).
- The Source→Unreal math lives once in `tools/bsp.py` — never inline it. Legacy non-`UE_`
  exporters still emit Godot Y-up/metres (`source_to_godot`) and are flagged for review
  (see `CLAUDE.md` → "Exporter status — the `UE_` convention").

## Sidecar contracts (what the runtime consumes)

All under `tools/out/<map>/`. Formats are fixed by the pipeline and shared with Godot:

| File | Content | Format |
|---|---|---|
| `<map>.obj/.mtl` + `tex/` | world geometry + materials | OBJ, MTL with VtMB extensions (illum 4 = alphatest, blend, Kd) |
| `<map>_sky.obj`, `.sky` | 3D skybox + `origin`/`scale` transform | OBJ + text |
| `.ents` | **all 1,226 entities**: classname, targetname, origin, `start_hidden`, raw keyvalues, brush-entity convex `hulls` + `contents`/`blocks_player`, and 7-field I/O `outputs` (`target, input, param, delay, times, python, name`) | JSON, Unreal cm (origins + entity-local hulls) |
| `.props` | static props: `safename ox oy oz qx qy qz qw solid`, models in `props/<safename>.obj` | text, Unreal cm + quaternion |
| `.hulls` / `.dispcol` | world brush convex hulls / displacement collision tris | text, Unreal cm |
| `.lights` | one line per WORLDLIGHTS source: `type origin dir rgb radius stopdot stopdot2 exponent style` | text |
| `.sprites` | env_sprite coronas: `texpath pos w h rgb amt orient` | text, Unreal cm (sizes = `scale × texpx × INCH_TO_CM`) |
| `.spawn` | `info_player_start` origin + yaw | text, Unreal cm (yaw pre-negated) |
| `.env` | skybox flag/name, fog on/color/start/end | text |
| `.water` | per-material plane, normalmap, fogcolor/dist, reflecttint | text, Unreal cm (plane Z + fogdist) |
| `.cube` | color-grade LUT | Adobe .cube |
| `_decals.obj` | infodecal geometry | OBJ |
| `npc/*.glb` | skeletal characters (mdl_skel → mdl_gltf) | glTF binary |

---

# Track A — parity with the Godot prototype

Class-for-class mapping of the Godot runtime (`E:\dev\elysium\game\src`), with the
Unreal-native substitutions:

| Godot (C#) | Unreal (C++) | Mechanism |
|---|---|---|
| `ContentPaths.cs` | `FElysiumContentPaths` | Content root at `tools/out` (dev), one-line switch to packaged location later. |
| `ObjModel.cs` | `FElysiumObjModel` | OBJ+MTL parser (albedo `map_Kd`, alpha-masked emissive `map_Ke`, alpha/blend flags). Extend `FElysiumMaterialDef` to the rest of the Godot `MaterialDef` field set: envmask, bump, WVT blend, water/decal params. |
| `TextureCache.cs` | `FElysiumTextureCache` | DDS (native DXT + mips) preferred, PNG fallback. Add: worker-thread prewarm batch (Godot `Prewarm` shape — load is texture-bound), cubemap load (`UTextureCube` from six faces). |
| `MaterialFactory.cs` | `FElysiumMaterialFactory` | MIDs off the master-material set below, parameters bound from `MaterialDef`. |
| `WorldLoader.cs` | `AElysiumMapActor` | PMC sections per material bucket (built). Grows into the per-map owner of all Track B subsystems. |
| `BrushCollision.cs` | brush collision in map actor | `UBodySetup` + `FKConvexElem` per `.hulls` brush; runtime trimesh from `.dispcol`. Replaces render-trimesh collision as the primary walkable surface (keep trimesh for displacement-heavy maps). |
| `LightRig.cs` + `Lightstyles.cs` | `UElysiumLightRig` (component on map actor) | Point/spot/directional from `.lights`; lightstyle patterns ticked as intensity curves; texlight clustering per the Godot implementation. |
| `CoronaField.cs` | billboard `UMaterialBillboardComponent`s or one Niagara system fed `.sprites` | additive glow sprites. |
| `WaterReflector.cs` (SubViewport mirror) | **Single Layer Water** material + Lumen/SSR reflections | no manual mirror camera — strict upgrade. |
| decals (corner-wrapped quads) | `_decals.obj` as translucent PMC sections first (parity), **`UDecalComponent`** projection as the upgrade | deferred decals handle angles/displacements the Godot path deferred. |
| `.cube` LUT | post-process Color Grading LUT (transient `UTexture` into per-map `FPostProcessSettings`) | native. |
| `.env` | sky material from six sky PNGs + `UExponentialHeightFogComponent` | native. |
| `SourceMovement.cs` / `PlayerController.cs` | custom `UCharacterMovementComponent` override | port the Source `CGameMovement` math line-by-line — `SourceMovement.cs` + `docs/source_movement.md` are the reference. Friction/accel/airaccel/StepMove constants verified against the decompile. |
| props (`MultiMesh`) | `UInstancedStaticMeshComponent` per unique model | static mesh built at runtime from `props/*.obj` (`FStaticMeshRenderData` path or PMC per model); `solid != 0` instances get convex collision from the render mesh. |
| VGUI2 menu (`Ui/Vgui/*`) | Slate/UMG port | `KeyValues`, `VguiScheme`, `VguiFont`, `.res` layout parsers to C++; glyph atlas-region draws; 640×480 logical space in a scale box. The Godot implementation is complete and is the porting reference. |
| `DevConsole.cs` | engine console commands now; Slate console only if it earns its keep | `elysium.*` commands cover current needs. |

**Success criterion for Track A**: side-by-side A/B match with the Godot viewer on
`sp_tutorial_1` and the hub maps (`sm_hub_1`, `ch_hub_1`, `hw_hub_1`, `la_hub_1`).

## Master materials (the one hand-authored asset set)

`UMaterial` cannot be created at runtime; everything else can. The repo commits a small,
game-agnostic set with parameter slots — the analogue of Godot's `.gdshader` files:

- `M_VtMB_World` *(exists — albedo + alpha-masked selfillum emissive)* — grows into
  `M_World_Opaque` (bump, envmap mask + cube, WVT second layer + vertex-color blend). The
  `$selfillum` path: the exporter bakes the base texture's alpha-masked RGB into `*_ke.png`
  and writes `map_Ke`; the runtime binds it onto the `Emissive` texture parameter and turns
  `EmissiveScale` on (`elysium.EmissiveScale`, default 1.5; the param defaults to 0 so
  non-selfillum surfaces never glow). Authored by `tools/add_world_emissive.py`.
- `M_World_Masked` (alphatest) and `M_World_Translucent`
- `M_Water` (Single Layer Water)
- `M_Additive` (coronas / glow props)
- `M_Decal` (deferred decal domain)
- `M_Sky` (six-face skybox) *(exists — unlit, two-sided, samples the `SkyCube` param by
  view direction; authored by `tools/make_sky_material.py`)*
- `M_VguiGlyph` / UI brushes

These encode shading logic, not game content — they belong in `Content/` permanently.

---

# Track B — the game layer (Unreal leads)

Everything in this track consumes data that is **already exported** (`.ents` carries the
complete entity/I/O surface) but has no runtime consumer in either engine. Design targets
come from the decompile-backed docs in `docs/`, not from Godot code. The concrete object
model for B1/B2 (core types, interaction flows, and the two-phase substrate→debug-layer
build plan) is `docs/engine-core.md`; the debug layer itself is `docs/debug-tooling.md`.

## B1. Entity substrate

Per-map `FElysiumEntityWorld` owned by `AElysiumMapActor` (destroyed with it — map
teardown stays trivial):

- **Parse `.ents` once** into `FElysiumEntityDef` records (classname, targetname, origin,
  keys, hulls, outputs, start_hidden).
- **Registry**: `TMultiMap<FName /*targetname*/, entity>` — targetnames are not unique;
  `Target("foo")` fans out. Plus classname index for debug/queries.
- **Spawn policy — three tiers**, not one actor per entity (1,226 entities/map):
  1. **Logic entities** (`logic_relay` ×107, `math_counter`, `logic_timer`, `logic_case`,
     `logic_auto`, `logic_pythoncheck`, `env_fade`, …): plain C++ objects
     (`FElysiumLogicEnt` subclasses), no actor, no transform. Cheap, serializable.
  2. **Brush/volume entities** (`trigger_*`, `func_door*`, `func_button`, `func_brush`,
     `func_breakable`, elevators): `UPrimitiveComponent`s on the map actor with convex
     `UBodySetup` built from the entity's `hulls` array.
  3. **Model entities** (`prop_dynamic` ×78, `prop_physics` ×54, `npc_*`,
     `item_*`): actors or ISM slots with their own mesh/physics.
  Pure-visual non-addressable entities keep shipping through typed sidecars
  (lights, coronas, decals, static props) — the accepted divergence from
  `entity_visuals.md` holds.
- **Handler factory**: `TMap<FName /*classname*/, FEntityFactoryFn>`. Unhandled
  classnames log once and become inert records (still inspectable) — coverage grows
  classname-by-classname, driven by the tutorial's histogram.
- **Visibility subsystem**: `StartHidden` / `ScriptHide` / `ScriptUnhide` = whole-entity
  OFF switch (no collision, no think, undrawn) — decompiled semantics in
  `entity_io.md`. This is the single most-driven input (`ScriptUnhide` ×1,191
  game-wide); implement it in the base entity class.

## B2. Source I/O event bus

The backbone. VtMB outputs are **7 fields**: `target, input, param, delay, times,
python, name` (field 5 is a Python call string — 6,956 across the engine-loaded
maps, 1,591 retail).

- `FElysiumEventQueue` on the map actor: time-sorted pending events
  `{fire_time, target, input, param, activator, caller}`. Ticked each frame; `times`
  counts down (−1 = infinite). `python` payloads forward to the script host (B6).
- **Input dispatch is a per-class name→member-function table** — a datamap mirror.
  This is deliberately the same mechanism the scripting layer needs
  (`Entity.__getattr__` in VtMB is datamap reflection, per `python_bridge.md`), so one
  table serves Hammer I/O, Python calls, and the debug inspector.
- **Do not route game-visible timing through `FTimerManager`** — the queue must be
  serializable for saves (VtMB saves its event queue and think contexts). Own the
  queue; it is ~100 lines.
- Implement `logic_relay` early — it is the biggest output source in the game (5,418
  outputs game-wide) and the tutorial's spine.
- Debug: port the Godot entity gizmo/inspector idea onto the HUD — F1 pick shows an
  entity's record and lets you fire inputs by hand. This is the primary test harness
  for the whole track.

## B3. Triggers, +use, and movers

- **Triggers** (`trigger_multiple` ×44, `trigger_once`, `trigger_hurt`,
  `trigger_autosave`, `trigger_inventory_check`, `trigger_environmental_audio`):
  overlap-only convex components; pawn begin/end overlap → `OnTrigger`/`OnStartTouch`
  outputs through the queue, spawnflag filters respected.
- **+use / interaction verbs**: camera line-trace against the usable set (13 classnames
  carrying `use_icon`/`locked_icon` — full 72-entry icon enum in `entity_io.md`);
  `E` dispatches the `Use` input. HUD shows the use icon (pipeline addition: export the
  icon atlas). Doors, buttons, containers, and item pickups all ride this one verb.
- **Movers** (`func_door_rotating` ×29 in the tutorial, ×1,236 game-wide; `func_door`,
  `func_button`, `func_rotating`, `func_movelinear`, elevators): kinematic component
  movement driven in C++ (position interpolation per tick), full spawnflag bit tables
  and state machines already decompiled in `animation_and_movers.md` Part B — implement
  from that spec. Chaos sweeps handle blocking; door sounds arrive with audio.
- **Success criterion**: in `sp_tutorial_1`, the elevator call button works — button →
  `Unlock`/`Trigger` outputs → doors open → `thug_2` `ScriptUnhide` fires. That chain
  exercises registry, queue, movers, visibility, and +use in one room.

## B4. Level travel

`trigger_changelevel` (×14 in tutorial) + `info_landmark` (×12): on overlap, call
`UElysiumMapSubsystem::Travel(map, landmark)` — the landmark parameter finally does
its job. New position = old offset relative to the source landmark, applied at the
destination landmark (both maps share the landmark targetname). `point_teleport` and
scripted `ChangeMap` are the other two spawn paths (`level_transitions.md`).
Convert `Travel` to the async two-stage state machine in `docs/map-architecture.md`
(fade → unload → task-thread parse → spawn → fade in) when synchronous hitches start
to matter — not before.

## B5. Game models: props, physics, NPCs

- **Static props** (Track A): ISM per unique model from `.props`.
- **Dynamic props** (`prop_dynamic` ×78): spawned from `.ents` `model` keyvalues.
  *Pipeline addition required*: export models referenced by entities (only
  GAME_LUMP static props are exported today). Skeletal ones ride the `.glb` path.
- **Physics props** (`prop_physics` ×54, `phys_hinge` ×12): **Chaos rigid bodies** with
  convex hulls from the render mesh, constraints for hinges. This is a straight
  Unreal win — the Godot plan had nothing here.
- **NPCs**: spawn from `npc_*` / `npc_maker` entity data at their origins;
  **glTFRuntime** plugin loads `npc/*.glb` (skeletal mesh + skeleton + animation)
  at runtime. *Pipeline additions*: batch NPC model export beyond the one test
  character; include-model resolution in `mdl_skel.py` (shared animation banks —
  currently only per-model animations decode). Blends/IK/ragdoll stay deferred.
- **AI later**: runtime-generated NavMesh (dynamic navmesh generation over the loaded
  world collision) + Behavior Trees/StateTree — Unreal's stock AI stack replaces the
  `info_node` graph (×154 + patrol points) rather than reimplementing Source AI
  navigation. `scripted_sequence` (×51) gets a minimal play-anim-at-marker handler
  long before real AI.

## B6. The Python connection (scripting host)

VtMB embeds CPython 2.1; `vampire.dll` owns the whole API. `python_bridge.md` has the
full RE. Key insight: **there is no big API to port** — entity method calls are datamap
reflection (B2's input table already provides this); the genuinely bespoke surface is
11 globals + 24 Character methods + the `G` persistent-flag dict + field marshalling.

Four script surfaces, in implementation order:

1. **Output field-6 call strings** (6,956; 1,591 retail) + **`logic_pythoncheck`** (51): a small C++
   expression evaluator (recursive-descent; calls, attribute access, literals,
   comparisons, and/or) over: `G.<flag>` (persistent dict, **default 0 on miss**),
   entity lookup by targetname → input table, and native bindings for the 11 globals /
   24 Character methods. This unblocks the whole tutorial.
2. **`.dlg` `dlgexpr`** conditions/actions: same evaluator, same bindings (scripts and
   dialogue are one coupled system — 208 of 345 `G` flags are written only by `.dlg`).
   Error-to-false on the 89 malformed snippets, matching retail behavior.
3. **Loose level scripts** (27 files, 690 functions, e.g. `worldspawn.levelscript =
   "tutorial"`): survey `tutorial.py` first, then decide — (a) extend the
   mini-interpreter to the restricted Python subset actually used, or (b) transpile
   offline in the pipeline to a simple IR. Both keep the runtime dependency-free;
   embedding real CPython is the fallback, not the default. `ScheduleTask` defers a
   *source string*, so whatever runs must evaluate strings at arbitrary later times.
4. Everything sits behind an `IElysiumScriptHost` interface so the choice in (3) stays
   reversible.

*Pipeline additions*: copy the loose `.py` level scripts and `.dlg` files into
`tools/out/scripts/` and `tools/out/dlg/` (they are plain text in the user's install).

## B7. Main game engine (persistent layer)

Subsystems on `UElysiumGameInstance` (persistent across map travel), map-scoped state
on the map actor:

- `UElysiumMapSubsystem` *(exists)* — map lifecycle, travel, landmark placement.
- `UElysiumGameState` — the `G` dict (~900 story flags), quest states, player RPG
  sheet (attributes/abilities/disciplines from `vdata/system/*.txt`, transcribed in
  `game_runtime.md`).
- `UElysiumScriptHost` — B6.
- `UElysiumSaveSubsystem` — mirror VtMB's block save: entity field state + event
  queue + think contexts + `G`. Serialize into a `USaveGame` container, but the
  content is our own deterministic blocks — which is why the event queue and entity
  state live in plain serializable structs, not engine timers.
- `UElysiumAudioSubsystem` — B8.
- Think loop: per-entity next-think times scheduled through the same event queue —
  one deterministic, serializable timeline for all game-visible time.
- The dice resolver (World-of-Darkness d10, decompiled port in
  `docs/recovered/dice-system.md`) slots under GameState — flagged unverified; needs
  the vroll golden test against the running game before it becomes canonical.

## B8. Audio

Fully RE'd in `audio_pipeline.md`, zero runtime exists in either engine:

- MS-ADPCM WAV (~92% of SFX): decode in C++ (the codec is simple) into
  `USoundWaveProcedural`/PCM — no offline transcode step needed, unlike Godot.
- MP3 dialogue/music: runtime decode likewise.
- `ambient_generic` (×76 in tutorial) from entity data; **SoundScheme** system
  (`sound/schemes/*.txt`) for ambience.
- Unreal wins: attenuation curves, reverb submixes, concurrency — native; MetaSounds
  optional later.

## B9. Dialogue and beyond

`.dlg` format (13-field CRLF Latin-1) is column-verified in `game_runtime.md`; no
parser exists anywhere yet. Parser + branch machine + UMG conversation UI + dlgexpr
(B6.2) + audio-by-path. Chargen, combat, and the full RPG sheet come after the
tutorial plays end-to-end — `rebuild-strategy.md` in the Godot repo holds the long-tail
toolkit table.

---

## Where Unreal beats the Godot implementation

- **Runtime geometry + lighting**: PMC sections + fully dynamic lights. Hardware
  ray-traced Lumen for GI/reflections (runtime meshes have no offline distance fields,
  so software Lumen is out; HWRT builds BLAS at runtime). **MegaLights** for the many
  shadowed point lights (394 lights in the tutorial alone). Virtual Shadow Maps.
  Replaces Godot's SDFGI+SSAO+planar-reflection stack, and can eventually replace
  per-surface `$envmap` cubemaps with real reflections.
- **Native subsystems replace hand-rolled ones**: deferred decals, Single Layer Water
  (vs. SubViewport mirror cameras), post-process LUT, exponential height fog, Niagara.
- **Chaos physics**: `prop_physics`, `phys_hinge`, breakables, later ragdoll — the
  Godot prototype has no physics-prop story at all.
- **AI stack**: runtime NavMesh + Behavior Trees/StateTree instead of porting Source
  node-graph navigation.
- **Audio engine**: submixes/attenuation/concurrency native; MS-ADPCM is the only
  custom code.
- **Ropes**: `keyframe_rope`/`move_rope` (×107 in the tutorial) → Cable Components.
- **Nanite is not applicable** (requires offline build; world meshes are ~20k tris).
- **C++ hot path**: parsing is already fast enough with the `.emc` cook-cache; if
  load time ever matters again, extend the cache — never reintroduce `.uasset` baking.

## Pipeline & tooling

**Tooling migration — done.** The decode/export pipeline lives in this repo's `tools/`
(`bsp.py`, `UE_bsp_to_scene.py`, `export_all.py`, `mdl.py`/`mdl_gltf.py`/`mdl_skel.py`, `vmt.py`,
`vpk.py`, `kv.py`, `fnt.py`, `install.py`, `tex_to_png.py`, `retex_dds.py`, `lightmap.py`,
`build_grade_lut.py`, `menu_extract.py`, `make_boot_map.py`, `make_sky_material.py`,
`add_world_emissive.py`, `set_world_material_usage.py`,
`make_testmap.py`, `sky_upscale.py`,
the `probe_*.py`/`*_probe.py` investigators, and `tools/CLAUDE.md`), with `tools/requirements.txt`
for deps (`Pillow`, `numpy`, `matplotlib` core; `torch`, `torchvision`, `spandrel`, `einops`,
`safetensors` optional, ESRGAN upscalers only). It is engine-neutral Python; the only "Godot" in
it is the `source_to_godot` coordinate convention baked into the intermediates, which the runtime
converts. `tools/out/` (generated), `tools/.venv/`, `tools/__pycache__/`, `tools/models/`, and the
entire `tools/ghidra*/` + `tools/re/` RE trees are gitignored. `FElysiumContentPaths::Root()` resolves to
`FPaths::ProjectDir()/"tools/out"` (in-repo), and `sp_tutorial_1` is exported there. The `re/` RE
toolchain (Crowbar/TemplePlus/VAMPTools/source-engine) and the `tools/ghidra/` workspace are
local-only, read-only RE references (never committed).

New sidecar formats and decoder fixes land in `tools/` from now on. The pipeline backlog
(entity-model export, script/`.dlg` copies, use-icon atlas, NPC batch export +
include-model resolution, sound-scheme/`vdata` copies, texlight merge, space fixes) is
tracked as **PL1–PL7 in `docs/roadmap.md`**. Everything else the runtime needs is already
exported.

## Milestones

**Sequencing and status live in `docs/roadmap.md`** (the single source of truth work
tracker; its traceability table maps M-numbers to roadmap phases — M3 → P1+P2+P4, M4 →
P5+P6, etc.). The M-numbers below remain the shared vocabulary for what each milestone
*means*.

Vertical slice: **play `sp_tutorial_1` start to finish, then walk into
`sm_pawnshop_1`.**

- **M0 — first pixels** *(done, verified)*: world + skybox rendering, DDS/PNG textures,
  trimesh collision, `.emc` cache, free-fly pawn, map switching, debug HUD.
- **M1 — world parity**: `.hulls`/`.dispcol` collision, Source movement component,
  light rig + lightstyles, `.env` sky/fog, `.cube` LUT, master-material set
  (alpha modes, WVT, bump/envmap), texture prewarm off the game thread. (The `UE_`
  coordinate conversion, `.env` sky/fog, `.cube` LUT, `M_Sky`, the light rig +
  lightstyles, and `.hulls`/`.dispcol` brush collision are done — see "M1 — remaining
  tasks" for what's left.)
- **M2 — dressing parity**: props via ISM (+ convex collision), decals, Single Layer
  Water, coronas, A/B match vs. the Godot viewer on tutorial + hub maps.
- **M3 — entity backbone**: entity world + registry, event queue + input dispatch,
  visibility subsystem, triggers, movers (doors/buttons/elevator), +use,
  `logic_relay`/`math_counter`/`logic_timer`, changelevel + landmark travel, entity
  debug inspector. *Success: the tutorial elevator chain works; walking out of the
  tutorial loads the pawnshop at the landmark.*
- **M4 — scripting + audio foundation**: expression evaluator + `G` + native bindings
  (field-6 calls, `logic_pythoncheck`), MS-ADPCM/MP3 decode, `ambient_generic`,
  sound schemes, door/button sounds.
- **M5 — menu + characters**: VGUI menu port (Slate/UMG), New Game flow, NPCs spawned
  from entity data via glTFRuntime, `scripted_sequence` minimal handler, dynamic props.
- **M6 — dialogue + game state**: `.dlg` parser + UI + dlgexpr, level-script execution,
  quest/XP basics, save/load. *Success: the tutorial is completable as in retail.*

Ordering rationale: M3 lands before menu/NPC work because every downstream system
(dialogue, AI, quests, saves) sits on the entity/I-O/scripting spine, and it is the
one part with no Godot reference to fall back on — de-risk it earliest.

## Repository facts

- Committed assets: `Content/Elysium.umap` (empty boot persistent level, regenerable via
  `tools/make_boot_map.py` headless) and the master materials `Content/VtMB/Materials/
  M_VtMB_World` and `M_Sky` (regenerable via `tools/make_sky_material.py` headless). No
  converted game content, no vendored Python.
- `Config/DefaultEngine.ini`: boot map `/Game/Elysium`, `AElysiumGameMode` global
  default, `UElysiumGameInstance`. `Config/DefaultInput.ini`: legacy axis/action
  mappings (WASD, mouse-look, Space jump, V noclip, T skybox, F1 debug, F10 console).
- Enabled plugins: `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline
  scaffolding only). Module deps: ProceduralMeshComponent, ImageWrapper, ImageCore,
  RenderCore, RHI, EnhancedInput, Slate, SlateCore.
- Content root: dev builds read `tools/out` in this repo (gitignored), generated by the
  in-repo pipeline (`-ElysiumMap=` selects the boot map). `FElysiumContentPaths::Root()`
  resolves to `FPaths::ProjectDir()/"tools/out"`. Packaged builds later read a `content/`
  folder next to the executable, populated by the user running the pipeline against their
  own install.
- Map lifecycle details: `docs/map-architecture.md` (note: its async-travel state
  machine and Slate console are design targets, not current code).
