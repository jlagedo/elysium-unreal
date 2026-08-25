# Elysium-Unreal — Rebuild Strategy

North star: rebuild VtMB as a **playable game — remastered** — in Unreal Engine 5.8 + C++,
consuming engine-neutral intermediates produced by this repo's own decode/export pipeline
(`pipeline/`). Game content converted into `.uasset`s — the offline look bake — lands on a
**gitignored, regenerable mount** (`/ElysiumBaked`): **no original game content is ever
committed in any form.** Generated Unreal packages under `Content/` stay local; licensed loose
source fonts and original project-owned packages under `Content/ElysiumAuthored/` are the tracked
content inputs.

**Remaster, not pixel-perfect recreation.** Tone, ambience, feel and game logic are kept; craft
is raised with tools 2004 did not have — modern UI and typography first, then assets, feel, and
quality-of-life. The stance, the three change layers (presentation / feel / logic), the three adjudication
tests, and the rule that **every behavioural divergence needs the RE done first and an explicit
owner's call** live in **`docs/project/remaster-direction.md`** — read it with this doc.

Two tracks run in parallel:

- **Track A — parity**: reach VtMB's own rendering/walking state (world, props, lights,
  water, decals, movement) — every system here has an exact data format, exported by this
  repo's own pipeline, to build from. **The UI is not a parity target** — VtMB's own VGUI
  screens are structural reference for the modern re-skin, not a thing to reproduce
  (principle 8).
- **Track B — the game layer**: entities, Source I/O, triggers, movers, +use,
  travel, NPCs, scripting, dialogue, the main game loop. This layer exists only as
  reverse-engineering docs and exported data — Unreal is the lead implementation here,
  designed Unreal-native from day one.

## Core principles

1. **Two clean halves**:
   - **Offline**: the Python decoders in this repo's `pipeline/` decode VtMB's proprietary
     formats (BSP v17, MDL v2531, TTH/TTZ, VPK, VMT, .fnt, .res) into portable intermediates
     under `$ELYSIUM_EXPORT_ROOT/<map>/` (gitignored — regenerable from the user's install) — OBJ+MTL+
     PNG/DDS, `.eskm` containers, and plain-text sidecars.
     `pipeline/src/elysium_pipeline/exporters/UE_bsp_to_scene.py` is the map exporter;
     the `uv run elysium export` task graph batches maps and global resources.
   - **Offline, second stage**: `uv run elysium export map <map>` coordinates
     `pipeline/unreal/bake_map.py`, which bakes each exported map's
     *look* — geometry, materials, textures, props, decals, lights, fog, the `.umap` — into
     real assets on the gitignored `/ElysiumBaked` mount. It is an editor commandlet, so it
     runs beside the decoders, not beside the game.
   - **Runtime**: the C++ module `ElysiumUE` opens the baked level and adopts its actors by
     tag, then builds everything else in code from the intermediates on disk — collision,
     ropes, the sky cubemap, the entity substrate, NPCs, audio, scripting. Python is **never
     run at runtime** — the seam between the halves is purely file-based.
2. **The Unreal editor is in the offline development/content loop only, never at runtime.** Editor
   Python authors what only the editor build can produce — the map bake (`bake_map.py`,
   `bake_lib.py`, `bake_verify.py`), the generated `Content/` assets (`build_content.py`), offline
   scaffolding (`make_boot_map.py`) — while designers author original project-owned packages below
   `Content/ElysiumAuthored/`. No shipped code path invokes editor Python. The bake architecture,
   split, and costs: `docs/architecture/uasset-bake-spike.md`.
3. **The pipeline lives in this repo.** `pipeline/` is the single home for the decoders; fixes
   and new sidecar formats land here. The intermediates stay engine-neutral so the format work
   is not Unreal-specific, but there is only one consumer.
4. **Bring-your-own-game holds** — nothing game-sourced is committed to this repo. Legal
   posture and rationale: root `CLAUDE.md`.
5. **Prove everything on `sp_tutorial_1`** (1,226 entities, 75 classnames — VtMB's own
   vertical slice exercising every system), then scale horizontally across the ~100 maps.
6. **Reference documentation lives in this repo's `docs/`.** The deep RE work this plan
   builds on is here: `docs/vtmb/entity_io.md` (I/O surface), `docs/vtmb/python_bridge.md` (scripting),
   `docs/vtmb/animation_and_movers.md` (skeletal + doors/buttons/elevators), `docs/vtmb/game_runtime.md` (main
   loop, RPG data, dialogue format), `docs/vtmb/audio_pipeline.md`, `docs/vtmb/source_movement.md`, `docs/vtmb/lighting.md`,
   `docs/vtmb/mdl_v2531.md`, `docs/vtmb/entity_visuals.md`, `docs/vtmb/color_gamma.md`, `docs/vtmb/level_transitions.md`,
   `docs/vtmb/m0_menu_build.md` (the original UI's structure + `GameUI.dll` findings — reference for the
   re-skin, not a port target), and `recovered/dice-system.md`. Do not re-derive what those already
   state.
7. **Remaster stance governs every target below.** The change layers, adjudication tests, and
   default-to-reproduce rule live only in `docs/project/remaster-direction.md`; divergences are recorded in
   the topic doc that owns the system.
8. **The world keeps its faithful baseline; the UI does not.** Asset enhancement is an A/B
   layer over that world baseline, never a fork. Its surface-specific design is
   `docs/architecture/asset-enhancement.md`; its governing test remains `docs/project/remaster-direction.md`.

## Cross-system invariants

Project-wide sequencing and all task status live in `docs/project/roadmap.md`, including its
CAP, ANM and CCC programme sections. Current code
inventories live beside the code in `/Source/ElysiumUE/CLAUDE.md`, `/pipeline/CLAUDE.md`, and
`/Content/CLAUDE.md`.

Two facts shape every downstream design:

- Exported intermediates are read verbatim in Unreal space; the complete rule is below.
- VtMB's look is indirect-bounce-dominated, so Lumen GI is load-bearing. The calibration,
  render path, hardware floor, and tuning live only in `docs/architecture/rendering-perf.md`.

## Coordinate conventions

`UE_bsp_to_scene.py` emits **Unreal space** for everything it writes — centimetres, Z-up,
left-handed — so the runtime reads geometry and sidecars **verbatim** into `FVector`, with
no swap, scale, or winding flip:

- **Positions** (OBJ world/sky/props, and the `.lights`/`.sprites`/`.ents`/`.hulls`/`.dispcol`/
  `.props`/`.decals` origins, `.spawn`, `.sky`) = `source_to_unreal(sx,sy,sz) = (sx, -sy, sz) * 2.54`.
  Both spaces are Z-up; the Y negation flips handedness (Source is right-handed).
- **Winding**: the Y negation is a reflection (det −1), so `UE_bsp_to_scene.py` (and
  `mdl.write_obj_scene` for prop meshes) reverses triangle winding once, at
  OBJ-write time. `ElysiumObjModel.cpp` reads tris as-is.
- **Directions** (`.lights` beam vectors) = `source_dir_to_unreal(x,y,z) = (x, -y, z)`,
  re-normalised (no scale).
- **Rotations** (`.props` prop angles) = `source_angles_to_unreal_quat(pitch,yaw,roll)`: the
  Source QAngle matrix conjugated by the handedness reflection `M=diag(1,-1,1)` (`R_u = M·R·M`),
  emitted as a unit quaternion `qx qy qz qw` and read straight into `FQuat` — no runtime math.
- **Scalars**: radii, sprite sizes, and fog distances are emitted in centimetres
  (`INCH_TO_CM`); `.spawn` yaw is emitted already negated (the Y flip reverses yaw sense).
- The Source→Unreal math lives once in `pipeline/src/elysium_pipeline/formats/bsp.py` — never
  inline it. Every coordinate-bearing OBJ/sidecar exporter emits this space (see
  `/CLAUDE.md` → "The `UE_` exporter convention", and `/pipeline/CLAUDE.md`).

## Sidecar contracts (what the runtime consumes)

All under `$ELYSIUM_EXPORT_ROOT/<map>/`. Formats are fixed by the pipeline:

| File | Content | Format |
|---|---|---|
| `<map>.obj/.mtl` + `tex/` | world geometry + materials | OBJ, MTL with VtMB extensions (`illum 4` = alphatest; `blend 1`; semantic `glass 1`; Source `refract <amount>` + `refractmap`; Kd and texture-role paths) |
| `<map>_sky.obj`, `.sky` | 3D skybox + `origin`/`scale` transform | OBJ + text |
| `.ents` | **all 1,226 entities**: classname, targetname, origin, `start_hidden`, `sky` (3D-skybox scope), raw keyvalues, brush-entity convex `hulls` + `contents`/`blocks_player`, 7-field I/O `outputs` (`target, input, param, delay, times, python, name`), and — only when the entity's `model` key decoded a static `.mdl` — `model_mesh` (the decoded OBJ stem) + `model_quat` (Unreal-space placement rotation); `phys_hinge` additionally carries `hinge_axis` (normalized Unreal-space hinge direction) | JSON, Unreal cm (origins + entity-local hulls) |
| `.props` | static props: `safename ox oy oz qx qy qz qw solid skin sky`, models in `props/<safename>.obj`, with sibling `props/<stem>.phys` (VPhysics hulls + authored mass, `prop_physics` only — format: `docs/vtmb/phy_vphysics.md`) and `props/<stem>.skins` (alternate skin-family remaps — format: `docs/vtmb/mdl_v2531.md`) | text, Unreal cm + quaternion |
| `.hulls` / `.dispcol` | world brush convex hulls (3D-skybox brushes excluded) / displacement collision tris | text, Unreal cm |
| `.lights` | one line per WORLDLIGHTS source: `type origin dir rgb radius stopdot stopdot2 exponent style sky` (rgb at six decimals — the type-5 row is the map's whole ambient level) | text |
| `.sprites` | env_sprite coronas: `texpath pos w h rgb amt orient sky` | text, Unreal cm (sizes = `scale × texpx × INCH_TO_CM`) |
| `.spawn` | `info_player_start` origin + yaw | text, Unreal cm (yaw pre-negated) |
| `.env` | skybox flag/name, sky-face orientation convention (`skyconv`), and **two** fog sets — `fog*` from `worldspawn` (the world's) and `skyfog*` from `sky_camera` (the 3D-skybox pass's own, distances ×`scale` into world units). Colours are the authored value **/255, still gamma-encoded** — the consumer decodes with a plain 2.2 | text, Unreal cm |
| `.water` | per-material plane, normalmap, fogcolor/dist, reflecttint | text, Unreal cm (plane Z + fogdist) |
| `.cube` | color-grade LUT | Adobe .cube |
| `.decals` | infodecal projectors: `material centre normal s_dir t_dir hw hh` (one deferred UDecalComponent per line) | text, Unreal cm (dirs unit; extents cm) |
| `.ropes` | move_rope/keyframe_rope cables, chain-resolved to segments: `tex ax ay az bx by bz width_cm rest_cm nodes texscale flags` (one UCableComponent per line) | text, Unreal cm (width/rest cm; `rest_cm` is the RE'd simulated rest length and may be shorter than the span — a taut cable; `-` tex = decode miss) |
| `npc/*.eskm` | skeletal characters (mdl_skel → UE_mdl_skeletal) | ESKM container |

---

# Track A — world/rendering parity with VtMB

The system-by-system implementation targets for reproducing VtMB's own rendering and
movement, Unreal-native throughout:

| Unreal (C++) | Mechanism |
|---|---|
| `FElysiumContentPaths` | Content root at `$ELYSIUM_EXPORT_ROOT` (dev), one-line switch to packaged location later. |
| `FElysiumObjModel` | OBJ+MTL parser (albedo `map_Kd`, alpha-masked emissive `map_Ke`, envmask, bump, WVT, alpha/blend, semantic glass, and Source Refract map/amount). Extend `FElysiumMaterialDef` only when a remaining VtMB material channel gains a consumer. |
| `FElysiumTextureCache` | DDS (native DXT + mips) preferred, PNG fallback. Add: worker-thread prewarm batch (load is texture-bound), cubemap load (`UTextureCube` from six faces). |
| `FElysiumMaterialFactory` | MIDs off the master-material set below, parameters bound from `MaterialDef`. |
| `AElysiumMapActor` | adopts the baked level's `SM_World_*` chunk actors (one per 2048 cm cell, split into Nanite and non-Nanite buckets) by tag. The per-map owner of all Track B subsystems. |
| brush collision in map actor | `UBodySetup` + `FKConvexElem` per `.hulls` brush; runtime trimesh from `.dispcol`. Replaces render-trimesh collision as the primary walkable surface (keep trimesh for displacement-heavy maps). |
| `UElysiumLightRig` (component on map actor) | Point/spot/directional from `.lights`; lightstyle patterns ticked as intensity curves; texlight clustering. |
| billboard `UMaterialBillboardComponent`s or one Niagara system fed `.sprites` | additive glow sprites. |
| **Single Layer Water** material + Lumen/SSR reflections | no manual mirror camera. |
| **`UDecalComponent`** deferred projection from the `.decals` sidecar (`M_Decal`, `DBM_TRANSLUCENT`) | went straight to deferred: a GBuffer decal is lit like its host wall, Lumen indirect included. |
| `.cube` LUT | post-process Color Grading LUT (transient `UTexture` into per-map `FPostProcessSettings`) — native. |
| `.env` | sky material from six sky PNGs + a **per-primitive distance-fog term** in the surface masters (`ElysiumFog.h`); `UExponentialHeightFogComponent` keeps the volumetric layer — native. The distance fog cannot be an engine fog: the world and the 3D-skybox miniature carry two authored sets and share screen depth. |
| custom `UCharacterMovementComponent` override | port the Source `CGameMovement` math line-by-line — `docs/vtmb/source_movement.md` is the reference. Friction/accel/airaccel/StepMove constants verified against the decompile. |
| one baked `SM_*` per unique model, placed as `AStaticMeshActor`s | baked offline from `props/*.obj` with skins, collision and authored mass on the asset (`props/*.skins`, `props/*.phys`); the map actor adopts the placements by tag. |
| **modern Slate/UMG UI** (not a VGUI port) | Screen inventory, panel anatomy, hierarchy and iconography carry over from `.res`/`trackerscheme.res`; the runtime is a resolution-independent Slate/UMG stack with vector type. No 640×480 scale box, no bitmap `.fnt` atlas, no classic mode. `docs/project/remaster-direction.md` → axis 1; `docs/vtmb/m0_menu_build.md` is structural reference. |
| engine console commands now; Slate console only if it earns its keep | `elysium.*` commands cover current needs. |

**Success criterion for Track A**: slice acceptance for P7, `docs/project/roadmap.md`.

## Master materials (generated from tracked source)

`UMaterial` cannot be created at runtime; everything else can. The editor-only content
generators produce a small, game-agnostic set with parameter slots:

- `M_VtMB_World` (albedo + alpha-masked selfillum emissive) — grows into
  `M_World_Opaque` (bump, envmap mask + cube, WVT second layer + vertex-color blend). The
  `$selfillum` path: the exporter bakes the base texture's alpha-masked RGB into `*_ke.png`
  and writes `map_Ke`; the runtime binds it onto the `Emissive` texture parameter and turns
  `EmissiveScale` on (`elysium.EmissiveScale`, default 1.5; the param defaults to 0 so
  non-selfillum surfaces never glow). Authored by `pipeline/unreal/make_world_materials.py`.
- `M_World_Masked` (alphatest) and `M_World_Translucent` (generic authored alpha)
- `M_World_Glass` for lit reflective `LightmappedGeneric`/`VertexLitGeneric` glass: UE Thin
  Translucent + Surface Forward Shading, Pixel Normal Offset, exact authored alpha as surface
  coverage, and a derived uneven-glass normal only when no authored bump map exists
- `M_Refract` for Source `Refract` overlays with no required albedo: the converted signed
  DUDV/authored normal drives Pixel Normal Offset at `1 + $refractamount`, while the material
  contributes no colour or attenuation of its own
- `M_Water` (Single Layer Water)
- `M_Additive` (coronas / glow props)
- `M_Decal` (deferred decal domain)
- `M_Sky` (six-face skybox, unlit, two-sided, samples the `SkyCube` param by view
  direction; authored by `pipeline/unreal/make_sky_material.py`)
- UI brushes / materials for the modern UI stack (vector-type rendering is Slate's own; these
  cover panel treatments — grain, ink bleed, vignette — that carry the paper/blood language)

These encode shading logic, not game content. Their generator source is permanent; their
local `.uasset` products are ignored and regenerated by `uv run elysium export bundle policy`.

The MTL semantic order is Source Refract, semantic glass, then generic translucent. `refract` and
`refractmap` preserve the Source framebuffer-distortion contract; `glass 1` is the engine-neutral
classification emitted by the exporter for lit reflective glass. Neither semantic changes virtual
package names or the public runtime API.

---

# Track B — the game layer (Unreal leads)

Everything in this track consumes data that is **already exported** (`.ents` carries the
complete entity/I/O surface) but has no runtime consumer yet. Design targets come from the
decompile-backed docs in `docs/`. The concrete object model for B1/B2 (core types,
interaction flows, and the two-phase substrate→debug-layer build plan) is
`docs/architecture/engine-core.md`; the debug layer itself is `docs/architecture/debug-tooling.md`.

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
  `docs/vtmb/entity_visuals.md` holds.
- **Handler factory**: `TMap<FName /*classname*/, FEntityFactoryFn>`. Unhandled
  classnames log once and become inert records (still inspectable) — coverage grows
  classname-by-classname, driven by the tutorial's histogram.
- **Visibility subsystem**: `StartHidden` / `ScriptHide` / `ScriptUnhide` = whole-entity
  OFF switch (no collision, no think, undrawn) — decompiled semantics in
  `docs/vtmb/entity_io.md`. This is the single most-driven input (`ScriptUnhide` ×1,191
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
  (`Entity.__getattr__` in VtMB is datamap reflection, per `docs/vtmb/python_bridge.md`), so one
  table serves Hammer I/O, Python calls, and the debug inspector.
- **Do not route game-visible timing through `FTimerManager`** — the queue must be
  serializable for saves (VtMB saves its event queue and think contexts). Own the
  queue; it is ~100 lines.
- Implement `logic_relay` early — it is the biggest output source in the game (5,418
  outputs game-wide) and the tutorial's spine.
- Debug: an entity gizmo/inspector on the HUD — F1 pick shows an entity's record and
  lets you fire inputs by hand. This is the primary test harness for the whole track.

## B3. Triggers, +use, and movers

- **Triggers** (`trigger_multiple` ×44, `trigger_once`, `trigger_hurt`,
  `trigger_autosave`, `trigger_inventory_check`, `trigger_environmental_audio`):
  overlap-only convex components; pawn begin/end overlap → `OnTrigger`/`OnStartTouch`
  outputs through the queue, spawnflag filters respected.
- **+use / interaction verbs**: camera line-trace against the usable set (13 classnames
  carrying `use_icon`/`locked_icon` — full 72-entry icon enum in `docs/vtmb/entity_io.md`);
  `E` dispatches the `Use` input. HUD shows the use icon (pipeline addition: export the
  icon atlas). Doors, buttons, containers, and item pickups all ride this one verb.
- **Movers** (`func_door_rotating` ×29 in the tutorial, ×1,236 game-wide; `func_door`,
  `func_button`, `func_rotating`, `func_movelinear`, elevators): kinematic component
  movement driven in C++ (position interpolation per tick), full spawnflag bit tables
  and state machines already decompiled in `docs/vtmb/animation_and_movers.md` Part B — implement
  from that spec. Chaos sweeps handle blocking; door sounds arrive with audio.
- **Success criterion**: in `sp_tutorial_1`, the elevator call button works — button →
  `Unlock`/`Trigger` outputs → doors open → `thug_2` `ScriptUnhide` fires. That chain
  exercises registry, queue, movers, visibility, and +use in one room.

## B4. Level travel

`trigger_changelevel` (×14 in tutorial) + `info_landmark` (×12): on overlap, call
`UElysiumMapSubsystem::Travel(map, landmark)` — the landmark parameter finally does
its job. New position = old offset relative to the source landmark, applied at the
destination landmark (both maps share the landmark targetname). `point_teleport` and
scripted `ChangeMap` are the other two spawn paths (`docs/vtmb/level_transitions.md`).
Convert `Travel` to the async two-stage state machine in `docs/architecture/map-architecture.md`
(fade → unload → task-thread parse → spawn → fade in) when synchronous hitches start
to matter — not before.

## B5. Game models: props, physics, NPCs

- **Static props** (Track A): one baked `SM_*` per unique model, placed from `.props`.
- **Dynamic props** (`prop_dynamic` ×78): spawned from `.ents` `model` keyvalues.
  *Pipeline addition required*: export models referenced by entities (only
  GAME_LUMP static props are exported today). Skeletal ones ride the character-bake path.
- **Physics props** (`prop_physics` ×54, `phys_hinge` ×12): **Chaos rigid bodies** with the
  model's own authored `.phy` convex hulls and authored mass, constraints for hinges. The whole
  rigid-body lane — props, brush physboxes, the `phys_*` constraint family, character ragdolls,
  the player's object carry and every impulse producer — is one system:
  `docs/architecture/physics-architecture.md`.
- **NPCs**: spawn from `npc_*` / `npc_maker` entity data at their origins and stand baked
  native skeletal bodies off the `/ElysiumBaked` mount — the character bake is the only build
  of a character, and glTF stays an inspection product (`docs/architecture/animation-architecture.md`).
  IK stays deferred; the ragdoll is `docs/architecture/physics-architecture.md`'s.
- **AI later**: runtime-generated NavMesh (dynamic navmesh generation over the loaded
  world collision) replaces the `info_node` graph (×154 + patrol points) rather than
  reimplementing Source AI navigation; decision-making stays the substrate schedule kernel
  (`docs/architecture/gameplay-systems-architecture.md` owns that owner call). `scripted_sequence`
  (×104, plus 4 `aiscripted_sequence`) runs ahead of real AI: the beat's animation, its
  `OnBeginSequence`/`OnEndSequence` gate, and scripted Walk travel through the NPC motor at the
  selected clip's decoded ground speed.

## B6. The Python connection (scripting host)

VtMB embeds CPython 2.1; `vampire.dll` owns the whole API. `docs/vtmb/python_bridge.md` has the
full RE. **There is no big API to port** — entity method calls are datamap
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
`$ELYSIUM_EXPORT_ROOT/scripts/` and `$ELYSIUM_EXPORT_ROOT/dlg/` (they are plain text in the user's install).

## B7. Main game engine (persistent layer)

Subsystems on `UElysiumGameInstance` (persistent across map travel), map-scoped state
on the map actor:

- `UElysiumMapSubsystem` *(exists)* — map lifecycle, travel, landmark placement.
- `UElysiumGameState` — the `G` dict (~900 story flags), quest states, player RPG
  sheet (attributes/abilities/disciplines from `vdata/system/*.txt`, transcribed in
  `docs/vtmb/game_runtime.md`).
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

Fully RE'd in `docs/vtmb/audio_pipeline.md`, no runtime exists yet:

- MS-ADPCM WAV (~92% of SFX): decode in C++ (the codec is simple) into
  `USoundWaveProcedural`/PCM — no offline transcode step needed.
- MP3 dialogue/music: runtime decode likewise.
- `ambient_generic` (×76 in tutorial) from entity data; **SoundScheme** system
  (`sound/schemes/*.txt`) for ambience.
- Unreal wins: attenuation curves, reverb submixes, concurrency — native; MetaSounds
  optional later.

## B9. Dialogue and beyond

`.dlg` format (13-field CRLF Latin-1) is column-verified in `docs/vtmb/game_runtime.md`; no
parser exists anywhere yet. Parser + branch machine + conversation UI on the modern UI
foundation + dlgexpr (B6.2) + audio-by-path. The **dialogue content is reproduced verbatim**
(lines, conditions, branch structure, the 89 malformed snippets' error-to-false behaviour);
only its presentation modernizes — legible type, reflowing line lists, subtitles, and a layout
that is not bound to 640×480. Chargen, combat, and the full RPG sheet come after the tutorial
plays end-to-end.

---

## The ownership register — what Unreal owns, what this repo reproduces

Which half builds a system is adjudicated before the work by the **Ownership test** in
`docs/project/remaster-direction.md`: a system is reproduced here only when authored content or a
game rule names it; everything the world merely needs in order to work is Unreal's. Troika's
source is the RE oracle for rules, orders, thresholds and data — never an implementation to
port. This register is the closed set on both sides. **A reproduction of a Source subsystem that
appears in neither column is a defect, not a tolerance** — the same enforcement shape as the
event layer's closed divergence set (`docs/architecture/gameplay-systems-architecture.md` K12).

**Unreal owns the mechanism** (the authored values ride it as data):

| Source subsystem | Unreal mechanism | What stays ours |
|---|---|---|
| Lightmaps + static visibility (PVS) | HWRT Lumen GI, MegaLights, VSM; render visibility behind the `IsNpcBodyVisible` query, divergence enumerated at its declaration | the `.lights` values and calibration; every rule that consults visibility |
| `info_node` graph navigation | Recast/Detour navmesh and `ACharacter` motors behind `IElysiumNpcMotor` | authored routes, marks and cover/hint *entities*; gaits, speeds, arrival and failure policy |
| VPhysics | Chaos rigid bodies + constraints | the `.phy` convex hulls and authored mass |
| Rope simulation (`CRopeKeyframe`) | one `UCableComponent` per segment | the RE'd endpoints, node counts and rest length (the sag) as data |
| Collision and trace queries | engine sweeps, overlap components, dedicated trace channels | authored hull shapes; every eligibility rule applied on the answer |
| Studio animation runtime | `UAnimSequence`/blend spaces/anim graph over the baked native pose | clip selection rules, the activity catalog, the bake that resolves VtMB storage |
| `$envmap` reflection composite | the PBR/Lumen specular response (`ElysiumReflections.h`) | which surfaces reflect, mask and tint — the authored intent |
| Audio playback: attenuation, spatialization, submix, concurrency | Unreal audio, `FSoundAttenuationSettings` built from authored radii | MS-ADPCM/MP3 **decode** and the SoundScheme rules — no engine reader exists for the bytes |
| Decals, water, fog, LUT, sprites, particles | `UDecalComponent`, Single Layer Water, native fog/LUT, Niagara | the authored placements and parameters |

**This repo reproduces** (a game rule or authored content names it):

| Reproduction | The observable that requires it |
|---|---|
| The entity substrate, thinks and the I/O queue | equal-time order, pending records and `times` are authored-visible and saved |
| Player movement math, call order and the box hull | step climb, accel and friction are the feel; pure half in `ElysiumMoveSolve.h`, engine half runs retail's order over Unreal's traces |
| The legacy camera evaluator and shot stack | recovered feel rules, kept A/B-able against the modern rig |
| Dice, sheet math and check policy | `DiceRolls.txt` and the rulebook are authored data |
| The NPC schedule kernel, senses policy and relationships | schedule names are script-visible API; decisions serialize and run headless |
| Embedded CPython 2.7 and the three legacy API tiers | VtMB's own scripts and bindings are content, not engine |
| Format decode (BSP, MDL, TTH/TTZ, VPK, WAV/MP3, `.dlg`, `.vcd`, `.res`) | no engine reader exists for the bytes |
| Mover state machines and timings | authored-visible timing, outputs and spawnflag semantics |
| The additive combine order (`FAnimNode_ElysiumPostAdditive`, `vampire.dll 0x100c12b0`) | which side the `_delta` lands on decides the drawn pose, and the answer depends on the pose it is accumulated onto rather than on the clip — so no bake conversion is exact over the fan cells and gaits one delta rides |

Standing engine facts that shape the build: the offline bake exists to give every surface real
DDC-fitted Lumen surface-cache cards and distance fields — the thing a runtime-built mesh can
never have. Nanite is on for every baked mesh that can take it (311 of 339 on the tutorial; the
28 exceptions are translucent/additive surfaces Nanite does not support) — the value is the
surface-cache/distance-field win, not raw triangle count at ~20k world tris. Parsing stays on
the C++ hot path with the `.emc` cook-cache; if load time ever matters again, extend the cache.

## Pipeline & tooling

The decode/export pipeline lives in this repo's `pipeline/`: format readers under
`pipeline/src/elysium_pipeline/formats/`, Unreal-native exporters under
`pipeline/src/elysium_pipeline/exporters/`, enhancement passes under
`pipeline/src/elysium_pipeline/enhancement/`, validation under
`pipeline/src/elysium_pipeline/validation/`, and editor-only generators under
`pipeline/unreal/`. Reusable probes and reverse-engineering instruments live under
`research/tooling/`. `pipeline/CLAUDE.md` owns the folder map, with the root `pyproject.toml`
for deps (`Pillow`, `numpy` core; `torch`, `torchvision`, `spandrel`, `einops`,
`safetensors` optional, ESRGAN upscalers only). It is engine-neutral Python; coordinate-bearing
products are Unreal-native or standard self-describing glTF (Coordinate conventions, above).
`$ELYSIUM_EXPORT_ROOT/`, Python environments, caches, models, and the
entire `$ELYSIUM_WORK_ROOT/research/ghidra/` + `$ELYSIUM_WORK_ROOT/research/reference-source/` RE trees remain outside Git. `FElysiumContentPaths::Root()` resolves
`-ElysiumContentRoot`, then `ELYSIUM_EXPORT_ROOT`, then `$ELYSIUM_WORK_ROOT/exports`; it has no
repository-relative corpus fallback. The reverse-engineering
toolchain (Crowbar/TemplePlus/VAMPTools/source-engine) and the `$ELYSIUM_WORK_ROOT/research/ghidra/` workspace are
local-only, read-only RE references (never committed).

The retail animation instrument is deliberately private and exact-build. Its
first intermediate result is an unattended final-matrix corpus for the union of
raw sequence and animation descriptors resolved by the installed player models:
prove one clean sequence, automate the same reset/settle/play/capture recipe for
every addressable identity, and retain exact evidence for duplicate-name,
blend-cell, parameterized, or otherwise unaddressable rows. Each clean capture
is matched to its patch-first source bytes and exported animation, then compared
per frame and bone with the engine-neutral decoder. Held-out captures test rules
suggested by mismatch clusters; correlation alone does not promote a hypothesis.
Only then does a focused experiment add the bounded raw registers, stack bytes,
pointers, and memory spans needed at the first unexplained resource, evaluation,
simulation, deformation, or render boundary. Unknown bytes remain available to offline analyzers; decoded fields
are hypotheses, not a durable capture contract. Writer and reader evolve
together. Offline indexes are disposable and rebuildable. Only measured
callback cost, memory pressure, writer backlog, trace volume, startup cost, or
query time justifies capture/storage optimization. The instrument is retired to closure; the
banked corpus is the standing oracle, and a renewed capture is a scoped owner call
(`docs/vtmb/vtmb-animation-reverse-engineering.md` → "Programme method").

New sidecar formats and decoder fixes land in `pipeline/`. The pipeline backlog
(entity-model export, script/`.dlg` copies, use-icon atlas, NPC batch export +
include-model resolution, sound-scheme/`vdata` copies, texlight merge, space fixes) is
tracked as **PL1–PL7 in `docs/project/roadmap.md`**. Everything else the runtime needs is already
exported.

## Milestones

The M-numbers below are vocabulary for what each milestone means. Project sequencing, task
mapping, and roll-up status live in `docs/project/roadmap.md`; its traceability table resolves
these names to phases.

Vertical slice: **play `sp_tutorial_1` start to finish, then walk into
`sm_pawnshop_1`.**

- **M0 — first pixels**: world + skybox rendering, DDS/PNG textures,
  trimesh collision, `.emc` cache, free-fly pawn, map switching, debug HUD.
- **M1 — world parity**: `.hulls`/`.dispcol` collision, Source movement component,
  light rig + lightstyles, `.env` sky/fog, `.cube` LUT, master-material set
  (alpha modes, WVT, bump/envmap), texture prewarm off the game thread.
- **M2 — dressing parity**: props via ISM (+ convex collision), decals, Single Layer
  Water, coronas, checked against the `uv run elysium debug shots` baseline on tutorial + hub maps.
- **M3 — entity backbone**: entity world + registry, event queue + input dispatch,
  visibility subsystem, triggers, movers (doors/buttons/elevator), +use,
  `logic_relay`/`math_counter`/`logic_timer`, changelevel + landmark travel, entity
  debug inspector. *Success: the tutorial elevator chain works; walking out of the
  tutorial loads the pawnshop at the landmark.*
- **M4 — scripting + audio foundation**: expression evaluator + `G` + native bindings
  (field-6 calls, `logic_pythoncheck`), MS-ADPCM/MP3 decode, `ambient_generic`,
  sound schemes, door/button sounds.
- **M5 — UI + characters**: the modern UI foundation (design system, vector type,
  resolution-independent Slate/UMG stack) carrying the main/pause menus and New Game flow, the
  HUD and sign panels re-skinned onto it, accessibility/options backing, NPCs spawned from
  entity data off the baked mount, `scripted_sequence` minimal handler, dynamic props.
- **M6 — dialogue + game state**: `.dlg` parser + UI + dlgexpr, level-script execution,
  quest/XP basics, save/load. *Success: the tutorial is completable as in retail.*

Ordering rationale: M3 lands before menu/NPC work because every downstream system
(dialogue, AI, quests, saves) sits on the entity/I-O/scripting spine, and it is the
one part built from scratch with no prior implementation to lean on — de-risk it earliest.

## Repository facts

Module, plugins, dependencies, source layout, config, content root, and the generated local package
list are documented next to the code they describe:

- `/Source/ElysiumUE/CLAUDE.md` — the C++ runtime (module + deps, key types, entity
  substrate, scripting hosts, audio, debug layer, console commands, `Config/` facts).
- `/Content/CLAUDE.md` — licensed source fonts, original project-authored packages, and generated
  local packages.
- `/pipeline/CLAUDE.md` — the VtMB input formats and their decoders.
- The repo-root `CLAUDE.md` — orientation, the load-bearing rules, build & run.

Map lifecycle design: `docs/architecture/map-architecture.md`.
