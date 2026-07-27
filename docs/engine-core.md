# Engine core — the entity object model and its two-phase build plan

This doc defines the runtime object language for Track B: the core types, how they
interact, and the plan to stand them up. It synthesizes `entity_io.md`,
`python_bridge.md`, `game_runtime.md`, `animation_and_movers.md` Part B,
`entity_visuals.md`, `level_transitions.md`, and `debug-tooling.md` — the requirements
below cite those docs rather than re-deriving them. Status tracking lives in
`docs/roadmap.md`.

## Design rules (the object language)

These rules are load-bearing; every Track B system and every debug tool assumes them.

- **R1 — Entities are plain C++ objects; Unreal objects are bodies.** A live entity is a
  `FElysiumEntity` (no UObject, no actor, no reflection dependency). Unreal
  actors/components are optional *embodiments* attached to an entity for
  rendering/physics/overlap only. **All game state lives on the entity object** — bodies
  are disposable presentation. This keeps save/load, travel teardown, the inspector, and
  determinism in our own code, and matches the three-tier spawn policy
  (`rebuild-strategy.md` B1): logic entities never get a body at all.
- **R2 — One name table per class serves everything.** Each classname registers a
  **class descriptor**: input table (name → member-function thunk) + field table
  (name → typed accessor). One case-folded lookup, walking the base-class chain
  (derived shadows base), serves: Hammer I/O input dispatch, Python attribute
  get/set/call (`Entity.__getattr__` is exactly this lookup in VtMB — `python_bridge.md`),
  keyvalue application at spawn, save-field enumeration, and the debug inspector's
  `ent_dump`/`ent_info`. There is no second dispatch mechanism anywhere.
- **R3 — Identity is a generation-checked handle; targetnames are non-unique.**
  `FElysiumEntityHandle {Index, Epoch}` — the index is the entity's position in the
  `.ents` array (stable across runs, never reused within a map load; killed entities are
  marked dead, not recycled), the epoch invalidates all handles on map teardown. Dead
  handles read as falsy (VtMB scripts rely on `if(ent):` after deletion). Name lookup is
  a `TMultiMap<FName, Handle>` (`Target("foo")` fans out; `FindEntityByName` returns
  first match) plus a classname index. Special targets `!self`/`!activator` resolve at
  dispatch time. Canonical debug string everywhere (logs, labels, inspector):
  `#<index> <targetname>(<classname>)`.
- **R4 — One clock, one queue, no engine timers.** Game-visible time is
  `FElysiumGameClock` (absolute game seconds, pausable, scalable — VtMB's `curtime`;
  variable step, no fixed tick). Delayed I/O, `ScheduleTask` source strings, think
  scheduling (per-entity next-think, FLT_MAX = never), and later timed discipline events
  all live in **one time-sorted `FElysiumEventQueue`** keyed on absolute game seconds.
  Never `FTimerManager`, never `SetTimer` — the queue and think times must serialize
  (VtMB saves both — `game_runtime.md` §2).
- **R5 — Two chokepoints, instrumented from day one.** All input delivery passes through
  `FElysiumEntityWorld::AcceptInput(...)`; all deferral passes through
  `FElysiumEventQueue::Add(...)`. No side channels. Every debug facility
  (`developer 2` log, ring-buffer history, VLOG, pause/step, `ent_break`, overlays)
  hangs off these two functions via a sink interface — three log lines instrument the
  whole game (`debug-tooling.md` Layer 2).
- **R6 — Dormancy is one reversible switch.** `StartHidden`/`ScriptHide`/`ScriptUnhide`
  is a whole-entity OFF state on the base class: non-solid (saving the prior solidity),
  next-think = never, undrawn, all together (decompiled semantics, `entity_io.md`). A
  hidden entity is inert — it cannot be touched, traced, or used. Never model this as
  visibility; it gates the body's collision *and* rendering *and* the think.
- **R7 — Addressability decides record vs sidecar.** Anything that needs runtime I/O
  identity (hide/show, inputs, outputs) is driven from its `.ents` record; pure
  non-addressable visuals keep shipping through typed sidecars (`entity_visuals.md` §8).
  Sidecar-built visuals that *can* be addressed (switchable lights, StartOff sprites)
  cross-reference by targetname. Spawn-flag filtering (`start_hidden`, StartOn bits) is
  mandatory in every handler.
- **R8 — Serialization shape mirrors VtMB's save blocks.** Four blocks, all plain
  structs we own: (a) per-entity save-flagged fields via R2's field tables, (b) the
  event queue (including deferred Python source strings), (c) per-entity think times,
  (d) `G` as a dynamic keyed blob (values are ints in the main, but 3 lists / 1 dict /
  3 strings exist — the store is variant-valued, **default 0 on miss**).

## Core types

| Type | Kind | Owner | Responsibility |
|---|---|---|---|
| `FElysiumVariant` | struct | — | Small tagged value: Void/Bool/Int/Float/String/Vector/Handle — the fieldtype marshalling currency for input params, fields, and script values. |
| `FElysiumEntityHandle` | struct | — | `{int32 Index, uint32 Epoch}`; resolves via the entity world; falsy when dead/stale. |
| `FElysiumEntityDef` | struct | `FElysiumEntityDefs` | Immutable parsed `.ents` record: classname, targetname, origin, `keys{}`, `hulls`+`contents`+`blocks_player`, `start_hidden`, 7-field `outputs[]`. The def array is the map's entity "asset". |
| `FElysiumEntity` | plain C++ base class | entity world | Live entity: handle, def, dormancy state (+saved solidity), next-think, optional body pointers. Base implements `Kill`, `ScriptHide`, `ScriptUnhide` once (they reach every class through the chain — `entity_io.md`). Virtuals: `Spawn()`, `Think()`, `AttachBody()`/`DetachBody()`. |
| `FElysiumClassDesc` / `FElysiumClassRegistry` | structs + singleton | module | R2's per-classname descriptor: factory fn, base-class link, input table, field table. Registered by static per-class registration in each entity `.cpp`. Unregistered classnames spawn as **inert record entities** (still listed, pickable, dumpable — coverage grows classname-by-classname). |
| `FElysiumIOEvent` | struct | queue | `{FireTime, TargetName/TargetHandle, Input, Param, PythonSrc, Activator, Caller}` — one queued delivery. |
| `FElysiumEventQueue` | plain C++ | entity world | Time-sorted pending events; `Add`, `Service(now)`, `Cancel(caller)`, pause/step flags, serializes. Zero-delay chains drain in the same service pass with an iteration cap (loop guard, logged). |
| `FElysiumEntityWorld` | plain C++ | `AElysiumMapActor` (TUniquePtr) | The substrate: def parse, entity storage + name/class indices, spawn pass, `AcceptInput` chokepoint, output firing (`times` countdown on the def's outputs), think servicing, the queue, the debug sink list, teardown (epoch bump). Ticked by the map actor with game delta. |
| `FElysiumIOSink` | interface | entity world | Debug tap: `OnQueued`, `OnDelivered`, `OnOutputFired`, `OnUnknownTarget/Input`. Ring buffer (1,000 entries, always on), log category `LogElysiumIO`, VLOG, and Phase 2 UI all implement it. |
| `IElysiumScriptHost` | interface | `UElysiumGameInstance` | `Eval(source, ctx) → FElysiumVariant`. Phase 1 ships `FElysiumNullScriptHost` (logs the call, returns 0) so field-6 Python payloads flow through dispatch visibly from day one; the real evaluator is M4 and slots in behind the interface (B6). |
| `UElysiumGameStateSubsystem` | GameInstance subsystem | game instance | Persistent-across-travel state: the `G` store (variant-valued, default-0), the quest string→int map, `FElysiumGameClock`. Later: the RPG sheet. |
| Bodies: `UElysiumBrushComponent` | `UPrimitiveComponent` (convex `UBodySetup` from the def's hulls) | map actor | Brush-entity embodiment: collision/overlap (+ optional debug draw); carries its owning handle; overlap events route back to the entity world. One per brush entity, positioned at the def origin (brush geometry is entity-local, origin = hinge for rotating doors). |
| Bodies: `AElysiumEntityActor` | thin `AActor` | map (spawned) | Model-entity embodiment (`prop_dynamic`, NPCs, items): mesh + transform; carries its handle; no game state. |

Persistent vs map-scoped state splits as follows: `UElysiumMapSubsystem` (lifecycle/travel)
and `UElysiumGameStateSubsystem` + script host live on the game instance;
`FElysiumEntityWorld` and everything it owns die with `AElysiumMapActor`.

## The flows (how the objects interact)

- **Map load:** map actor builds geometry (existing) → parses `.ents` into defs → entity
  world allocates one `FElysiumEntity` per def via the registry (inert record when
  unregistered) → `Spawn()` applies keyvalues through the field tables, builds bodies
  (skipped when `start_hidden` — dormant from birth), indexes names → fires the
  `logic_auto` `OnMapLoad` outputs through the queue. The world is live.
- **Input dispatch:** `AcceptInput(target, input, param, activator, caller)` resolves
  `!self`/`!activator`, fans out over the name multimap, walks each entity's class-chain
  input table (case-folded), invokes the thunk, notifies sinks. Unknown target/input:
  log once, count, keep going (retail data contains dead wires).
- **Output firing:** an entity fires a named output → for each matching 7-field def
  output whose `times` has not run out: queue the I/O delivery at `now + delay`, and if
  field 6 carries Python, attach the source string — the queue forwards it to the script
  host at fire time. 1,500 outputs fire *only* Python; the queue entry is still real.
- **Tick (map actor Tick, in order):** advance clock (unless paused) → run due thinks
  (entities whose next-think ≤ now) → `Service(now)` on the queue (deliver everything due;
  drain zero-delay chains with the loop guard). **Retail is think-first** — confirmed in
  `vampire.dll`: the server frame calls `Physics_RunThinkFunctions` (`FUN_1003bdd0`,
  `0x1011ac1b`) and *then*, at `0x1011ac34`, the single `CEventQueue::ServiceEvents`
  (`FUN_100cebb0`, fires each event with `fireTime ≤ curtime`: Entity I/O via `AcceptInput`
  vtable `+0x1d8`, field-6 Python via `FUN_100ce990`, `ScheduleTask` source via `FUN_100ce8a0`).
  So an output fired *during* a think is serviced after all thinks that frame, not interleaved.
  Match retail unless a determinism reason argues otherwise (RE2, `roadmap.md`).
- **Touch/use:** brush bodies raise begin/end overlap → entity world translates to
  `OnStartTouch`/`OnEndTouch` outputs (trigger classes), respecting dormancy. `+use` is
  a camera trace against a use-only collision channel on usable bodies (13 classnames
  carry `use_icon`); hit → `Use` input; the button's own `OnIn`/`OnOut` outputs arm the
  reticle icon (M3).
- **Travel:** `trigger_changelevel` overlap (or scripted `ChangeNow`) →
  `UElysiumMapSubsystem::Travel(map, landmark)`; new player position = destination
  landmark + (player − source landmark). Entity world, queue, bodies die with the map
  actor (epoch bump kills all handles); `G`, quests, clock, player state persist on the
  game instance.
- **Save (M6, designed now):** the four R8 blocks serialize from the entity world's
  field tables + queue + think times and the game state's `G`/quests into a `USaveGame`
  container. Nothing engine-side holds game-visible state, so this stays mechanical.
- **Debug (Phase 2 consumes, Phase 1 provides):** sinks on the chokepoints; the entity
  world exposes read-only iteration (defs, entities, indices, queue contents) so Cog
  windows and `ent_*` verbs render straight from the substrate; bodies carry handles so
  a crosshair trace resolves to an entity in one step; labels/folders on bodies
  (`WITH_EDITOR`) mirror the canonical debug string.

## Class coverage ladder

Phase 1 registers just enough real classes to make the substrate observable end-to-end
on `sp_tutorial_1` (1,226 entities, 75 classnames): `logic_auto` (map-load ignition),
`logic_relay` (a quarter of all game wires), `trigger_multiple`/`trigger_once` (touch →
outputs; 55 volumes in the tutorial), plus the inert-record fallback for the other ~70
classnames. M3 then climbs the histogram with the debug layer watching: `func_button`,
`func_door`/`func_door_rotating` (the shared `LinearMove`/`AngularMove` + 4-state
toggle base from `animation_and_movers.md` B.4), `math_counter`, `logic_timer`,
`trigger_changelevel` + landmark travel, `+use`, `prop_dynamic`, `func_brush`,
`point_teleport`, `logic_pythoncheck` (via the script host), elevator family.

## Phase 1 — core substrate (engine changes, debug-ready)

*Status tracking lives in `docs/roadmap.md` (**P1**; step 1 below is roadmap 0.4). This
section is the design detail behind those tasks.*

Ordered; each step compiles and runs on its own.

1. **Pipeline verification first:** confirm `UE_bsp_to_scene.py` emits `.ents` fully in
   Unreal space (origins *and* entity-local hulls, cm/Z-up/winding) per the `UE_`
   convention; fix in `tools/bsp.py` terms if not. The runtime reads defs verbatim.
2. **Currency types:** `FElysiumVariant`, `FElysiumEntityHandle`, `FElysiumGameClock`
   (on the new `UElysiumGameStateSubsystem`, with the `G`/quest stores).
3. **Defs:** `.ents` JSON parser → `FElysiumEntityDefs` (+ `.emc`-style cache only if
   parse time demands it).
4. **Registry + base entity:** class descriptors, registration statics, case-folded
   chain lookup; `FElysiumEntity` with `Kill`/`ScriptHide`/`ScriptUnhide` and dormancy
   per R6; inert-record fallback.
5. **Entity world + queue + chokepoints:** storage, indices, spawn pass, `AcceptInput`,
   output firing with `times`, think servicing, teardown; **sinks, ring buffer,
   `LogElysiumIO`, and VLOG lines land in this same step** — the chokepoints are never
   uninstrumented. `FElysiumNullScriptHost` behind `IElysiumScriptHost`.
6. **Brush bodies:** `UElysiumBrushComponent` per brush entity from def hulls (handle
   attached, overlap routing, dormancy-gated) — triggers become live.
7. **Starter classes:** `logic_auto`, `logic_relay`, `trigger_multiple`/`trigger_once`.
8. **Editor affordances:** labels/folders on all bodies and existing spawn paths
   (`#if WITH_EDITOR`), canonical debug strings in every log line.

**Acceptance:** loading `sp_tutorial_1` fires the `logic_auto` chains through real
queue entries; walking through a trigger volume logs
`(time) OnStartTouch #n name(trigger_multiple) → target.Input(param)` lines; Python
payloads appear as script-host log lines; the ring buffer holds the session's I/O
history; PIE Outliner shows labeled bodies. All observable with nothing but the log —
no debug UI exists yet.

## Phase 2 — the debug layer

*Status tracking lives in `docs/roadmap.md` (**P2**). This section is the design detail.*

Builds `debug-tooling.md`'s Layers 1–2 on the Phase 1 substrate (core first, then UI):

1. **Cog vendored** into `Plugins/`, compiled on 5.8, world-subsystem shell, dev-only.
2. **Entity windows** reading `FElysiumEntityWorld` directly: browser (filter by
   class/name, histogram, dormancy state), inspector (def keyvalues, live fields via
   the field tables, outputs with all 7 fields, fire-any-input buttons), event-queue
   window (pending entries, ring-buffer history, pause/step).
3. **`ent_*` console verbs** on the chokepoints: `elysium.ent_fire` (through the real
   queue), `ent_dump`/`ent_info` (off the class tables), `ent_pause`/`ent_step`,
   `ent_break <target> [input]`, `ent_text`/`ent_bbox`/`ent_messages` per-entity
   overlay bitmask — all with the picker fallback (no argument = entity under the
   crosshair, via body handles; nearest-origin for bodiless logic entities).
4. **World visualization:** `elysium.showtriggers` (wireframe hulls colored by
   class/state), I/O beam arrows on fire (fading, duration-drawn), entity gizmos
   (color-keyed boxes + labels, 3-state cycle off/visible/all, cone pick).
5. **Maps/Lights Cog windows + `elysium.reload`** (absorbing the Canvas HUD panels).

**Acceptance:** in standalone `play.bat` — open the entity browser, pick the elevator
call button through the crosshair, `ent_fire` its `OnPressed` target chain by hand,
watch the beams and the queue window, pause and single-step the chain, and reload the
map after a re-export without restarting. That is the M3 test harness, ready before
M3 writes its first mover.

## Open items (tracked, non-blocking)

- **Trigger/button spawnflag semantics** — **decompiled** (RE1): `func_button`
  (`CBaseButton::Spawn`) and `trigger_multiple`/`trigger_once`
  (`CBaseTrigger::PassesTriggerFilters`) bit maps are in `entity_io.md`. Trigger filter
  matches stock Source (`0x1`=clients … `0x8`=physics + `m_hFilter`); the button does not.
- **Retail queue-vs-think order** — **confirmed think-first** (RE2, see the Tick note above):
  thinks run, then `CEventQueue::ServiceEvents`.
- **Ghidra datamap export** — method confirmed (RE4): VtMB datamaps are runtime-built by
  per-class builders (`CBaseEntity` = `FUN_100a22f0`); the base keyfield/input/output contract
  is extracted (`python_bridge.md`). A batch `DumpDatamap` over the builders → JSON per classname
  is still to build — optional, valuable once class coverage widens.
- **`G` default-0 and dialogue error-to-false** — **confirmed** (RE3, `python_bridge.md`):
  `G.tp_getattr` returns `PyInt(0)` on a miss; `logic_pythoncheck`/exec paths clear errors and
  evaluate false. Implement as documented.
