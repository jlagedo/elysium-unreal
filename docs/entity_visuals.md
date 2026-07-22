# Entity visuals — rendering plan & I/O-ready substrate

> **Reference — Godot prototype, not this repo's live state.** This document (its `game/src/*.cs`
> code, `CoronaField`/`LightRig`, `.gdshader`, Godot capture commands, and the R0–R6 "shipped/
> done" statuses) describes the read-only Godot prototype at `E:\dev\elysium`. Elysium-Unreal has
> only **M0** built so far — see `docs/rebuild-strategy.md` (Track B) for the Unreal-native entity
> substrate plan. The **engine-neutral facts** (entity/`.ents` schema, the additive/seams design,
> the fact base in §3) carry over; the C#/Godot specifics are porting reference. Bare `CLAUDE.md`
> and `docs/archive/…` paths mentioned below are in the Godot repo.

Governing design doc for rendering the map's **entity-placed visuals** (glow sprites,
dynamic-model props, ropes, particles, signs) that the pipeline currently drops. Scope
is **visuals only** — no gameplay behavior (triggers, logic, Python, NPCs). But the code
is structured so the deferred behavior wires in *additively* later, without reworking the
visual layer.

This is a **plan/handoff doc**: it is detailed enough for another agent to pick up a phase
and execute it. Present-tense statements describe current facts; the "Phases" section is
forward-looking work.

> **Status (2026-07).** The **real-time lighting refactor shipped after this plan was
> drafted** (commits `b8fce61`/`60a71b3`) and delivered R1's coronas and R6's lighting
> substrate the **sidecar way** (`CoronaField` + `LightRig`), *not* through the
> `EntityWorld` handler layer proposed below. Net state: **R1 coronas — done**
> (refinements open); **R0 / R2 / R3 / R4 / R5 — not started**; **R6 — still blocked on
> the unbuilt entity-I/O layer.** The §5 substrate remains a valid design for
> *addressable* entities (dynamic props, runtime I/O), but is no longer a hard
> prerequisite for pure visuals: the project has shown it will ship a visual through a
> typed sidecar (as with `.sprites`) whenever no I/O identity is needed. Line/anchor
> references below are refreshed to the current tree.

---

## 1. Goal & principle

Bring the map's night look up to Source: lamp coronas and volumetric light beams, the
street lights / cop cars / other placed models, overhead cables, steam/smoke, and signage.

**Principle — thin entity substrate now, additive behavior later.** Every visual entity is
spawned through a small `EntityWorld` + handler layer. We implement only the *spawn-visual*
half of each handler now. The *behavior* half (inputs, output firing, movement, runtime
show/hide) is left as **declared-but-empty seams** plus **already-parsed data**, so wiring
it later is fill-in work, not a rewrite.

Non-goals (deferred, see §6): the I/O event queue, Source I/O dispatch, Python evaluation,
runtime state changes, doors moving, NPCs/AI, cameras/cinematics.

---

## 2. Current state (what already exists — do not redo)

Done earlier in the material-fidelity pass (relevant because lamps are the motivating case):

- **`$additive` prop materials** are handled end-to-end: `tools/vmt.py` parses `$additive`;
  `tools/mdl.py::_resolve_material` carries it; `game/src/Assets/ObjModel.cs`
  `MaterialDef.Additive` + the `additive 1` MTL flag; `MaterialFactory.BuildProp`
  (`game/src/Assets/MaterialFactory.cs:36`) has an additive branch (`BlendMode.Add`, no depth
  write, no vertex tint).
- **VMT comment stripping**: `tools/vmt.py::_decomment` strips `//` line comments (quote-aware)
  before key matching, so a fully-commented key (`//"$selfillum" "1"`) is no longer read.
- **Prop emission scales with the `emis` knob**: `BuildProp` draws a masked/`$additive`
  overlay at `AlbedoColor = EmisEnergy` (the `emis` tune). There is **no separate `Exposure`
  multiplier any more** — it was removed in the gamma-space colour match (`docs/color_gamma.md`).
  At the default 1.0 the overlay draws at its authored brightness and hot bulb texels clip
  toward white on the LDR output (`MakeEnvironment`, `game/src/Core/GameScene.cs:451`, keeps
  glow on with `GlowHdrThreshold = 1.0`).
- All 7 exported maps were re-exported after those changes.

**The real-time lighting refactor (post-draft) already shipped two of this doc's targets** —
via the established per-feature **sidecar** pattern, not the `EntityWorld` substrate:

- **`env_sprite` coronas are DONE.** `write_sprites` (`tools/bsp_to_scene.py:128`) decodes each
  `env_sprite`'s Sprite-VMT `$basetexture` → `tex/spr_*.png` and writes a `<map>.sprites`
  sidecar; `CoronaField` (`game/src/World/CoronaField.cs`) spawns one additive, unshaded,
  camera-facing `QuadMesh` billboard per sprite (depth-tested, no depth write), tinted
  `rendercolor × renderamt × ELYSIUM_RIG_CORONA`, orient 0/1. This **is** R1's corona
  deliverable — see R1 for what it does *not* yet do (StartOn filter, occlusion fade, beams,
  flashers).
- **The baked lightmap is gone.** Lighting is the real-time `LightRig` (one Godot light per
  WORLDLIGHTS source) + a flat `Environment` ambient fill (`docs/lighting.md`). There is **no
  lightmap atlas, no UV2, no per-location baked tint**; props and world share the lit
  `shaded.gdshader`. R6's switchable states pin against this rig.

Entity data is **fully exported** but **only drawn as debug gizmos**:

- `tools/bsp_to_scene.py::write_entities` (`:230`) writes `<map>.ents` (schema in §4).
- `game/src/World/WorldLoader.cs` reads `.ents` in exactly one place —
  `BuildEntGizmos` (`:366`), reached via `SetEntGizmos` (`:240`) — building `MultiMesh` boxes +
  `Label3D`s + a read-only inspector (`EntityDump`, `:323`; `PickEntity`, `:302`). **No
  functional entity behavior is spawned** (no `Area3D`, no lights, no sprites, no I/O).
  (`CoronaField` above is a *separate* sidecar path, not this gizmo layer.)
- Static props come from a **different** path: GAME_LUMP `sprp` → `write_props`
  (`bsp_to_scene.py:366`) → `<map>.props` sidecar → `LoadProps` (`WorldLoader.cs:457`).
  These are engine static props, **not** entities, and stay as-is.

---

## 3. Fact base (grounds every decision; measured across the 7 exported maps)

| Fact | Number | Consequence |
|---|---|---|
| Distinct entity classnames | 112 over 10,837 entities | Handler registry, not 112 special cases |
| `.ents` already carries identity + `keys{}` + `outputs[]` (7-field incl. Python) + `hulls` + `start_hidden` | — | **Future I/O needs zero export rework** |
| Runtime visual toggles (`Hide/Show/TurnOn/TurnOff`) | ~250 outputs | All `logic_relay`/`timer`/`trigger`/`choreo` sourced → change scene *during play*, not at load |
| `logic_auto` (only spawn-time firer) targeting a visual entity | **0 direct** (5 relays, 33 py at spawn) | Initial frame ≈ spawn flags; spawn-chains are a tiny deferred subset |
| `env_sprite` StartOn (`spawnflags` bit0) | 1113 on / 135 off / 3 flag=2 | Must filter by spawn flag; `start_hidden` is 0 on all sprites |
| `env_sprite` materials | glowa 900, volumelighta 96, candle 42, coplights 41, coplightsb 37, volumelighta_proxyfade 32 | Coronas **and** volumetric beams **and** candle flames **and** cop flashers |
| `prop_dynamic`/`prop_physics`/`_ornament` | 425, all `.mdl`; 242 named; 116 `start_hidden` | Reuse prop decoder; skip hidden; keep identity for future I/O |
| `keyframe_rope`/`move_rope` | 562; keys `NextKey`,`Subdiv`,`Width`,`Slack`,`RopeMaterial`(`cable/cable`),`TextureScale`,`Type` | Catenary computable from chain + slack |
| `env_particle` | 125; keys `particle_definition`,`active`,`attach_type`,`bounds`,`ramp_scale` | Needs particle-system resolution — investigate first |
| Switchable lights (have `targetname`) | 8 of 2841 | Lighting is real-time (`LightRig`); switchable styles 32+ are held ON until entity I/O lands (see R6) |

Example env_sprite (pierlight street lamp, sp_tutorial_1): `model materials/sprites/glowa.vmt`,
`rendermode 5` (additive), `renderamt 40`, `rendercolor 255 255 255`, `scale .75`,
`disableshadows 1`, `spawnflags 1`. It sits ~3u from a `light_spot` `_light 247 207 115 3500`
(warm), which the real-time `LightRig` now spawns as a `SpotLight3D` (no baked lightmap).

Source `rendermode` values that matter here: **3 = `kRenderGlow`** (camera-facing additive
corona, occlusion-**faded**, ~constant apparent size), **5 = `kRenderTransAdd`** (plain
additive, world-scaled). Both blend additive; glow adds the occlusion fade.

---

## 4. Data contracts

### 4.1 `.ents` (already produced — the entity source of truth)

`tools/bsp_to_scene.py::write_entities` (`:230`) emits `{"map": <base>, "entities": [ … ]}`.
Per entity (schema at `:261–293`):

- Always: `classname`, `targetname`, `origin` (**Godot metres**, `source_to_godot` applied),
  `start_hidden` (bool), `keys` (every remaining keyvalue verbatim, last-wins — so
  `spawnflags`, `model`, `rendermode`, `rendercolor`, `renderamt`, `scale`, `angles`,
  `particle_definition`, `NextKey`, `Slack`, `use_icon`, … all live here).
- Brush entities only (`model "*N"`): `model` (int index), `hulls` (list of flat
  `[x,y,z, …]` **entity-local Godot-metre** convex hulls), `contents`, `blocks_player`.
- If any `On*/Out*` key: `outputs[]`, each `{name, target, input, param, delay, times,
  python}` (`_split_output`, `:212`). Retained now, unused until the I/O phase.

`classname`/`targetname`/`origin` are popped from `keys`; `StartHidden` stays in `keys` **and**
is surfaced as `start_hidden`. **Do not change this schema** — the deferred I/O reads it as-is.

### 4.2 Asset decodes (new export output — assets only, keyed by the entity's own references)

Exporter passes decode the heavy assets an entity references; the entity record itself stays
in `.ents`. Reuse the existing decode helpers:

- Sprite materials → `tex/spr_<name>.png` (via `tools/vmt.py::parse` → `$basetexture` →
  `tools/tex_to_png.py::decode`, same TTH/TTZ path as world textures) — **shipped** by
  `write_sprites`; the `.sprites` sidecar names the png per sprite.
- Dynamic-prop `.mdl` → `props/<safename>.obj` + `props/tex/` (reuse `tools/mdl.py` exactly as
  `write_props` does; dedupe by model).
- Rope material (`cable/cable`) → `tex/<name>.png`.

No new sidecar is required for entity records (the runtime reads `.ents`); a phase MAY emit a
small **derived-geometry** sidecar where runtime computation is undesirable (e.g. ropes), but
it must be keyed back to the entity, never a replacement for it.

### 4.3 Coordinate conventions (reference)

Source→Godot: `(sx,sy,sz) → (sx, sz, -sy) * 0.0254`. Angles (`pitch yaw roll`) →
`basis = M·AngleMatrix(pitch,yaw,roll)·M⁻¹` (see `WorldLoader.LoadProps`, `:457`, and the
prop transform there). `.ents` `origin` is **already** in Godot metres; `keys.origin`/`angles`
are raw Source.

---

## 5. Architecture — the substrate (build in `game/src/Entities/`)

```
Entity                         // one per .ents record
  string   Classname, Targetname
  Vector3  Origin              // Godot metres
  Dictionary<string,string> Keys
  List<Output> Outputs         // parsed, retained, UNUSED now
  bool     StartHidden
  Node3D   Visual              // what the handler built (may be null)
  int      MultiMeshIndex      // for pooled visuals (dynamic props), else -1
  Dictionary<string,Action<EntEvent>> Inputs   // RESERVED, empty now

EntityWorld                    // owned by WorldLoader; single source of truth
  void Load(string entsPath)                    // parse → Entity[], index by Targetname
  Dictionary<string,List<Entity>> ByTarget
  EntityRegistry Registry                       // classname → IEntityHandler
  void SpawnAll(ctx)                            // for each entity: Registry.Get(cls)?.Spawn(e, ctx)
  // RESERVED (do NOT build now): EventQueue, FireOutput(), Tick(), ResolveTarget()

interface IEntityHandler
  void Spawn(Entity e, EntityCtx ctx)           // IMPLEMENT now — builds the visual
  void RegisterInputs(Entity e) {}              // declared, empty — the deferred seam

EntityRegistry                 // classname → handler; unknown classname → inert (data only)
```

- `EntityWorld` is created and driven by `WorldLoader` alongside `LoadProps`. It owns a child
  `Node3D "Entities"` under the world root.
- **Repoint the gizmo/inspector to `EntityWorld`** so there is one entity source. `BuildEntGizmos`
  (`WorldLoader.cs:366`), `PickEntity` (`:302`), `EntityDump` (`:323`) should read the parsed
  `Entity[]` from `EntityWorld` instead of re-parsing JSON. (The gizmo/inspector then reflects
  live spawned state for free.)
- **Spawn-flag filter (mandatory, all handlers):** a handler skips an entity that spawns
  invisible — `StartHidden == true`, or (for `env_sprite`) the StartOn `spawnflags` bit0 is
  clear. This is the "matches what the player sees on load" rule from §3.

`EntityCtx` carries what handlers need: the parent node, the map's texture cache, the main
camera (for billboards), and the content dir. (There is **no** lightmap atlas / per-location
tint sampler — the world is lit in real time by the `LightRig`, so a spawned visual is either
lit by the same rig on `shaded.gdshader` or is self-lit/additive, like the coronas.)

---

## 6. The seams — how deferred behavior plugs in (proof it's additive)

| Deferred capability | Plugs into | Already present now |
|---|---|---|
| Runtime input dispatch | `EntityWorld.EventQueue` + `handler.RegisterInputs` | `Entity.Inputs` map (empty) |
| Output firing / wires | `EntityWorld.FireOutput` reads `Entity.Outputs` | outputs already parsed |
| ScriptHide / ScriptUnhide / Kill | handlers expose `SetVisible`/`SetSolid`; base inputs call them | `StartHidden`, `Entity.Visual`, `MultiMeshIndex` |
| Doors / movers | handler swaps its `MeshInstance`→`AnimatableBody3D`; adds `Open/Close` | brush `hulls` in `.ents` |
| Python (output field 6, `logic_pythoncheck`) | evaluator resolves names against the **same** `Entity.Inputs` map (per `docs/python_bridge.md` — inputs and Python attrs are one namespace) | `Outputs[].python` retained |
| Switchable lights (styles 32+) / `start_hidden` coronas | light handler toggles `LightRig` energy/style; corona handler shows the sprite | `LightRig` drives every light (styles 1–11 animated via the shared `Lightstyles` helper); `CoronaField` already skips `start_hidden` sprites — see R6 |
| Spawn-time chains (logic_auto → relay) | a one-shot spawn pass over `logic_auto` outputs | outputs parsed |

Every row is fill-in on existing structure. The `Spawn` visual path never changes.

---

## 7. Phases of execution

Each phase is independently shippable and verifiable. Order is by visual impact. **R0 (the
substrate) is no longer a hard blocker for pure visuals** — R1's coronas already shipped through
a `.sprites` sidecar without it, and R3/R5 could too. R0 is required only for *addressable*
entities: R2 dynamic props that want runtime identity, and R6's I/O. R6 (switchable lighting) is
the item carried over from the completed lighting refactor and stays blocked on R0's I/O layer.
Remaining highest-impact visual work is now **R2** (dynamic props) — the largest missing content.

Common verification loop (all phases): headless capture and compare against the running game.
```
GODOT="tools/godot/Godot_v4.7.1-stable_mono_win64/Godot_v4.7.1-stable_mono_win64.exe"
"$GODOT" --path game --resolution 1600x900 -- --capture out.png --map sp_tutorial_1 \
  --eye X,Y,Z --look X,Y,Z          # eye/look are Godot-space metres
```
Re-export a map after any exporter change: `cd tools && python bsp_to_scene.py <map>`
(**run from `tools/`** so `out/` resolves to `tools/out/`, not a stray repo-root `out/`).

---

### R0 — Entity substrate + repoint gizmos  (blocks R1–R5)

**Objective:** the `Entities/` layer in §5 exists and drives spawning; gizmo/inspector read
from it. No handlers yet beyond a no-op default (unknown classname → inert).

**Runtime work** (`game/src/Entities/`):
- `Entity.cs`, `Output.cs`, `EntEvent.cs` (record types).
- `EntityWorld.cs`: JSON load (reuse the parse shape from `BuildEntGizmos`, `WorldLoader.cs:366`),
  `ByTarget` index, `EntityRegistry`, `SpawnAll`.
- `IEntityHandler.cs`, `EntityRegistry.cs`, `EntityCtx.cs`.
- Wire into `WorldLoader`: construct `EntityWorld`, `Load` the `.ents`, `SpawnAll` after the
  world mesh + props build. Refactor `BuildEntGizmos`/`PickEntity`/`EntityDump` to consume
  `EntityWorld.Entities` instead of re-parsing.

**Acceptance:** map still loads; gizmos/inspector behave exactly as before but now sourced from
`EntityWorld`; no visual regression; `--capture` unchanged vs a pre-R0 capture.

---

### R1 — `env_sprite` (coronas ✅ done · StartOn filter / occlusion fade / beams / flashers — open)

**Shipped (the corona path).** Additive billboards at every non-`start_hidden` `env_sprite`
already draw, via the sidecar pattern (not the substrate): `write_sprites`
(`tools/bsp_to_scene.py:128`) decodes each sprite's Sprite-VMT `$basetexture` → `tex/spr_*.png`
and writes `<map>.sprites`; `CoronaField` (`game/src/World/CoronaField.cs`) spawns one additive,
unshaded, camera-facing `QuadMesh` per sprite — `BlendMode.Add`, depth-tested (walls occlude),
no depth write (a glow occludes nothing), tinted `rendercolor × renderamt × ELYSIUM_RIG_CORONA`,
world size `scale × textureSize`, orient 0 (`vp_parallel`) / 1 (`parallel_upright`). This covers
`glowa` (900), `candle` (42), `coplights*` (78) and `volumelighta*` (128) — but all as **flat,
static** billboards.

**Open refinements (not yet done):**
- **StartOn spawnflags filter.** `write_sprites` skips only `start_hidden` sprites; it does
  **not** filter the 135 StartOff sprites (`spawnflags` bit0 clear), so they currently draw. Add
  the filter — and surface those 135 to R6 so entity I/O can switch them on.
- **rendermode 3 (`kRenderGlow`) occlusion fade.** Coronas are hard depth-tested, not faded. A
  `kRenderGlow` sprite should fade its alpha by the unoccluded fraction (physics raycast or
  `DEPTH_TEXTURE` sample) and hold ~constant apparent size. `rendermode 5` (`kRenderTransAdd`,
  plain additive, world-scaled) is what ships today for all of them.
- **Volumetric beams.** `volumelighta*` are light **shafts** — elongated, not round halos; today
  they draw as the same flat billboard. Give them beam geometry/anisotropy.
- **Cop flashers + candle animation.** `coplights*` cycle frames/colour (`framerate` key);
  `candle` flickers. Today both are static.

**Architecture decision.** These refinements can extend `CoronaField` in place (the shipped
path) **or** move `env_sprite` under the R0 substrate. For visuals alone, extending
`CoronaField` is the smaller change; the substrate only earns its keep once these sprites need
runtime show/hide (R6) — the one thing the sidecar path can't do. Default: refine `CoronaField`
now; migrate only when R6 forces addressability.

**Acceptance:** StartOff sprites (135) absent; `kRenderGlow` coronas fade behind geometry and
hold apparent size; `volumelighta*` read as shafts; `coplights*`/`candle` animate — each matched
capture-vs-game (e.g. the pierlight, Godot `--eye 3.8,-1.0,4.3 --look 7.0,4.94,5.70`).

---

### R2 — `prop_dynamic` / `prop_physics` (missing placed models)

**Objective:** draw the 425 dynamic-model props (StreetLight, CopCar, …), reusing the prop
pipeline, as **addressable** entities.

**Export:** extend `write_props` (or a sibling pass) to also gather `prop_dynamic`,
`prop_physics`, `prop_dynamic_ornament` entities — each has `model` (`.mdl`), `keys.origin`,
`keys.angles`. Decode each unique model once (shared `tex_cache`) exactly as static props do.
Emit their instances so the runtime can build per-model `MultiMesh` (reuse the `.props` line
format, or a parallel `<map>.dynprops` — but each line must retain the entity's `targetname`
so identity survives). Skip `start_hidden` (116). Props are lit at runtime by the LightRig,
so no per-prop tint is sampled (same as static props).

**Runtime** (`EntityWorld` handler + reuse `LoadProps`/`BuildPropModel`, `WorldLoader.cs:457/538`):
- Group by model, one `MultiMesh` per model (as `LoadProps`), per-instance transform, lit by the
  LightRig. **Record each entity's `MultiMeshIndex`** on its `Entity` so a future `SetVisible`/`Move`
  input can address the instance (hide = zero-scale that instance; move = set its transform).
- Collision optional for visuals; if built, reuse the convex hull path (`prop_physics` solid).

**Acceptance:** street lights, cop cars, and other dynamic props appear at correct positions;
count logged; hidden props absent; no double-draw with GAME_LUMP static props.

---

### R3 — ropes / cables (`keyframe_rope` / `move_rope`)

**Objective:** the overhead wires — 562 catenary cables.

**Export:** resolve rope chains (each node's `NextKey` → the `targetname` of the next node;
`keyframe_rope` is a start node, `move_rope` a mid/end node). For each segment sample a catenary
between the two node origins using `Slack` (sag) and `Subdiv` (segment count); decode
`RopeMaterial` (`cable/cable`) → `tex/`. Write derived geometry to `<map>.ropes` (polyline
points in Godot metres + `Width` + `TextureScale` + material), keyed to the start entity.

**Runtime** (handler): build a tube or 2-quad billboard strip per rope from the polyline, width
`Width`, tiling by `TextureScale`, unshaded/lit as appropriate; the `cable/cable` material is
opaque. `move_rope` motion (`MoveSpeed`) is behavior → static for now (render at rest).

**Acceptance:** cables hang between poles/buildings with correct sag; material tiles along length.

---

### R4 — particles (`env_particle`, `func_particle`, `params_particle`, `env_steam`)

**Investigate first** (blocker): resolve what `particle_definition` names. VtMB is early Source;
determine whether these are the Troika KeyValues particle scripts (`particles/*.txt`, already
ported in `game/src/Ui/Vgui/VguiParticles.cs`) or Source `.pcf` systems. If the former, reuse
`VguiParticles` at world scale; if the latter, scope a minimal reader.

**Objective:** steam/smoke/fire at emitter origins, honoring `active` (start state) and
`attach_type`/`bounds`.

**Export:** emitter list (origin, `particle_definition`, `bounds`, `ramp_scale`) + decode the
referenced particle assets/sprites. **Runtime:** spawn CPU/GPU emitters per the resolved system.
Lowest visual priority; may land last.

**Acceptance:** visible steam/smoke at known emitters (e.g. street steam), roughly matching Source.

---

### R5 — signs (`game_sign` / `prop_sign`)

**Objective:** signage. `game_sign` (72) / `prop_sign` (38) — textured quads/decals.
**Export:** resolve the sign material/texture + placement. **Runtime:** a textured quad (or reuse
the decal mesh path). Small, cosmetic; last.

---

### R6 — switchable lighting via entity I/O  (carried over from the lighting refactor)

**Objective:** let entity I/O drive the two lighting states the real-time rig currently pins
open. This is the only unfinished item from the completed baked→real-time lighting refactor
(archived at `docs/archive/lighting_refactor.md`, Phase 8 — **Godot repo**); it was always blocked on the same
unported entity-I/O layer this doc's substrate (R0) provides, which is why it lives here now,
not in the lighting layer.

**Current pinned states (both correct for a load with no I/O, wrong once things toggle):**
- **Switchable worldlight styles (32+)** are held **ON**. `LightRig` (`game/src/World/LightRig.cs`)
  spawns one Godot light per WORLDLIGHTS (lump 15) source and drives animated styles 1–11 per
  frame through the shared `Lightstyles` helper (`game/src/World/`); styles 32+ are switchable
  and default ON because nothing toggles them. A light meant to start off stays on.
- **`start_hidden` `env_sprite` coronas** are held **hidden**. `CoronaField`
  (`game/src/World/CoronaField.cs`) skips `start_hidden` sprites at load (matching the engine's
  spawn state); the script/I/O that would switch them on later is not ported.

**Work (fill-in on the R0 substrate — no new visual path):**
- A `light`/`env_*` input handler cross-refs lump-15 lights to their `light*`/`env_*` entities in
  `.ents` by `targetname` (8 of 2841 lights carry one), and on `TurnOn`/`TurnOff`/`Toggle` sets the
  addressed `LightRig` light's energy/style. Held-ON style 32+ lights start from their authored
  switch state instead of forced ON.
- The `env_sprite` handler (R1) exposes `SetVisible`; a `ShowSprite`/`HideSprite` input toggles a
  `start_hidden` corona's `CoronaField` billboard.
- Both are pure I/O dispatch onto existing nodes — the seams are the §6 "Switchable lights" and
  "ScriptHide/ScriptUnhide" rows.

**Acceptance:** a switchable light entity fired OFF via a `logic_relay`/trigger goes dark; a
`start_hidden` corona shows on its ON input; unfired maps render identically to today.

---

## 8. Design decisions locked

1. **`.ents` is the entity source of truth for *addressable* entities.** Anything that will need
   runtime I/O identity (dynamic props, switchable lights/sprites, doors) spawns from
   `EntityWorld` reading `.ents`, because it already holds the identity + outputs the deferred
   I/O needs. **Divergence from the original plan:** pure, non-addressable visuals have instead
   shipped through typed sidecars — `env_sprite` coronas via `.sprites` + `CoronaField`, exactly
   as static props use `.props` + `LoadProps`. That is the accepted pattern now; move a feature
   onto the `EntityWorld` substrate only when it needs addressability (runtime show/hide, move,
   I/O), not merely to be drawn.
2. **GAME_LUMP static props stay on the `.props` sidecar path** (they have no entity identity /
   no I/O). Dynamic props (R2) go through `EntityWorld` but reuse the same decode + MultiMesh code.
3. **Spawn-flag filtering is mandatory** in every handler (StartOn / not-StartHidden).
4. **No event queue / input dispatch / Python is built now** — only declared as seams (§6).

## 9. Open questions

- R4: `particle_definition` resolution (see R4 "investigate first").
- R1: exact `scale`→world-size and additive-energy mapping for each sprite material — determine
  by capture-vs-game tuning; expose as knobs.
- R1: occlusion-fade implementation choice (physics raycast vs depth-texture sample) for
  rendermode 3.

## 10. Related docs

`docs/entity_io.md` (the 7-field I/O, use_icon, StartHidden/ScriptHide — the deferred behavior
surface), `docs/python_bridge.md` (name→delegate namespace, the four Python call paths),
`docs/lighting.md` (real-time LightRig + coronas + lightstyles), `docs/mdl_v2531.md` (prop model decode),
CLAUDE.md ("BSP format", "Static props / models", "Material fidelity", "Textures").
