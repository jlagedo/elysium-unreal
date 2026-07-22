# Elysium-Unreal — Rebuild Strategy

North star: rebuild VtMB as a **playable game** in Unreal Engine 5.8 + C++, consuming the
same engine-neutral intermediates the Godot project produces. **No original game content is
ever converted into `.uasset`s.** The only assets committed to `Content/` are hand-authored,
game-agnostic scaffolding (master materials, input configs, empty maps).

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

1. **Two clean halves** (same as `E:\dev\elysium`):
   - **Offline**: the Python decoders in `E:\dev\elysium\tools` decode VtMB's proprietary
     formats (BSP v17, MDL v2531, TTH/TTZ, VPK, VMT, .fnt, .res) into portable intermediates
     under `E:\dev\elysium\tools\out\<map>\` — OBJ+MTL+PNG/DDS, glTF `.glb`, and plain-text
     sidecars. `tools\bsp_to_scene.py` is the exporter; `tools\export_all.py` batches it.
   - **Runtime**: the C++ module `ElysiumUE` loads those intermediates from disk at
     map-load time and builds engine objects in code. No import step, no bake, no
     editor involvement. Python is **never run at runtime** — the seam between the
     halves is purely file-based.
2. **The Unreal editor is never in the content loop.** Editor Python exists only for
   offline scaffolding (`make_boot_map.py`).
3. **The intermediates are shared, not forked.** `tools/out` stays the single content root
   for both engines; decoder fixes and new sidecar formats land once, in
   `E:\dev\elysium\tools`, and both runtimes consume them.
4. **Bring-your-own-game holds.** Nothing game-sourced is committed to this repo. This is
   the load-bearing legal posture — prior community rebuilds died to a C&D, not to
   technical failure.
5. **Prove everything on `sp_tutorial_1`** (1,868 entities, 88 classnames — VtMB's own
   vertical slice exercising every system), then scale horizontally across the ~100 maps.
6. **Reference documentation lives in the Godot repo.** `E:\dev\elysium\docs\` holds the
   deep RE work this plan builds on: `entity_io.md` (I/O surface), `python_bridge.md`
   (scripting), `animation_and_movers.md` (skeletal + doors/buttons/elevators),
   `game_runtime.md` (main loop, RPG data, dialogue format), `audio_pipeline.md`,
   `source_movement.md`, `level_transitions.md`. Do not re-derive what those already state.

## Current implementation state (M0 — verified)

Working today: boot into the empty persistent level → `UElysiumMapSubsystem::Travel`
(synchronous) loads a map as two `UProceduralMeshComponent` actors (world 355 sections +
3D skybox 84 sections for `sp_tutorial_1`), one MID per material off the single master
`M_VtMB_World`, DDS-preferred textures with PNG fallback, per-section trimesh collision
(async-cooked; pawn held until ground exists), `.emc` parse-cache (~1.3s warm load),
`.sky` transform, `.spawn` placement, Character-movement FPS pawn with noclip, Canvas
debug HUD, engine-console commands `elysium.map` / `elysium.maps` / `elysium.debug`.

Not yet built (sidecars exist unread in `tools/out`): `.hulls`, `.dispcol`, `.lights`,
`.props`, `.sprites`, `.env`, `.cube`, `_decals.obj`, `.water`, `.ents`. Lighting is a
placeholder sun + skylight. `Travel`'s landmark parameter is accepted and ignored.
Input bindings are legacy axis/action mappings (EnhancedInput is configured as the
player-input class but unused). Only one of the planned master materials exists.
`docs/map-architecture.md`'s async load state machine and Slate console are design,
not code.

## Coordinate conventions

The pipeline emits **two spaces**, and the runtime owns both conversions:

- **OBJ geometry** (world, sky, props, decals) is Godot space (metres, Y-up):
  Unreal position = `(gx, gz, gy) * 100`, winding reversed
  (`ElysiumObjModel.cpp`).
- **Sidecar coordinates** (`.spawn`, `.sky`, `.props`, `.lights` origins) are Source
  space (inches, Z-up): Unreal position = `(sx, -sy, sz) * 2.54`.
- **`.ents` origins and hulls** are Godot metres (same conversion as OBJ).
- Keep exactly two named helpers — `GodotToUE()` and `SourceToUE()` — and never
  inline the math. Every sidecar reader states which space its file is in.

## Sidecar contracts (what the runtime consumes)

All under `tools/out/<map>/`. Formats are fixed by the pipeline and shared with Godot:

| File | Content | Format |
|---|---|---|
| `<map>.obj/.mtl` + `tex/` | world geometry + materials | OBJ, MTL with VtMB extensions (illum 4 = alphatest, blend, Kd) |
| `<map>_sky.obj`, `.sky` | 3D skybox + `origin`/`scale` transform | OBJ + text |
| `.ents` | **all 1,868 entities**: classname, targetname, origin, `start_hidden`, raw keyvalues, brush-entity convex `hulls` + `contents`/`blocks_player`, and 7-field I/O `outputs` (`target, input, param, delay, times, python, name`) | JSON |
| `.props` | static props: `safename ox oy oz pitch yaw roll solid`, models in `props/<safename>.obj` | text, Source coords |
| `.hulls` / `.dispcol` | world brush convex hulls / displacement collision tris | text, Godot metres |
| `.lights` | one line per WORLDLIGHTS source: `type origin dir rgb radius stopdot stopdot2 exponent style` | text |
| `.sprites` | env_sprite coronas: `texpath pos w h rgb amt orient` | text, Godot metres |
| `.spawn` | `info_player_start` origin + yaw | text, Source coords |
| `.env` | skybox flag/name, fog on/color/start/end | text |
| `.water` | per-material plane, normalmap, fogcolor/dist, reflecttint | text |
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
| `ObjModel.cs` | `FElysiumObjModel` | OBJ+MTL parser. Extend `FElysiumMaterialDef` to the full Godot `MaterialDef` field set: emission/selfillum, envmask, bump, alpha mode, WVT blend, water/decal params. |
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

- `M_VtMB_World` *(exists)* — grows into `M_World_Opaque` (albedo, selfillum, bump,
  envmap mask + cube, WVT second layer + vertex-color blend)
- `M_World_Masked` (alphatest) and `M_World_Translucent`
- `M_Water` (Single Layer Water)
- `M_Additive` (coronas / glow props)
- `M_Decal` (deferred decal domain)
- `M_Sky` (six-face skybox)
- `M_VguiGlyph` / UI brushes

These encode shading logic, not game content — they belong in `Content/` permanently.

---

# Track B — the game layer (Unreal leads)

Everything in this track consumes data that is **already exported** (`.ents` carries the
complete entity/I/O surface) but has no runtime consumer in either engine. Design targets
come from the decompile-backed docs in `E:\dev\elysium\docs\`, not from Godot code.

## B1. Entity substrate

Per-map `FElysiumEntityWorld` owned by `AElysiumMapActor` (destroyed with it — map
teardown stays trivial):

- **Parse `.ents` once** into `FElysiumEntityDef` records (classname, targetname, origin,
  keys, hulls, outputs, start_hidden).
- **Registry**: `TMultiMap<FName /*targetname*/, entity>` — targetnames are not unique;
  `Target("foo")` fans out. Plus classname index for debug/queries.
- **Spawn policy — three tiers**, not one actor per entity (1,868 entities/map):
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
python, name` (field 5 is a Python call string — 1,621 across the game).

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

1. **Output field-6 call strings** (1,621) + **`logic_pythoncheck`** (51): a small C++
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

## Pipeline additions (land in `E:\dev\elysium\tools`, shared with Godot)

1. Export models referenced by `.ents` entities (`prop_dynamic`/`prop_physics` model
   keyvalues) — static-lump props only today.
2. Copy loose level scripts → `out/scripts/`, `.dlg` files → `out/dlg/`.
3. Use-icon atlas export (the 72-entry `use_icon` set).
4. Include-model resolution in `mdl_skel.py` (shared animation banks); batch NPC export.
5. Later: sound scheme/`.txt` and `vdata/system/*.txt` copies for audio and RPG data.

Everything else the runtime needs is already exported.

## Milestones

Vertical slice: **play `sp_tutorial_1` start to finish, then walk into
`sm_pawnshop_1`.**

- **M0 — first pixels** *(done, verified)*: world + skybox rendering, DDS/PNG textures,
  trimesh collision, `.emc` cache, free-fly pawn, map switching, debug HUD.
- **M1 — world parity**: `.hulls`/`.dispcol` collision, Source movement component,
  light rig + lightstyles, `.env` sky/fog, `.cube` LUT, master-material set
  (alpha modes, WVT, bump/envmap), texture prewarm off the game thread.
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
  `tools/make_boot_map.py` headless) and `Content/VtMB/Materials/M_VtMB_World` (master
  material). No converted game content, no vendored Python.
- `Config/DefaultEngine.ini`: boot map `/Game/Elysium`, `AElysiumGameMode` global
  default, `UElysiumGameInstance`. `Config/DefaultInput.ini`: legacy axis/action
  mappings (WASD, mouse-look, Space jump, V noclip, T skybox, F1 debug, F10 console).
- Enabled plugins: `ProceduralMeshComponent` (runtime), `PythonScriptPlugin` (offline
  scaffolding only). Module deps: ProceduralMeshComponent, ImageWrapper, ImageCore,
  RenderCore, RHI, EnhancedInput, Slate, SlateCore.
- Content root: dev builds read `E:/dev/elysium/tools/out` (`-ElysiumMap=` selects the
  boot map). Packaged builds later read a `content/` folder next to the executable,
  populated by the user running the pipeline against their own install.
- Map lifecycle details: `docs/map-architecture.md` (note: its async-travel state
  machine and Slate console are design targets, not current code).
