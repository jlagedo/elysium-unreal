# Elysium-Unreal — Consolidated Roadmap (single source of truth)

**This document is the one work tracker.** It consolidates and supersedes the plan/tracking
sections of `rebuild-strategy.md` (milestones M0–M6, pipeline backlog),
`roadmap-lighting-unreal.md` (lighting tasks L*, absorbed — that file is now a stub),
`debug-tooling.md` (build order), and `engine-core.md` (Phases 1–2). Those docs remain the
**design/reference detail** behind the tasks here; this doc owns **sequencing, status, and
decisions**. If a task exists anywhere, it exists here.

## How to use this doc

- Status marks: `[ ]` open · `[~]` in progress/partial · `[x]` done (verified) · `[P]` parked
  (deliberately deferred — revisit trigger stated).
- Every task: **ID — name — why/where — acceptance — deps**. Detail lives in the linked
  design doc; don't duplicate it here — link it.
- **Decision log** (bottom): append-only, dated. Record a decision when it's made, including
  "pending" decisions with their trigger.
- Old plan IDs (M1–M6, L0–L5, X1/X2, engine-core Phase 1/2) map to new IDs in the
  **traceability table** at the bottom; other docs may still say "M3" — that table resolves it.
- One rule for edits: land a task → flip its checkbox here in the same change.

## North star and the slice ladder

Rebuild VtMB as a playable game on UE 5.8 + C++, runtime-loading this repo's exported
intermediates — no game content in `.uasset`s, bring-your-own-game holds (strategy and
principles: `rebuild-strategy.md`). Everything is proven on `sp_tutorial_1` first
(1,226 entities, 75 classnames — VtMB's own vertical slice), then scaled across ~100 maps.

The phases below are **vertical slices** — each ends with something observable/playable:

| Phase | Slice outcome | Runs parallel with |
|---|---|---|
| **P0 — Ground truth & de-risk** | numbers, second map, verified data, spiked risks | — (do first) |
| **P1 — Entity substrate** | tutorial's I/O chains fire visibly in logs | P3 |
| **P2 — Debug layer** | live entity browser/inspector/verbs in standalone | P3 |
| **P3 — Lighting correct & engaged** | frame reads right, many-light path guaranteed | P1, P2 |
| **P4 — Interaction** | elevator chain works; walk into the pawnshop | P3 tail, P6 |
| **P5 — Scripting foundation** | tutorial's Python payloads actually run | P6, P7 |
| **P6 — Audio foundation** | doors/buttons/ambience are audible | P5, P7 |
| **P7 — Dressing & parity** | A/B match vs Godot viewer on tutorial + hubs | P4–P6 |
| **P8 — Characters & menu** | NPCs stand in the world; New Game from a real menu | P7 tail |
| **P9 — Dialogue & persistence** | **tutorial completable as retail**, save/load works | — |
| **P10 — Scale & ship-shape** | all maps, floor validated, packaged story | ongoing after P4 |

## Done foundation (verified, compressed — details in `rebuild-strategy.md`)

- [x] M0: world+skybox PMC rendering, DDS/PNG textures, `.emc` cache, fly pawn, map
  switching, Canvas HUD, `elysium.*` commands.
- [x] `UE_` coordinate conversion (exporter emits Unreal cm/Z-up/LH, winding pre-reversed).
- [x] `.env` sky/fog + `.cube` LUT + `M_Sky`; light rig + lightstyles (fully dynamic
  HWRT Lumen + MegaLights + VSM config); `.hulls`/`.dispcol` brush collision (walkable);
  static props via runtime `UStaticMesh` + ISM with convex collision.
- [x] Designs adopted: `engine-core.md` (entity object model), `debug-tooling.md`
  (three-layer debug architecture), `map-architecture.md` (map lifecycle; async travel is
  design-only → R10.4).

---

## P0 — Ground truth & de-risk

Cheap tasks that unblock or de-risk everything downstream. Do these before/alongside P1.

- [x] **0.1 Profiling baseline** *(was L0.1)* — **automated & headless.** `profile.bat` drives
  the `-ElysiumProfile` harness (`ElysiumProfiler.cpp`): fixed vantages near spawn, warmup +
  300-frame CSV capture (`-csvGpuStats` per-pass GPU ms) + a `ProfileGPU` log dump, then exit;
  `tools/profile_report.py` emits the table. Baseline for `sp_tutorial_1` **and** `sm_hub_1`
  committed in the appendix below; `PCD3D_SM6` confirmed. Add a vantage in-game with
  `elysium.campos`. *(Dev card RTX 5070 Ti — proportions/regression, not floor fps.)* *Deps:* none.
- [x] **0.2 MegaLights engagement check** *(was L0.2)* — **answered by the 0.1 harness** (it logs
  MegaLights vs ShadowDepths every run). MegaLights dominates and the many-light cost is flat
  (395 vs 687 lights → same ~1 ms); ShadowDepths never blows up (≈0 on sm_hub_1). No silent VSM
  fallback → **3.1 is not the immediate next task.** See appendix "0.2 verdict". *Deps:* 0.1.
- [x] **0.3 Export a second map** *(was X1)* — `sm_hub_1` (687 lights) **and** `sm_pawnshop_1`
  (161 lights, the travel target) are exported via `export_all.py`, load, walk, and profile
  headlessly; each has fixed harness vantages (`sm_hub_1` h1/h2, `sm_pawnshop_1` p1/p2/p3 plus
  the shared spawn vantage — baselines in the appendix). Unblocks texlights (3.4), travel (4.6),
  calibration (10.1), A/B (7.8), and kills the tutorial-only bias.
- [x] **0.4 Sidecar space audit** *(was X2 + engine-core P1 step 1)* — audited every sidecar the
  next phases consume against `UE_bsp_to_scene.py`: `.ents` origins (`:272`) + entity-local hulls
  (`:286`), `.sprites` origin (`:188`) + sizes (`INCH_TO_CM` `:201`), `.spawn` origin (`:1147`) +
  pre-negated yaw (`:1150`), `.water` plane Z (world-scene verts) + fogdist (`:1185`), `_decals.obj`
  (`:1030`, winding reversed) — **all already route through `source_to_unreal`/`INCH_TO_CM`, i.e.
  Unreal cm.** The exporter was fully migrated; only the `rebuild-strategy.md` contract table lagged
  (`.sprites` said "Godot metres", `.spawn` said "Source coords") — **corrected**. No `tools/bsp.py`
  code fix needed → **PL7 is empty.** Runtime spot-check: `.spawn` read verbatim
  (`ElysiumMapActor.cpp:705,722`; the +100 cm Z lift is a spawn-clearance offset, not a conversion).
  *Deps:* none.
- [x] **0.5 Cog 5.8 compile spike** — **de-risked: Cog builds and runs clean on UE 5.8.** Vendored
  the main [Cog](https://github.com/arnaud-jamin/Cog) plugin only (upstream `cb1b435`, `main`) into
  `Plugins/Cog/` — the 6 modules CogImgui/Cog/CogEngine/CogCommon/CogDebug/CogDebugEditor + bundled
  ImGui/ImPlot/NetImgui; the GAS/AI/Input plugins (CogAbility/CogAI/CogInput/CogAll/CogCommonUI) are
  **not** vendored (this project uses none of those systems). `ElysiumUE.Build.cs` deps: `CogCommon`
  (all configs) + `Cog`/`CogDebug`/`CogEngine`/`CogImgui` (non-Shipping only); plugin enabled in
  `ElysiumUE.uproject`. `UElysiumCogSubsystem` (`UWorldSubsystem`, `#if ENABLE_COG`-gated) depends-in
  `UCogSubsystem` and registers 15 stock CogEngine windows (Inspector, Selection, Collision Viewer,
  Console, Output Log, Stats, Metrics, Plots, …) — the `Cog::AddAllWindows` helper is avoided because
  it pulls in CogAbility/CogAI. **The known ImPlot `INFINITY` MSVC patch (issue #71) was NOT needed**
  on this toolchain (VS 14.50.35717 / UE 5.8); only non-fatal C4996 deprecation warnings in upstream
  Cog. Editor target compiled (84 s, exit 0); standalone boot loaded all 6 Cog DLLs,
  `UCogSubsystem::TryInitialize` fired in-world, traveled to `sp_tutorial_1`, zero Cog/ImGui errors.
  Full integration (custom Maps/Lights/Entities windows, F1 toggle polish) is 2.1. *Acceptance met:* Cog
  inspector opens in PIE and standalone — compile + runtime-load verified, and the F1 ImGui menu +
  Inspector window confirmed on-screen (`play.bat`, F1). *Deps:* none.
- [x] **0.6 `ent_survey` count reconciliation** *(RE, small)* — re-ran the survey; the two
  snapshots were both retail runs, and the tool deterministically yields **16,125 outputs /
  1,591 Python** on `Vampire/maps` today (the 16,214/1,621 figure was a stale, unreproducible
  snapshot). Pinned across `entity_io.md`, `python_bridge.md`, `rebuild-strategy.md`,
  `tools/CLAUDE.md`; also recorded the patch-set counts (108 maps → 24,081 outputs).
- [x] **0.7 Repo hygiene** — the entire `tools/ghidra*/` and `tools/re/` trees are now local-only
  RE references. Untracked the 15 previously-committed hand-authored files (`git rm --cached`: the
  9 `Dump*/EnableAIF.java` scripts + `run.ps1` + `README.md` + inner `.gitignore` under
  `tools/ghidra/`; `dll_recon.py` + `ghidra_extract_mechanics.{java,py}` under `tools/re/`) — all
  kept on disk. Replaced the selective ignore rules with two blanket globs (`tools/ghidra*/`,
  `tools/re/`) and re-based the docs that called these "tracked/committed"
  (`tools/CLAUDE.md`, `CLAUDE.md`, `rebuild-strategy.md`, `m0_menu_build.md`). *Deps:* none.
- [x] **0.8 Re-base `entity_io.md` on the patch (engine-loaded) map set** *(RE, small)* — every
  table in `entity_io.md` is now the **engine-loaded** set (`Unofficial_Patch/maps`, 108 maps):
  **71,096 entities / 326 classnames / 24,081 outputs / 6,956 Python** (6,851 Python-only, 105
  both); the retail set (63,861 / 299 / 16,125 / 1,591) is kept as a labelled comparison.
  Established the patch `maps/` is a **strict superset** of retail — all 101 retail names
  (shadowed by heavier patched versions) plus 7 patch-only maps (`hw_chateau_1`, `hw_warrens_2b`,
  `la_bradbury_1`, `la_library_1`, `la_malkavian_3b`, `sm_coffee_1`, `sm_smoke_1`) — so globbing
  the patch dir *is* the engine-loaded set (the per-name `map_path` resolution gives the 68,430
  subset, which drops those 7). Rebased all histograms (classname counts, StartHidden, usable-set
  incl. the patch-only `prop_doorknob-wesp`, inputs-by-class, field-6 breakdown, most-driven,
  biggest sources — `events_world`/`events_player` jump into the top output sources), the
  `sp_tutorial_1` `.ents` example (now 1868 ents / 185 brush / 466 hulls / 1028 outputs), and
  propagated the 6,956 field-6 Python figure to `python_bridge.md` / `rebuild-strategy.md` /
  `tools/CLAUDE.md`. **Decision:** `ent_survey.py` keeps the retail default (stable baseline) and
  gains a `--patch` flag for the engine-loaded set; not defaulted patch-first. *Deps:* none.

## P1 — Entity substrate *(design: `engine-core.md` — read it; steps here are the tracker)*

- [x] **1.1 Currency types + persistent state** — `FElysiumVariant` (Void/Bool/Int/Float/
  String/Vector/Handle, total coercions), `FElysiumEntityHandle` (stable `.ents` index +
  epoch; falsy-on-resolve is the world's job in 1.4), `FElysiumGameClock` (pausable/scalable
  curtime, passive until the map-actor tick advances it); `UElysiumGameStateSubsystem` (GI
  subsystem) with the `G` store (variant-valued, **default 0 on miss**, None-deletes,
  **case-sensitive** keys) + quest string→int map + the clock, and an `elysium.g` verb.
  *Deps:* none.
- [x] **1.2 `.ents` defs parser** — `FElysiumEntityDefs::Parse` reads `<map>.ents` (one JSON
  blob, via the `Json` module) into immutable `FElysiumEntityDef` records: classname,
  targetname, Unreal-space origin (verbatim — UE_ convention), raw `Keys{}`, brush fields
  (`Model`/`Hulls` as entity-local `FElysiumConvexHull` point clouds/`Contents`/
  `bBlocksPlayer`), `bStartHidden`, and 7-field `FElysiumOutputDef` outputs
  (name/target/input/param/delay/times/python; field-6 "extra" is dropped at export, so not
  carried). `IsBrush`/`IsPythonOnly` helpers. `MapEnts` path added to `FElysiumContentPaths`.
  `elysium.ents [map]` verifies the round-trip off disk (entity/brush/hull/output/py-only/
  start_hidden/classname counts) without spawning anything. *Deps:* 0.4.
- [x] **1.3 Class registry + base entity** — `FElysiumClassDesc`/`FElysiumClassRegistry`:
  per-classname descriptor (factory + base-class link + input table + typed field table),
  registered by module-static `FElysiumClassRegistrar`; case-folded (FName-keyed) lookup that
  walks the base chain (derived shadows base) for both inputs and fields. `FElysiumEntity`
  (plain C++, non-copyable) carries the CBaseEntity keyfield contract (22 base keyfields from
  `python_bridge.md`), the three base inputs (`Kill`/`ScriptHide`/`ScriptUnhide`), and the
  one-switch dormancy (R6: `bHidden` → non-solid + next-think-never + undrawn via
  `OnDormancyChanged` body hook, saved/restored think; `Kill` terminal). `Construct` applies
  raw `.ents` keyvalues through the chain field table (honouring `start_hidden`);
  `FElysiumClassRegistry::Create` builds the leaf class or an **inert base record** for
  unregistered classnames (`bRecordOnly`). `elysium.classes [classname]` lists the registry or
  resolves one classname, dumps its chain-walked tables, and probes the base contract on a
  throwaway entity (fires hide/unhide/kill through the registry). No world/queue yet — that is
  1.4. *Deps:* 1.1.
- [x] **1.4 Entity world + event queue + chokepoints** — `FElysiumEntityWorld` (plain C++,
  owned by `AElysiumMapActor` via `TPimplPtr`, dies with the map): one `FElysiumEntity` per def
  (registry `Create`, inert record when unregistered), name (`TMultiMap`) + classname indices,
  spawn pass, generation-checked `Resolve` (epoch + not-dead → the falsy-when-dead/stale
  contract), teardown = epoch-to-0 (invalidates all handles at once). The two chokepoints:
  `AcceptInput` (resolve `!self`/`!activator`/`!caller`, fan out over non-unique names, walk the
  class-chain input table, invoke thunk; unknown target/input log-once + counted) and
  `FElysiumEventQueue::Add` (the only queue entry, reached via `FireOutput`/output-firing with the
  per-entity `times` countdown seeded from the def). `Tick` is **think-first** (retail order,
  RE2): `RunThinks(now)` then `ServiceEvents(now)` — the queue drains due events including
  zero-delay chains queued mid-pass, with a 10k-per-frame loop guard. `FElysiumIOSink` taps both
  chokepoints from day one: always-on 1,000-entry ring buffer (`FElysiumRingBufferSink`) +
  `LogElysiumIO` stream + VLOG (`FElysiumLogSink`). `FElysiumNullScriptHost` behind
  `IElysiumScriptHost` (on `UElysiumGameStateSubsystem`, `SetScriptHost` for M4) logs field-6
  payloads and returns Void. `FireMapLoadOutputs` ignites `OnMapLoad` at load (P1.7's `logic_auto`
  supersedes). Verbs: `elysium.world` (histogram/queue/ring/dead-wire summary),
  `elysium.world.io [n]` (ring dump), `elysium.world.fireinput` (inject an input, test harness).
  Verified on `sp_tutorial_1`: 1868 entities live, OnMapLoad chains fire through the real queue at
  their delays and deliver to resolved targets (unregistered classes hit `[no input]` as expected).
  *Deps:* 1.2, 1.3.
- [x] **1.5 Brush bodies** — `UElysiumBrushComponent` (`UPrimitiveComponent`, collision-only,
  no scene proxy) per brush entity: a convex `UBodySetup` cooked from the def's entity-local
  hulls (`CTF_UseSimpleAsComplex`, one `FKConvexElem` per hull), placed at the def origin,
  carrying its owning `FElysiumEntityHandle`. Runtime solidity is per-classname
  (`ElysiumBrushSolidityForClass`, since the export's `contents`/`blocks_player` don't
  discriminate): `trigger_*` → `OverlapAllDynamic` overlap volume, `func_illusionary` →
  `NoCollision`, everything else → `BlockAll` solid blocker. Overlap routing: begin/end overlap
  → the component (via the owning `AElysiumMapActor`, so no stale-world pointer) →
  `FElysiumEntityWorld::RouteBrushTouch` → the resolved brush entity's `OnTouchStart`/`OnTouchEnd`
  (base no-ops; P1.6 triggers override to fire `OnStartTouch`/`OnEndTouch`). Dormancy-gated (R6):
  the base `FElysiumEntity::OnDormancyChanged` drops the body's collision when inert (born dormant
  when `start_hidden`) and restores the built solidity on unhide. Bodies built in the world's spawn
  pass (after `Spawn()`), owned by the map actor, destroyed on world teardown; the entity gains a
  `World` back-pointer + `FireOutput` seam (set at Load) so leaf classes can fire outputs, and the
  pawn capsule raises overlap events. `elysium.BrushBodies` cvar A/Bs body building; `elysium.world`
  reports body + touch counts; the HUD collision line shows `ents/bodies`. Verified on
  `sp_tutorial_1`: 1868 entities → 185 brush bodies, no cook stall. *Deps:* 1.4.
- [x] **1.6 Starter classes** — the four leaf classes that make the substrate fire visibly
  (`Source/ElysiumUE/Private/ElysiumStarterClasses.cpp`, plain-C++ `FElysiumEntity` subclasses via
  module-static registrars). **`logic_auto`** owns map-load ignition: `Spawn()` schedules a one-shot
  think at t=0, `Think()` fires `OnMapLoad` once on the first world tick (superseding the generic
  `FireMapLoadOutputs`, now removed). **`logic_relay`** (the indirection layer) takes
  `Trigger`→re-fire `OnTrigger` (activator propagated), `Enable`/`Disable`/`Toggle` gate it, field
  `StartDisabled`. **`trigger_multiple`/`trigger_once`** derive from a shared **`CBaseTrigger`** chain
  node (never in the data — pure registry base) carrying `Enable`/`Disable`/`Toggle` + `StartDisabled`/
  `wait` fields; `OnTouchStart`/`OnTouchEnd` overrides translate the P1.5 brush-body overlap into
  `OnStartTouch`/`OnEndTouch` and (`wait`-debounced) `OnTrigger`; the filter reduces to the
  `ALLOW_CLIENTS` (0x1) spawnflag since the only P1.6 toucher is the player (43/44 tutorial
  `trigger_multiple` + all `trigger_once` carry it; the lone `0x8` physics-only trigger correctly
  ignores the player); `trigger_once` `Kill()`s itself after the first successful touch.
  Subclass-field marshalling is a local `AddSubclassField<TClass>` (static_cast accessor; the base
  `Field()` only takes `FElysiumEntity` members). Stock-Source remove-on-fire / fast-retrigger
  spawnflags are **not** modelled (unconfirmed for VtMB, which diverges on buttons; per-output `times`
  already caps re-fires). Verified headless on `sp_tutorial_1`: all 5 `logic_auto` fire `OnMapLoad`
  once through the real queue at their delays; injecting `Trigger→elev_up` cascades relay→relay
  (`elev_up`→`logic_closealldoors`→6 door `Close()`); `trigger_multiple` chain-resolves 6 inputs /
  24 fields; no loop-guard trips. *Deps:* 1.4, 1.5.
- [x] **1.7 Labels & debug strings** — editor-only (`#if WITH_EDITOR`) World Outliner affordance
  (debug-tooling.md Layer 0) on every runtime spawn path, via the shared `ElysiumEditorObjectName`
  helper (`ElysiumEditorLabels.h`, FName-safe fold; compiled out of Shipping so it keeps
  auto-names). `AElysiumMapActor` labels itself `Map:<name>` + drops into an `Elysium` Outliner
  folder; brush bodies name `Body_<idx>_<name>_<class>` and carry the exact canonical debug string
  as a `ComponentTag`; light-rig lights name `Light_<idx>_<point|spot|sun|tex>`; prop ISMs name
  `Props_<model>_<solid|nonsolid>`. Call sites gate the whole label build behind `#if WITH_EDITOR`,
  so a Development editor build reads as a live scene browser (F8 Eject, Details inspection) at zero
  Shipping cost. The canonical `#<idx> <name>(<class>)` string (`FElysiumEntity::DebugString`)
  already threads every I/O log line (the sinks + `RouteBrushTouch`), so logging needed no change.
  *Deps:* 1.5.

**Slice acceptance:** loading `sp_tutorial_1` fires the `logic_auto` chains through real
queue entries; walking through a trigger logs timestamped I/O lines; Python payloads appear
as script-host log lines; ring buffer holds the session history — observable with logs only.

## P2 — Debug layer *(design: `debug-tooling.md` Layers 1–2)*

- [x] **2.1 Cog integration** — the custom-window foundation on top of the 0.5 spike (which
  already vendored Cog, stood up `UElysiumCogSubsystem` with the 15 stock CogEngine windows, the
  `#if ENABLE_COG` out-of-Shipping posture, and proved the F1 menu renders in PIE + standalone).
  **`FElysiumCogWindow`** (`Private/ElysiumCogWindow.{h,cpp}`) is the base for every Elysium window:
  it hands derived windows the live handles Cog's UObject-reflection inspector can't reach —
  `GetMapActor()`/`GetEntityWorld()`/`GetGameState()`, resolved off the window's world — because the
  Track-B entities are plain C++, not UObjects. **`FElysiumCogWindow_Status`** (registered
  `"Elysium.Status"`, so an **"Elysium"** category appears in the F1 menu beside "Engine") is the
  first window: a live read-only summary in four sections — Map (name / surface / sky / light / prop
  counts), Collision (brush vs. trimesh, hulls, disp tris), Entities (records, brush bodies, event-queue
  depth, I/O ring-buffer fill, touch + dead-wire tallies), Clock (now / scale / paused) — reading the
  map actor's stat fields and the entity world's chokepoint counters directly. Every file is fully
  `#if ENABLE_COG` (compiles to nothing in Shipping); no public-API surface added (headers live in
  `Private/`). Cog's ImGui layout + `Config=Cog` UPROPERTYs persist under the gitignored `Saved/`
  (`Saved/ImGui/imgui.ini`, `Saved/Config/.../Cog.ini`) — nothing runtime-generated is tracked (only
  the vendored `Plugins/Cog/Config/DefaultCog.ini`). Editor target compiles + links clean. 2.2 (entity
  windows) and 2.5 (Maps/Lights) derive from `FElysiumCogWindow` the same way. *Deps:* 0.5.
- [x] **2.2 Entity windows** — three custom windows derive from `FElysiumCogWindow` under the
  `Elysium` F1 group. **Entities** (`FElysiumCogWindow_Entities`): a filterable, clipper-paged list
  of every substrate record (live + inert unhandled classnames), a targetname/classname search bar,
  live/hidden/dead/record include toggles, a per-classname histogram, and a colour-coded state
  column; clicking a row sets the shared selection. **Entity Inspector**
  (`FElysiumCogWindow_Inspector`, the B2 "primary test harness"): the selected entity's identity +
  dormancy, its chain-walked live fields (name/type/keyable/value off the class field tables), its
  raw `.ents` keyvalues, and its 7-field outputs (name, target, input, param, delay, times
  remaining/total, python); a param box + a fire button per chain-resolved input that injects
  through the real queue. **Event Queue** (`FElysiumCogWindow_EventQueue`): the pending time-sorted
  deliveries (relative fire time, target, input, param, caller, python flag), the always-on I/O
  history ring buffer (tail-following), and Pause / Step / Step-10 controls driving the queue's
  pause/step flags. The shared browser→inspector selection is a `static FElysiumEntityHandle` on
  `FElysiumCogWindow` (self-clears on reload via epoch mismatch). A new public chokepoint-respecting
  seam, **`FElysiumEntityWorld::EnqueueInput`** (queue a hand-made input at now+delay through
  `AddEvent`), backs the inspector's fire buttons and will back 2.3's `ent_fire`; the inspector
  targets one specific record via `!self` + `Caller = its handle`. All files `#if ENABLE_COG`;
  editor target compiles + links clean. *Deps:* 2.1, 1.4.
- [x] **2.3 `ent_*` verbs** — the Source-style verb set lands as `UElysiumEntityDebugSubsystem`
  (a `UTickableWorldSubsystem`, `Private/ElysiumEntityDebugSubsystem.{h,cpp}`): it registers the
  `elysium.ent_*` console commands (`ECVF_Cheat`), reaches the live world through the map subsystem's
  current map actor, and ticks to draw the overlays. **`ent_fire`** injects through the real queue via
  `EnqueueInput` (`!self` + Caller = each resolved handle, so shared targetnames still hit the exact
  record) — target matches targetname **or** classname (fans out over the class), no target = the
  crosshair picker, no input = list the target's chain-resolved inputs (safe discovery, no fire).
  **`ent_dump`** dumps one entity's live state / chain-resolved fields / raw keyvalues / 7-field
  outputs off the class tables; **`ent_info <class>`** dumps a class's base chain + chain-resolved
  inputs and typed fields (outputs are per-entity `.ents` data, not a class schema). **`ent_pause`**
  toggles the queue pause; **`ent_step [n]`** arms n single-steps (pausing first). **`ent_break
  [target] [input]`** pauses the queue when a matching input is delivered (handle-scoped when armed via
  the picker, else targetname/classname string; `ent_break` alone clears or arms on the crosshair).
  **`ent_text`/`ent_bbox`/`ent_messages`** toggle a per-entity overlay bitmask (identity/state text,
  colour-coded collision-bounds box, fading I/O message lines), `ent_clear`/`<verb> off` clear them;
  the fade freezes while the queue is paused so a breakpoint's evidence stays put. The **picker** is a
  multi-trace that returns the nearest brush body (solids block, triggers overlap) else the bodiless
  logic entity whose origin is nearest the aim ray. **The primary surface is the Cog Entity Inspector as a
  live crosshair inspector** (`FElysiumCogWindow_Inspector`): left open it keeps updating while you play
  (Cog renders visible windows even with the F1 menu closed), draws its own imgui reticle, and each frame
  traces the camera ray to report **whatever it hits — surface *and* entity**: actor / component / mesh /
  material + bound textures (so any wall/prop/texture is inspectable, not just entities), and if the hit is
  a brush/logic entity its full detail (identity, chain-walked fields, raw keyvalues, 7-field outputs).
  Selection is sticky (a plain surface keeps the last entity), and Text / Box / Messages overlay + break
  toggles + fire buttons sit right there; opening F1 to click them freezes the aim on the current entity.
  `ent_break` + the `ent_messages` capture ride a `FElysiumDebugTapSink` the subsystem installs into each
  new world epoch through the new public `FElysiumEntityWorld::AddSink` seam — the same sink interface the
  ring buffer and log use, so there is no I/O side channel (R5). The console verbs are the scriptable /
  Source-muscle-memory layer, not the only surface — F1 covers the whole flow (Entities browse → Inspector
  live-inspect + fire + overlays + break → Event Queue pause/step). Registration + tick compile out
  of Shipping (`#if !UE_BUILD_SHIPPING`); the in-world draws compile out wherever `ENABLE_DRAW_DEBUG` is
  off. Editor target compiles + links
  clean. *Deps:* 1.4–1.6.
- [x] **2.4 World visualization** — the three map-wide debug layers land on `UElysiumEntityDebugSubsystem`
  as a `FVizSettings` block the always-running tick renders (so a layer left on stays on while you play,
  F1 open or not), fronted by the **`Elysium.World Viz` Cog window** (`FElysiumCogWindow_WorldViz`) as the
  F1-first surface — every control there flips the same `Viz()` state the three thin `ECVF_Cheat` verbs
  flip. **Entity gizmos** (Godot viewer port): a color-keyed solid box per live entity as a **retained
  GPU-instanced layer** (`FElysiumGizmoLayer`) — one `UInstancedStaticMeshComponent` of unit cubes built
  once per epoch, colour+opacity in per-instance custom data read by the committed `M_Gizmo`/`M_Gizmo_XRay`
  masters (`ElysiumGizmoClassColor` palette — trigger red / light-sprite yellow / sound-ambient cyan /
  logic-math-relay magenta / prop-model green / other grey; hidden dimmed, dead transparent) with a
  targetname/class label, in an **off / visible / all** cycle (Off hides the component, Visible = depth-tested
  material so walls occlude, All = x-ray material drawn on top). Retained + event-driven: idle frames cost
  only the instanced draw, and a dormancy/liveness flip re-uploads just that one instance via
  `FElysiumEntityWorld::SetVisualChangedHook` (fired from `OnDormancyChanged`/`Kill`) — no per-frame rebuild,
  no distance cull needed. Only the labels stay immediate-mode (distance-culled). `elysium.ent_gizmos
  [off|visible|all]` (no arg cycles). **Show triggers**: the wireframe per-hull AABBs
  of every trigger brush entity (exact for VtMB's axis-aligned box brushes), colored by class or by
  enabled/dormant state (`elysium.showtriggers [0|1] [state]`). **I/O beams**: a fading caller→target
  arrow captured at the delivery chokepoint tap (`TapDelivered`, reusing the freeze-while-paused fade
  clock and the `AddSink` seam) and drawn foreground so causality is followable through walls
  (`elysium.ent_beams`). Gizmo/beam picking rides the existing P2.3 crosshair picker (brush-body trace +
  nearest-origin logic ent) → the Entity Inspector's shared selection, so no separate cone picker is
  added. All draws compile out wherever `ENABLE_DRAW_DEBUG` is off; the window is `#if ENABLE_COG`; the
  verbs `#if !UE_BUILD_SHIPPING`. Editor target compiles + links clean. *Deps:* 1.5.
- [x] **2.5 Maps/Lights windows + `elysium.reload` + `UCheatManager` subclass** — two more custom
  Cog windows derive from `FElysiumCogWindow` under the `Elysium` F1 group, plus the reload hot loop
  and a cheat-manager home. **Maps** (`FElysiumCogWindow_Maps`): the exported-map list (each row a
  Travel button, current highlighted), a Reload button, and the current map's surface/collision/
  light/prop/entity counts + a per-phase load-timing table (`AElysiumMapActor::LoadPhases`, stamped
  by `LoadMap` in build order: World+collision / Skybox / Props / Environment / Lights / Entities /
  Total). **Lights** (`FElysiumCogWindow_Lights`): a rig-visibility toggle, the source-type breakdown
  (point/spot/tex/sun/animated), a clipper-paged per-source list (type / colour swatch + raw
  magnitude / live intensity + reach / lightstyle), and **live calibration sliders** — point/spot
  scale, max brightness, falloff exponent, reach scale, specular, sun lux — that re-derive every
  light in place via the new `UElysiumLightRig::ApplyLiveTuning()` (no map reload; the rig now keeps
  a per-light `FLightSource` record — raw magnitude / radius / fit / style — and folds the resolved
  boot `elysium.LightScale` back into `PointSpotScale` so the slider is truthful). **`elysium.reload`**
  (`UElysiumMapSubsystem::Reload`) re-`Travel`s the current map — the recook-free export→reload loop;
  the Maps window's Reload button flips the same seam. **`UElysiumCheatManager`** (a `UCheatManager`
  subclass hosted by the new `AElysiumPlayerController`, set as the game mode's `PlayerControllerClass`)
  adds the `Noclip` and `ElysiumTeleport <srcX srcY srcZ>` execs (autocompleted, player-centric) and
  carries the stock `UCheatManager` execs (God / Fly / Ghost / Slomo / …) for free; noclip routes
  through the pawn's new `SetNoclip`. The **Canvas HUD** is trimmed to the always-on FPS/position
  overlay (pose in metres + Source units + yaw, movement mode, sky/lights state) — the map/collision
  counts moved to the Maps + Status windows and "what am I aiming at" to the Entity Inspector's live
  crosshair. All windows `#if ENABLE_COG`; the cheat manager compiles only where cheats are enabled.
  Editor target compiles + links clean (full unity). *Deps:* 2.1.

**Slice acceptance:** in standalone — browse entities, pick the elevator call button through
the crosshair, hand-`ent_fire` its chain, watch beams + queue window, pause/single-step,
`elysium.reload` after a re-export without restarting. The P4 test harness exists before P4.

Deferred (tracked, not scheduled): dynamic console autocomplete of targetnames
(`UConsole::BuildRuntimeAutoCompleteList` via custom viewport client); Gameplay Debugger
category; Remote Control channel; NetImgui remote — see Options.

## P3 — Lighting correct & engaged *(parallel lane; detail: `rendering-perf.md`, `lighting.md`)*

Fix-the-frame tasks absorbed from the lighting roadmap (its "broken/missing" findings), plus
M1 leftovers that live in this lane.

- [ ] **3.1 Pin MegaLights per-light** *(was L1.1)* — set each light's **MegaLights Shadow
  Method** property (Ray Tracing is the default; VSM is the costly alternative) explicitly in
  `UElysiumLightRig::Build`. MegaLights is **Production-Ready in 5.8** (no longer
  Experimental). *Acceptance:* 0.2 shows 100% rig lights MegaLights-handled. *Deps:* 0.2.
- [ ] **3.2 Shadow curation** *(was L1.3)* — heuristic shadow-off for low-contribution lights
  + `elysium.ShadowMinIntensity` cvar. *Acceptance:* measurable ms drop, no hero-shadow loss.
  *Deps:* 0.1, 3.1.
- [ ] **3.3 Attenuation-radius audit** *(was L1.4)* — revisit `RadiusScale` /
  `FallbackRadiusCm=2500`; tighter reach, no black gaps. *Deps:* 0.1.
- [ ] **3.4 Texlight clustering** *(was L1.2 / M1.4)* — bin type-0 patches by
  `(intensity, normal)`, single-linkage cluster, one shadowless point per surface —
  **prefer exporter-side merge** in `UE_bsp_to_scene.py` (deterministic, free at runtime).
  Fixes N-fold over-lighting on non-tutorial maps. *Deps:* 0.3.
- [ ] **3.5 Sky IBL onto the SkyLight** *(was L2.1)* — assign the built cube,
  `RecaptureSky()`, keep a floor ambient (night skies are near-black). Unblocks reflections.
  *Deps:* none.
- [ ] **3.6 Pinned exposure** *(was L2.2)* — fixed EV/bias on the per-map post-process;
  deterministic LDR framing. *Deps:* none.
- [ ] **3.7 Grade/tonemapper fidelity** *(was L2.3 + M1 polish)* — stop the filmic curve
  crushing the `.cube` grade (neutralize tone curve or re-fit + `elysium.GradeIntensity`);
  **verify sky orientation** (face order/sign are derived, unverified); add
  `elysium.*` A/B toggles for sky/LUT/fog. *Deps:* 3.6.
- [ ] **3.8 Texture prewarm off the game thread** *(was M1.3)* — worker-thread batch in
  `FElysiumTextureCache` (load is texture-bound; Godot `Prewarm` shape). *Deps:* none.
- [ ] **3.9 Lightstyle clock pin + freeze** *(was L4.4)* — cvar-pinned phase + freeze toggle
  for reproducible A/B captures. *Deps:* none.

## P4 — Interaction *(design: `engine-core.md` class ladder + `animation_and_movers.md` Part B)*

- [x] **4.1 Mover base** — `FElysiumMoverBase` (the CBaseToggle primitive, `ElysiumMover.h/.cpp`):
  `LinearMove`/`AngularMove` drive the entity's `UElysiumBrushComponent` at **constant velocity**
  (no easing, B.4) toward a body-relative target, snapping + firing `MoveDone()` on arrival; motion
  runs on the substrate clock/think (R4 — no `FTimerManager`), re-arming `NextThink` each moving
  frame, and moves **swept** (`bSweep`) so the solid kinematic body pushes the pawn and reports
  blockers (the Chaos behaviour this task de-risks). Angular rotation pivots about the body's own
  origin = the hinge; `LinearMove` unit-converts Source in→cm (ready for 4.3's slide). Layered on it,
  `FElysiumDoorBase` (registered as the `CBaseDoor` chain node) is the CBaseDoor 4-state machine
  (`m_toggle_state` {AT_TOP=0, AT_BOTTOM=1, GOING_UP=2, GOING_DOWN=3}): inputs
  `Open`/`Close`/`Toggle`/`Lock`/`Unlock`/`Use`, outputs
  `OnOpen`/`OnClose`/`OnFullyOpen`/`OnFullyClosed`/`OnLockedUse`/`OnBlockedClosing`, `wait` autoclose
  (`-1` = stay open), the locked path (fires `OnLockedUse`, no move), Toggle in-flight reversal, and
  **blocked-while-closing → `dmg` (`ApplyDamage`) + reverse + `OnBlockedClosing`** (pawn-only
  blockers; opening into a blocker clamps + retries). `speed`/`distance`/`wait`/`lip`/`dmg` are
  chain fields on `CBaseDoor`. The prototype leaf `func_door_rotating` (the workhorse: 1236 uses / 22
  on the tutorial) swings `distance°` about yaw/Z around the hinge, `REVERSE`/`LOCKED`/`START_OPEN`
  honoured. Driven through the I/O inputs (`ent_fire <door> Open`/`Unlock`) until `+use` (4.4).
  *Runtime feel of the Chaos push/block still needs an in-game play test.* *Deps:* 1.5, 1.6.
- [x] **4.2 `func_button`** — `FElysiumButton` (`ElysiumMover.cpp`), a concrete `CBaseButton` over
  the `FElysiumMoverBase` primitive: the press → `TriggerAndWait` (fire `OnPressed`) → autoclose/
  spring-back **or** latch (`wait -1`) / toggle (re-use springs it back) cycle (B.4), on the
  substrate clock (R4). Spawnflags reconciled against the decompiled `CBaseButton::Spawn`
  (`FUN_100c8d60`) — **`0x100`=TOUCH, `0x400`=USE** (VtMB keeps the stock layout; the `0x400` handler
  gates on `PassesUseFilter`, and all six tutorial buttons are `1057` = `0x400`+use_icon), plus `0x1`
  DONTMOVE (the logical no-slide path every exported button uses; the physical LinearMove press-in
  lands but is best-effort/untested since no content clears DONTMOVE), `0x20` TOGGLE, `0x800` LOCKED.
  `Press`/`Lock`/`Unlock` inputs drive it from `ent_fire`. The **minimal +use look-cursor** also lands
  (`FElysiumEntityWorld::UpdateUseCursor`/`PlayerUse`, driven from the map-actor tick + the pawn's `E`
  key): a per-frame camera-ray pick against the usable (`0x400`, non-inert) brush bodies in reach fires
  `OnIn`/`OnOut` on aim enter/leave and presses the aimed button — so `StartHidden`→`ScriptUnhide`
  arm/disarm gates the reticle + collision together for free. The full use-only trace channel + use-icon
  HUD are 4.4; this lands activation + `OnIn`/`OnOut`. Fixed the `entity_io.md` `0x100`/`0x400` label
  swap in the same pass. *Deps:* 4.1.
- [x] **4.3 `func_door` / `func_door_rotating`** — the sliding leaf `FElysiumFuncDoor`
  (`ElysiumMover.cpp`) completes the CBaseDoor family: a second leaf over the same 4-state machine
  that translates along `angles` (Source `SetMovedir` → Unreal, `SourceAnglesToUnrealDir`) by its own
  depth in that direction minus `lip` — open pose `pos + movedir·(|size·movedir| − lip)`, matching the
  decompiled `CBaseDoor::Spawn` (`FUN_100ef260`); size comes from the def hulls (the brush body isn't
  built until after `Spawn()`), `speed` is in/s (in→cm), driven by `LinearMove` (swept, pushes the
  pawn). The **full spawnflag table (B.5)** is now decoded and honoured: `START_OPEN`/`REVERSE`/`LOCKED`
  (were live) + **`NO_AUTO_RETURN` (0x20)** (stay open, folded into `StaysOpen()` alongside `wait -1`)
  + **`PUSE` (0x100)** — the dominant door bit (105 doors) arms the +use look-cursor via `IsUsable()`,
  and a use runs the **doorknob path** `DoorUse()`: toggle this leaf (locked → `OnLockedUse`) and, if
  `linked_door` is set, the paired leaf too (the double-door swing) — driven by the P4.2 cursor / the
  pawn E key / `ent_fire <door> Use`. **`linked_door`** (485 uses) resolves the partner through the
  name index (no-RTTI `AsDoorBase()` downcast) and mirrors `Use` onto it (movement I/O is wired to both
  leaves by the map data; the runtime link is the doorknob, per the decompile where `m_hLinkedDoor` is
  otherwise only consulted in `IsCloseBlocked`). `PASSABLE`/`ONEWAY`/`NONPCS`/`SILENT`/`USE_CLOSES`/
  `PTOUCH` are decoded + labelled but deferred (no exported door uses `PASSABLE`; NPCs, audio, and the
  alt autoclose land later). **Debug layer:** movers surface runtime state via a new
  `FElysiumEntity::GetDebugState` hook (toggle state, current move, poses, resolved link, spawnflag
  decode for doors; press state + latch/flags for buttons) rendered in the Cog Inspector's **Live
  state** section; mover outputs already thread the Event Queue window. Verified headless on
  `sp_tutorial_1`: `Use` on `tutwareelevdra` slid both it and its `linked_door` partner
  `tutwareelevdrb` open together (both `OnFullyOpen` at t=1.0 → `elev_button.Unlock()`). *Deps:* 4.1.
- [x] **4.4 `+use` verb + use-icon HUD** — the look-cursor now runs on a **dedicated use-only
  trace channel** (`ELYSIUM_USE_CHANNEL` = `ECC_GameTraceChannel1` = "ElysiumUse", declared in
  `Config/DefaultEngine.ini` with a default-Block response so world + solid brush bodies occlude the
  ray while it stays isolated from the `ECC_Visibility` channel the crosshair inspector uses);
  `UpdateUseCursor` traces it and gates the hit on `IsUsable`. `E` → `Use` was already wired (4.2);
  this lands the **context-icon HUD**: `use_icon`/`locked_icon` are parsed onto the base entity
  (`FElysiumEntity::UseIcon`/`LockedIcon`, registered base fields), `GetUseIcon()` resolves
  locked → `locked_icon` (VtMB `GetUseIcon` = `FUN_100c8940`; `IsUseLocked()` overridden by
  door/button off `bLocked`), and `AElysiumHUD` draws the ring frame + the entity's icon cell over
  the crosshair (else the plain aim cross) from the PL3 atlas (`out/hud/use_icons.png` + `.json`,
  loaded once lazily; per-icon UVs from the JSON, `FCanvasTileItem` translucent). The **72-entry
  enum** name table lives in `ElysiumUseIcons.h` (shared with the debug layer). The Cog **Entity
  Inspector** grows a `+use` section (usable/locked, `use_icon`/`locked_icon` with names, the
  resolved reticle icon = exactly what the HUD draws, and whether the look-cursor is on this entity),
  so `use_icon` state is inspectable per-entity while playing. *Pipeline:* **PL3 use-icon atlas
  export** (done). *Deps:* 4.2, PL3.
- [x] **4.5 Tutorial logic classes** — the tutorial-histogram logic/point/brush + trigger classes
  land as plain-C++ leaves (`ElysiumLogicClasses.cpp` + three trigger leaves in
  `ElysiumStarterClasses.cpp`), each grounded in the decompiled `vampire.dll` factories/datamaps
  (via `tools/ghidra/run.ps1 -Script DumpGrep`): **`math_counter`** (`FUN_10133350`, stock —
  Add/Subtract/Multiply/Divide/SetValue/SetValueNoFire/SetHitMax/SetHitMin/GetValue, clamps to
  `[min,max]` only when a bound is set, edge-fires OnHitMax/OnHitMin, and its `OutValue` carries the
  value), **`logic_timer`** (`FUN_10131390`, stock — fires `OnTimer` every `RefireTime` on the
  substrate clock, `UseRandomTime` band, Enable/Disable/Toggle/FireTimer), **`logic_case`** (stock
  value-match InValue→OnCaseNN/OnDefault + PickRandom) **and `logic_case_toggle`** (the VtMB
  divergence `FUN_101344f0`/`FUN_101346e0`: InValue is a *delta* that advances a current-case pointer
  that many **configured** cases — skipping empty slots, wrapping 0..15 — then fires that case;
  `InitialCase` seeds the pointer; the 4-byte-larger class carries the extra current-index int),
  **`env_fade`** (`FUN_10100e10` — the `Fade`/`ReverseFade` full-screen colour fade, `SF_FADE_IN`
  reveal + `SF_FADE_STAYOUT` hold-covered, rendered by `AElysiumHUD` off a single screen-fade state on
  the entity world), **`func_brush`** (`FUN_1013dd30` — Enable/Disable/Toggle + `Solidity`
  never/always/toggle, unified with dormancy through the body's one `SetDormant` switch;
  ScriptHide/ScriptUnhide/Kill were already the base path the tutorial wires), **`point_teleport`**
  (`FUN_1018d940` — `Teleport` moves `!player` to origin + `angles` yaw via the world's `GetPlayerPawn`
  seam, capsule-lifted), and the trigger family over `CBaseTrigger`: **`trigger_hurt`**
  (`FUN_101c5c30` — `damage` to the player every 0.5 s while stood in it, `ApplyDamage`),
  **`trigger_look`** (`FUN_101c6b00` — fires `OnTrigger` once the player looks at `target` within
  `FieldOfView` for `LookTime` cumulative seconds, self-contained off the pawn camera), and
  **`trigger_autosave`** (checkpoint volume — logs + fires once; the actual save is P10). A Source
  `COutput<T>` value seam lands with it: `FElysiumEntity::FireOutput(name, activator, value)` fills any
  wire whose map-param is empty (math_counter → logic_case_toggle passes the value/delta), else the
  authored param wins. **Debug:** every class implements `GetDebugState` (surfaced in the Cog Entity
  Inspector's Live state) and a new **`Elysium.Logic` Cog window** boards them all — live values/state
  per class, an Inspect button, a quick-fire of each primary input, and the current env_fade
  screen-fade the HUD is drawing. Deferred as unbuilt-system stubs (left inert records):
  `trigger_stealth_mod`, `trigger_inventory_check`, `trigger_environmental_audio` (need stealth /
  inventory / RoomDSP). **RE1** (trigger spawnflag filters) already `[x]`. *Deps:* 1.6.
- [ ] **4.6 `trigger_changelevel` + landmark travel** — `Travel(map, landmark)` finally uses
  the landmark: new pos = dest landmark + (player − src landmark); `point_teleport` and
  scripted `ChangeNow` ride the same paths. *Deps:* 1.6, 0.3.
- [ ] **4.7 Source movement component** *(was M1.1; parallel-capable)* — port `CGameMovement`
  friction/accel/airaccel/StepMove into a `UCharacterMovementComponent` override
  (`source_movement.md`, Godot `SourceMovement.cs`). *Deps:* none.
- [ ] **4.8 Rotating/linear/elevator family** — `func_rotating` (spin-up/down, hurt-touch),
  `func_movelinear`, `func_elevator` (`GotoFloor`, floor Z table), keyframed movers if the
  tutorial needs them. *Deps:* 4.1.

**Slice acceptance** *(M3 criterion)*: the tutorial elevator chain works — button →
`Unlock`/`Trigger` → doors open → `thug_2` `ScriptUnhide` — and walking out of the tutorial
loads `sm_pawnshop_1` at the landmark.

## P5 — Scripting foundation *(design: `python_bridge.md`, `rebuild-strategy.md` B6)*

- [x] **5.1 PL2: copy scripts + dialogue** — **`UE_extract_scripts.py`** copies the loose
  plain-text `python/**/*.py` (41: 35 patch + 6 retail-only; the 24 VPK `.pyc` are stale/
  unreachable and ignored) → `out/scripts/` and the `dlg/**/*.dlg` (147; patch loose fully
  overlays the 138 VPK) → `out/dlg/`, verbatim, patch-first (patch loose > retail loose >
  VPK). Whole-game (not map-scoped), so it runs once at the end of `export_all.py`
  (`--no-scripts` to skip), even on a zero-map run; re-copies are cached. Debug: the read-only
  **Cog `Elysium.Scripting` window** (`FElysiumCogWindow_Scripting`) confirms the mirror is on
  disk (`.py`/`.dlg` counts) and resolves the current map's `worldspawn.levelscript` value to
  its module file (`scripts/<m>/<m>.py`, present/missing) — a delivery pre-flight; the runtime
  G-store/event state grows into it at 5.2+. *Deps:* none.
- [x] **5.2 Expression evaluator** — **`ElysiumExpr`** is the runtime subset interpreter: a
  self-contained lexer + recursive-descent parser (Python precedence: or/and/not/compare/bitwise/
  shift/arith/unary/`**`/trailer) + AST + tree-walk over the restricted Python-2.1 expression
  subset, with **no C++ exceptions** — every failure (parse / NameError / AttributeError / type /
  ÷0) collapses to Void = **error-to-false** (RE3). Values: int/float/str/`None`(→Void) literals,
  `+ - * / % | & ^ << >> **` (Python-2 floor int div/mod, string `+`/single-arg `%`), chained
  comparisons, short-circuit `and`/`or` (returns the operand), `not`. Binding surface (**one
  namespace, no new dispatch**): `G.<flag>` reads the game-state bag (default-0) and `G.<flag> =
  <expr>` writes it (`None`/Void deletes, tp_setattr); `G.has_key`/`keys`/`ClearAll`; a bare name
  resolves to an entity by targetname → `<ent>.<input>` manufactures a bound callable that fires
  the input through the real chokepoints (`EnqueueInput "!self"`, so exactly that entity),
  `<ent>.<field>` reads/writes the P1 class-chain field table (keyable-gated). Native module
  globals (FindPlayer…) + level-script names are unresolved here (NameError) — 5.3/5.5.
  `FElysiumExprScriptHost` implements the B6 seam via `ElysiumExpr::Exec` (field-6 is a statement);
  **opt-in** behind `elysium.script.live` / the Cog checkbox (the null host stays the map-load
  default — 5.4 makes it default + adds logic_pythoncheck/ScheduleTask). Debug: the
  **`Elysium.Scripting` Cog window** grows a live `G` table (filter / poke / add / clear), an
  eval/exec input box, and a recent-eval log; `elysium.eval` / `elysium.exec` /
  `elysium.script.live` console verbs echo it; `UElysiumGameStateSubsystem` owns the eval-record
  ring + `EvalScript`. *Deps:* 1.3, 1.4.
- [x] **5.3 Native bindings** — the engine `vampire`-module surface in `ElysiumExpr`: the **11
  module globals** resolve as bare names (ahead of targetnames) and the **24 Character methods**
  dispatch off a character object. `FindPlayer()` returns the PC (a `Character` FVal, no player
  entity yet); `FindEntityByName` resolves a targetname to a handle; `SetQuest`/`GetQuestState`
  route to the real quest map; everything else **logs a stub and returns a plausible default**
  (predicate-shaped globals read false). An **unlisted** method still binds off the character object
  so `FindPlayer().ClearActiveDisciplines()` dispatches to the generic stub rather than raising
  (matching retail's forgiving `__getattr__`); a Character method also binds off an NPC entity handle
  (`FindEntityByName("bob").SetExpression(...)`). `self`/`activator` resolve to the eval's I/O
  provenance when bound (below the module globals, so nothing real is shadowed — no exported field-6
  actually references them, they are I/O *targets*). **Case-insensitive stat names** normalise through
  `CanonicalStatName` (BumpStat/CalcFeat). One static `GNativeBindings` table is the single source of
  truth (membership tests + the debug view). Debug: the `Elysium.Scripting` Cog window grows a
  **Native bindings** table (name / kind / backing status / live call-count) and a **Recent native
  calls** log; `UElysiumGameStateSubsystem` owns the native-call ring (`RecordNativeCall` +
  per-name counters). `ScheduleTask` (deferred source = 5.4) and `ChangeMap` (travel = P4) stay
  logged stubs by design. *Deps:* 5.2.
- [x] **5.4 Field-6 + `logic_pythoncheck` + `ScheduleTask` live** — `FElysiumExprScriptHost` is now
  the **map-load default** (`UElysiumGameStateSubsystem::Initialize`), so field-6 payloads run live
  the moment a map loads — on the tutorial the `G.x = ...` outputs (Tut_Officedoor, Tut_Elev, …) flip
  visibly, while the level-script names they sometimes reference (`cCelerity`, `DialogPostProcess()`)
  stay NameError → error-to-false until 5.5. `elysium.script.live 0` swaps in the null host for A/B —
  the **whole** scripting surface (field-6 + pythoncheck + ScheduleTask) goes dark together, not just
  field-6. **`logic_pythoncheck`** lands as a leaf class (`ElysiumStarterClasses.cpp`): its
  `python_script` keyfield is an expression, the **`Test`** input evaluates it through the world's new
  `FElysiumEntityWorld::EvalCondition` (via the installed host, so it obeys the same on/off switch and
  lands in the eval log) and fires **`OnTrue`**/`OnFalse` on the result's truthiness — Void (no host /
  raise / unresolved name) reads `OnFalse` (retail's `Py_eval_input`-seeded-0 error-to-false); the
  incoming activator is propagated onto the branch. **`ScheduleTask(delay, "<source>")`** is real (no
  longer a stub): `FElysiumEntityWorld::EnqueuePython` posts a **python-only event** (no I/O target,
  just the source string) onto the one event queue at `now+delay`, delivered through the same
  `DeliverEvent` Python half — so a deferred task single-steps in the Event Queue window and serializes
  into a save (R8) like every other queue entry. Sources still resolve against 5.3's binding surface,
  so the `__main__.`-prefixed forms the retail scripts use NameError until 5.5, but the mechanism is
  demonstrable today (`elysium.exec ScheduleTask(1.0, "G.Foo = 42")` flips `G.Foo` a second later).
  `AddSubclassField` grows FString support for the new keyfield. Debug: the Event Queue window's last
  column shows the deferred Python source verbatim (a `(python)` Target row); the Scripting window's
  **Live script eval** checkbox (renamed from "Live field-6", on by default) toggles the whole surface,
  the Native bindings table shows `ScheduleTask` non-stub with a live call count, and a pythoncheck's
  `python_script` + last `Test` outcome surface in the Cog Inspector's Live-state via `GetDebugState`.
  *Deps:* 5.2, 5.3.
- [x] **5.5 Level-script decision — embed CPython 2.x (option c) + PoC.** Surveyed all 36 loose
  scripts (16,473 lines: 1,119 defs, 45 classes, try/except, imports, exec — full Python 2.1, past
  ElysiumExpr) and confirmed the 2.1→2.7 delta is ~0 (no string-exceptions, no `__future__`).
  **Chose `qnox/python-2.7` 2.7.18** (active Jan 2026; x64 MSVC / VS2022 build, headers+lib+dll+stdlib).
  PoC pulled forward from 9.3: vendored SDK + `Build.cs` wiring, `FElysiumPythonVM` (Py_Initialize +
  `vampire` C-module with `G` proxied onto `UElysiumGameStateSubsystem` + Python-stubbed natives),
  `FElysiumCPythonScriptHost` (`elysium.script.cpython`), `elysium.py.*` verbs, and a CPython panel in
  the `Elysium.Scripting` Cog window. Offline-validated against real `tutorial.py` (imports; callbacks
  run; `G.Tutorial_Discflags |= cCelerity` flips G to 8). C++ compiles; full in-editor run pends the
  P8 glTFRuntime build fix. Details + remainder in the decision log. *Deps:* 5.1, 5.2. → **9.3.**

**Slice acceptance:** the tutorial's field-6 calls and `logic_pythoncheck` gates actually
execute (e.g. `FindPlayer().ClearActiveDisciplines()` runs, `OnTrue`/`OnFalse` fire);
`G` flags flip visibly in the debug layer.

## P6 — Audio foundation *(design: `audio_pipeline.md`; parallel with P5/P7)*

- [x] **6.1 MS-ADPCM decode** — runtime WAV decode in C++, no offline transcode. Vendored
  single-header **`dr_wav`** (`Source/ElysiumUE/Private/ThirdParty/dr_wav.h`, public domain;
  emitted once in `ElysiumDrWav.cpp` under `THIRD_PARTY_INCLUDES`; establishes the module's
  ThirdParty-include convention) decodes MS-ADPCM (0x02), IMA (0x11) and PCM16 to interleaved
  int16. `FElysiumSoundCache` (mirrors `FElysiumTextureCache`) caches the decoded PCM + metadata
  path-keyed; `FElysiumSoundCache::MakeWave` mints a **`USoundWaveProcedural`** per play (the
  `RawPCMData` path regressed in UE 5.5+/packaged builds — the queue-fed procedural is the
  reliable 5.8 route; looping is 6.3's re-queue-on-underflow). `UElysiumAudioSubsystem` (GI-scope)
  owns the decode registry + `PreviewSound2D` (SpawnSound2D + retained component, auto-stopped on
  drain) + the `elysium.playsound`/`elysium.sound_info` verbs. Debug: **Cog `Elysium.Audio` window**
  (`FElysiumCogWindow_Audio`) — path input + Play, this map's `ambient_generic` refs one-click, and
  a live decode-metadata table (format tag / ch / rate / bits / frames / duration / decode ms /
  errors) with the MS-ADPCM/IMA/PCM mix. Offline delivery: **`UE_extract_sounds.py`** verbatim-copies
  the WAVs each map's `.ents` reference (patch-first) into a shared `out/sound/` mirror — no transcode
  — wired into `export_all.py` (`--no-sound` to skip); verified on `sp_tutorial_1` (47 WAVs: 45
  MS-ADPCM, 2 PCM). Ghidra: the vampire.dll `CSoundScheme` parser + `ambient_soundscheme` are pinned
  (`audio_pipeline.md §12`) for 6.3. *Deps:* none.
- [x] **6.2 MP3 decode** — dialogue/music/radio path. Vendored single-header **`dr_mp3`**
  (`Source/ElysiumUE/Private/ThirdParty/dr_mp3.h`, public domain, MP3 patents expired — no
  licensing exposure; emitted once in `ElysiumDrMp3.cpp` under `THIRD_PARTY_INCLUDES`, the same
  isolation convention as `dr_wav`) decodes the loose `.mp3`s to interleaved int16. Unreal has **no
  runtime path for loose MP3s** (its MP3 story is editor-only cooked `USoundWave` assets, and this
  project bakes nothing), so MP3 takes the exact same self-decode-to-PCM route as 6.1's WAV:
  `FElysiumSoundCache` now dispatches on file extension (`LoadSoundDecoded` → `DecodeWav`/`DecodeMp3`
  → one `FDecoded` of int16 PCM + `FElysiumSoundInfo{Codec}`), so the whole subsystem/verbs/window
  above it are codec-agnostic. MP3 is a two-pass decode (`drmp3_get_pcm_frame_count` off the Xing/Info
  tag → `seek(0)` → `drmp3_read_pcm_frames_s16`). The **Cog `Elysium.Audio` window** + the
  `elysium.playsound`/`elysium.sound_info` verbs handle MP3 verbatim (Format column shows `MP3`, the
  session summary counts an MP3 bucket, ambient_generic ref-gathering takes `.mp3` too). Offline:
  `UE_extract_sounds.py` now mirrors any `.wav`/`.mp3` an `.ents` references (`collect_audio_refs`),
  plus a `--radio` opt-in that discovers + copies the loose `sound/radio/*.mp3` loops as canonical MP3
  decode test material (no map references them; dialogue MP3s arrive with 9.2, music/scheme MP3s with
  6.3/PL5a). Verified on `radio/radio_loop_5.mp3` (MPEG-1 L3, mono 44.1 kHz, ~619 s). No Ghidra RE
  needed: VtMB's MP3s are standard streams (Miles decoded them — `audio_pipeline.md §2`
  `CAudioSourceMP3`); the RE that matters (music state machine, streaming/loop) is 6.3/P9. *Deps:* none.
- [x] **6.3 `ambient_generic` + SoundSchemes** — entity-driven ambience + `sound/schemes/*.txt`.
  The audio subsystem grows a **voice pool** (`FElysiumPlayParams` → `PlayVoice`/`StopVoice`/
  `SetVoiceVolume`/`SetVoicePitch` over a `UAudioComponent` per voice, reaped in `TickAudio` from the
  map actor): 3D sphere attenuation (`NaturalSound`, radius→cm), non-spatialized beds, `SourceEntityName`
  attach-to-mover, built-in fades, and **looping** via `MakeWave(bLoop)` binding
  `OnSoundWaveProceduralUnderflow` to re-queue the cached PCM (the 6.1 hand-off). **`ambient_generic`**
  (`ElysiumAmbientGeneric.cpp`, 66 on the tutorial) is a leaf class: `message`/`health`(vol 0–10)/`radius`/
  `pitch`/`SourceEntityName` + the Source spawnflags (0x1 everywhere / 0x10 start-silent / 0x20 not-looped),
  wiring `PlaySound`/`StopSound`/`ToggleSound`/`Volume`/`FadeIn`/`FadeOut`; the initial play defers one
  think so mover bodies exist. **SoundSchemes** (`ElysiumSoundScheme.h/.cpp`): a runtime KeyValues scheme
  parser (field set + retail defaults from the decompiled `CSoundScheme` @0x1022a930), the
  **`ambient_soundscheme`** entity (`start_enabled` + `FadeIn`/`FadeOut`/`Disable` crossfade), and
  `FElysiumSoundSchemeManager` (owned by `AElysiumMapActor`, ticked with the listener pos): looping ambient
  bed, the **music state machine** (explore/combat/alert stems started phase-locked, volume-crossfaded on
  `EElysiumMusicState` — cvar/Cog-driven until combat scoring lands in P9), and the **polar RandomSound
  scheduler** (per-sound Frequency cadence, `RandomSoundCount` concurrency cap, DistMin/Max·Height·Angle
  placement around the anchor). Debug: **Cog `Elysium.Sound Schemes`** (active scheme + stems + randoms +
  music-state buttons + per-anchor FadeIn/FadeOut) and the **Audio window's live-voices table**. Music-stem
  crossfade timing/DSP-room reverb are documented deferrals (client.dll music-state RE is landmark/soundscape
  level; RoomDSP submixes are P-later). **PL5a: scheme file copies** — `UE_extract_sounds.py` now mirrors each
  map's `ambient_soundscheme` `.txt` verbatim into `out/sound/Schemes/` and folds their `Filename` music/
  ambient/random assets into the copy set (KeyValues-parsed via `kv.py`). *Deps:* 1.6, 6.1.
- [x] **6.4 Mover sounds** — door/button `soundgroup` sets + button explicit WAVs, through the 6.3
  voice pool. **RE finding (Ghidra, `vampire.dll`):** a `soundgroup` token has **no data file** — it
  resolves *by directory convention* to `sound/usable/<category>/<token>/<subkey>.wav` (verified: an
  exhaustive scan of all 67k install files finds the tokens only inside `.bsp` entity lumps; VtMB's
  soundscript system is commented-out). `CBaseDoor::Spawn` (`FUN_100ef060`) resolves the door subkeys
  **`open`/`close`/`swing`/`locked`** (category `openable`); `CBaseButton::Spawn` (`FUN_100c8810`)
  resolves **`on`/`off`** (category `switches`); `FUN_100ee4e0` gates all of them on the SILENT
  spawnflag `0x1000` and routes `swing` to the movement channel; the locked sound plays on the locked
  `+use` path (`CBaseDoor::Use` `FUN_100efc90`). **Offline:** `UE_extract_sounds.py` mirrors each
  referenced soundgroup's `usable/…` WAVs and writes `out/sound/usable/soundgroups.json`
  (`{category:{group:{subkey:relpath}}}`); explicit button `locked_sound`/`unlocked_sound` are direct
  WAV paths already copied by the ordinary ref path. **Runtime** (`ElysiumMover.cpp`): `FElysiumMoverBase`
  loads the manifest once (`ElysiumMoverSoundManifest`), and the door/button state machines play through
  the GI voice pool at the body — door `open`/`close` one-shots on motion start with the looping `swing`
  moving sound (stopped on arrival), `locked` on the locked path; button `on` on press (explicit
  `unlocked_sound` overrides), `off` on spring-back, `locked_sound` on a locked press. **Debug:** the
  Cog **Entity Inspector**'s Live-state shows each mover's soundgroup/category/SILENT/resolved subkeys/
  last-played (via `GetDebugState`), the **Audio window** grows a **Mover soundgroups** browser (the
  manifest as a per-category tree, Play any subkey 2D) and the live-voices table shows the mover voices
  as they play. *Deps:* 4.1–4.3, 6.1.

## P7 — Dressing & parity *(Track A completion; parallel lane)*

- [ ] **7.1 Coronas** *(was L3.3 / M2)* — `.sprites` consumer: additive depth-tested
  billboards (Godot `CoronaField.cs`), StartOff spawnflag filtering. *Deps:* 0.4.
- [ ] **7.2 Decals** *(M2)* — `_decals.obj` as translucent PMC sections (parity) →
  `UDecalComponent` projection upgrade later (tracked as one task, two stages). *Deps:* 0.4.
- [ ] **7.3 Water** *(M2)* — `.water` → `M_Water` Single Layer Water + Lumen reflections
  (no mirror cameras). Known engine facts: Lumen reflections on Single Layer Water are
  **forced mirror** (roughness only scales brightness — acceptable for VtMB's mirror-like
  water), and **MegaLights does not light water surfaces** — verify the water direct-lighting
  path during this task. *Deps:* 0.4.
- [ ] **7.4 Master-material set (rest)** *(was M1.2)* — `M_World_Masked`, `M_World_Translucent`,
  grow `M_VtMB_World` → `M_World_Opaque` (bump, envmap mask+cube, WVT + vertex-color blend),
  `M_Additive`, `M_Decal`; authored offline like `M_Sky`. *Deps:* none.
- [ ] **7.5 Real reflections** *(was L3.1)* — Lumen + roughness/reflection channel for
  `$envmap` surfaces. *Deps:* 3.5, 7.4.
- [ ] **7.6 Bloom/glow tuning** *(was L3.2)* — VtMB's overbright neon/selfillum vs pinned
  exposure. *Deps:* 3.6.
- [ ] **7.7 Shadow quality** *(was L3.4)* — contact shadows on hero lights, penumbra softness,
  within 0.1 budget. *Deps:* 0.1, 3.1, 3.2.
- [ ] **7.8 A/B capture harness** *(was L5.1)* — scripted fixed-camera screenshots vs Godot
  reference frames on tutorial + hubs. *Deps:* 0.3, 3.9.

**Slice acceptance** *(Track A criterion)*: side-by-side A/B match with the Godot viewer on
`sp_tutorial_1` + hub maps.

## P8 — Characters & menu *(design: `rebuild-strategy.md` B5, `m0_menu_build.md`)*

- [x] **8.1 PL1: entity-model export** — `UE_bsp_to_scene.py` decodes every `.ents`
  entity carrying a static `.mdl` `model` key (prop_dynamic/prop_physics + the
  prop_button/prop_doorknob(_electronic)/prop_sign/prop_switch/prop_hacking/item_container
  family; skeletal `npc_*` excluded — glTFRuntime track 8.2/8.5) into the shared `props/`
  dir (one decode per unique model, shared with the GAME_LUMP static props), and annotates
  each entity in `<map>.ents` with `model_mesh` = the decoded OBJ stem. Tutorial: 160 props
  / 64 models, all decoded. Runtime consumption is 8.3/8.4. *Deps:* none.
- [~] **8.2 glTFRuntime adoption spike** — vendor the plugin, load the one exported test
  NPC `.glb` (mesh+skeleton+anim) at runtime; **decision point** on the skeletal path.
  Upstream is actively maintained (Jan 2026 release supports UE 5.7; 5.8 not yet listed —
  the maintainer historically follows new engine versions within weeks) and runtime skeletal
  mesh + animation loading is a core documented feature. *Deps:* none.
  **Implemented (compiles + links on UE 5.8; pending an in-editor visual check):** `rdeioris/glTFRuntime`
  (MIT source, GitHub master, no engine-version lock) is vendored under `Plugins/glTFRuntime`
  and added to the `.uproject` + `ElysiumUE.Build.cs` (`glTFRuntime` runtime module, all
  configs). The test asset `out/npc/gangmember_male_2.glb` (69 bones, 9 materials, 6137 tris,
  clip `patron_barstand`) is produced by `mdl_gltf.py` — which already emits **standard glTF
  2.0**, so glTFRuntime does the glTF→UE basis/scale change itself (`FglTFRuntimeConfig`
  defaults: `SceneScale 100` m→cm, `TransformBaseType::Default`, `bAllowExternalFiles` resolves
  the sibling `tex/*.png`) and **no `UE_` pre-conversion is needed** (the raw-OBJ path's rule
  is for dumb containers; a self-describing one the loader can reorient is exempt). The runtime
  path is `UElysiumNpcSubsystem` (GI-scoped): `glTFLoadAssetFromFilename` → `LoadSkeletalMesh(0,0)`
  → `LoadSkeletalAnimation`/`…ByName` → spawn an `AActor` + `USkeletalMeshComponent` at the
  player's feet playing the clip single-node, driven by `elysium.npc.load [stem] [anim]` /
  `elysium.npc.clear` / `elysium.npc.list` and the Cog **`Elysium.NPC`** window (pick a glb,
  choose a clip, Load; a table of bone-count/anims/applied-clip/load-ms/spawn-location + per-clip
  re-play). `export_all.py --npc` regenerates the test glb. **Decision (provisional, to confirm
  on the visual check):** adopt glTFRuntime for the NPC track — a self-describing glTF round-trips
  through the existing offline bake with zero runtime coordinate code, and mesh+skeleton+one-anim
  loads with the plugin's default config. Remaining risk lives in **8.5/PL4**, not the plugin:
  include-model resolution (shared animation banks) and multi-sequence merge. *Deps:* none.
- [ ] **8.3 Dynamic props** — `prop_dynamic` from records with `Skin`/`SetAnimation`/`Break`
  inputs (the ISM path grows per-instance addressability per `entity_visuals.md` R2).
  *Deps:* 8.1, 1.3.
- [ ] **8.4 Physics props** — `prop_physics` ×54 / `phys_hinge` ×12 as Chaos bodies +
  constraints, convex from render mesh. *Deps:* 8.1.
- [ ] **8.5 NPC presence + `scripted_sequence` minimal** — spawn `npc_*`/`npc_maker` at
  origins via glTFRuntime; play-anim-at-marker handler (×51) long before real AI.
  *Pipeline:* **PL4 batch NPC export + `mdl_skel.py` include-model resolution** (shared
  animation banks). *Deps:* 8.2, PL4.
- [ ] **8.6 VGUI menu + New Game flow** — Slate/UMG port of the complete Godot
  implementation (`KeyValues`/`VguiScheme`/`VguiFont`/`.res` parsers, 640×480 scale box).
  *Deps:* none.
- [ ] **8.7 Ropes** — `keyframe_rope`/`move_rope` (×107 in tutorial) → Cable Components.
  *Deps:* none.

## P9 — Dialogue & persistence *(design: `game_runtime.md`, `rebuild-strategy.md` B7/B9)*

- [ ] **9.1 `.dlg` parser + dlgexpr** — 13-field CRLF Latin-1 parser + branch machine; the
  dlgexpr grammar on the 5.2 evaluator; **error-to-false** on the 89 malformed retail
  snippets (RE3 confirms; implement as documented regardless). *Deps:* 5.2.
- [ ] **9.2 Conversation UI + audio-by-path** — UMG dialogue screen, line audio via 6.2.
  *Deps:* 9.1, 6.2, 8.6.
- [ ] **9.3 Level-script execution** — implement the 5.5 decision; `ScheduleTask` strings
  must evaluate at arbitrary later times against a live `__main__`-equivalent namespace.
  *Deps:* 5.5.
- [ ] **9.4 Quests/XP + RPG sheet data** — quest map is live since 1.1; load
  `vdata/system/*.txt` (**PL5b copies**) into the sheet; XP awards. *Deps:* 1.1.
- [ ] **9.5 Save/load** — the four blocks (entity save-fields via the field tables, event
  queue incl. deferred strings, think times, `G` blob) into a `USaveGame` container.
  *Deps:* 1.4, 5.4.
- [ ] **9.6 Dice resolver + golden test** *(RE5)* — port in `recovered/dice-system.md` is
  unverified; run the vroll golden test against the running game before it becomes
  canonical. *Deps:* none (test needs retail game).

**Slice acceptance** *(M6 criterion)*: `sp_tutorial_1` is completable as in retail —
dialogue, scripted flow, quests, save/load included.

## P10 — Scale & ship-shape *(ongoing after P4)*

- [ ] **10.1 Horizontal scale-out** — export all ~100 maps; per-map light calibration
  (`probe_light_calibration.py` / `.lightfit`; was L5.2); fix decoder edge cases as maps
  surface them. *Deps:* P4 done (travel), P3 done (calibration meaningful).
- [ ] **10.2 Perf deepening** — Lumen tuning ladder (was L4.1), light culling/max-influence
  cap (was L4.2), scale-up scalability tier for 4070+ (was L4.3). *Deps:* 0.1, P3.
- [ ] **10.3 Floor validation `[needs 3060]`** *(was L5.3)* — 1080p/60 on a real RTX
  3060-class card; the one gate look can't judge. *Deps:* P3, 10.2.
- [ ] **10.4 Async travel state machine** — `map-architecture.md` design (fade → unload →
  task-thread parse → spawn → fade in); **trigger: when synchronous hitches start to
  matter, not before.** *Deps:* 4.6.
- [ ] **10.5 Packaged-build content path** — `content/` next to the exe, packaging story,
  Shipping config sweep (debug layer compiled out). *Deps:* none until first package.
- [ ] **10.6 EnhancedInput decision** — configured but unused; migrate the legacy mappings
  or remove the plugin. *Deps:* none.
- [P] **10.7 Long tail** *(post-tutorial; promote to tasks when reached)* — combat + full
  RPG sheet + chargen; real NPC AI (runtime NavMesh + BT/StateTree replacing `info_node`);
  ragdoll/IK/anim blends; MetaSounds; `.emc`-style cache for `.ents` if parse time bites;
  lump-8 lighting bake as a low-end contingency (parked with the dynamic-path commitment);
  retail `.sav` import (needs RE7 wire format — currently a non-goal). For the low-end
  contingency, **Lumen Lite** (5.8's medium-quality irradiance-field GI, ~2× faster, runs
  on PC) is noted as a cheaper alternative to a lump-8 bake path — see Options.

## Pipeline backlog (indexed; owned by phases above)

| ID | Task | Needed by |
|---|---|---|
| PL1 | Export `.ents`-referenced models (`prop_dynamic`/`prop_physics`) — `model_mesh` in `.ents` | 8.1 [x] |
| PL2 | Copy loose `.py` → `out/scripts/`, `.dlg` → `out/dlg/` — `UE_extract_scripts.py` | 5.1 [x] |
| PL3 | Use-icon atlas export (72-entry enum) — `UE_use_icons.py` → `out/hud/use_icons.png`+`.json` | 4.4 [x] |
| PL4 | Batch NPC export + include-model resolution in `mdl_skel.py` | 8.5 |
| PL5 | Copy sound schemes (a) [x] + `vdata/system/*.txt` (b) | 6.3, 9.4 |
| PL6 | Texlight merge in exporter | 3.4 |
| PL7 | Sidecar space fixes surfaced by the audit — **none (0.4: all sidecars already Unreal cm)** | 0.4 [x] |

## RE backlog (reverse-engineering work; each cited where consumed)

| ID | Question | Consumed by | Status |
|---|---|---|---|
| RE1 | Trigger/button spawnflag filter bits — `func_button` (`CBaseButton::Spawn`) + `trigger_multiple`/`trigger_once` (`PassesTriggerFilters`) maps in `entity_io.md`. **4.2 re-verified the button bits against the decompile and corrected a `0x100`↔`0x400` (touch↔use) label swap: VtMB keeps the stock layout (`0x100`=touch, `0x400`=use); the `0x400` handler gates on `PassesUseFilter`.** | 4.2, 4.5 | [x] |
| RE2 | Retail queue-vs-think service order — **confirmed think-first** (thinks then `ServiceEvents`); our provisional queue-first diverges (see `engine-core.md` Tick note) | 1.4 | [x] |
| RE3 | `__setattr__` write path + error-to-false + `G` default-0 **all confirmed** | 5.2, 9.1 | [x] |
| RE4 | Ghidra datamap export (validate our input/field tables vs retail) — method confirmed, CBaseEntity base map extracted. **4.5 proved the fast path: `run.ps1 -Script DumpGrep` (str=/cls= anchors) over the persisted `vtmb` project, no re-import — recovered every P4.5 class factory/datamap and settled `logic_case_toggle`'s delta-advance divergence.** | 4.5+ (optional, valuable) | [~] |
| RE5 | Dice-system vroll golden test | 9.6 | [ ] |
| RE6 | `ent_survey` count reconciliation — retail = 16,125 outputs / 1,591 Python (16,214/1,621 was stale) | 0.6 | [x] |
| RE7 | Retail `.sav` block wire format | 10.7 (only for importing retail saves) | [P] |
| RE8 | Re-base `entity_io.md` survey on the patch (engine-loaded) map set — patch 24,081 outputs / 6,956 Python (retail 16,125 / 1,591) | 0.8 | [x] |

### Ghidra extraction — findings + plan *(pass dated 2026-07-22; scripts + dumps in `tools/ghidra/`, `out/re*.txt`)*

All addresses are `vampire.dll` (image base `0x10000000`) unless noted. Structure facts feed
`python_bridge.md` / `entity_io.md`; the extractor workflow is `tools/ghidra/README.md`.

**RE3 — CONFIRMED (was "assumed, load-bearing"):**

- **`Entity.__setattr__`** — thunk `0x10013c3c` → body `FUN_10195a10`. Resolves the name against
  the **same datamap walk** as `__getattr__` (`FUN_10195940`, `GetDataDescMap` = vtable `+0x148`,
  `baseMap` chain @`0xC`, 44-byte records). Unknown names **fall through to the instance
  `__dict__`** (`PyDict_SetItemString`) — confirmed, not inferred. `_entity_ptr_` is **read-only**
  (`"%s is read only"`). Writes are gated by **flag bit `0x8` at record `+0x12`** (non-keyable
  fields raise read-only). Full per-fieldtype **write-marshalling** table over VtMB's shifted
  `fieldtype_t`: `1`/`0xf` float(=FLOAT/TIME), `2`/`0x10`/`0x11` string(interned), `3`/`0xe`
  3-vector, `4` int, `5` bool(`PyObject_IsTrue`), `8` color32(4-tuple), `0xb`/`0xc`/`0xd` ehandle
  variants; `default` → `"%s.%s has unhandled type %d"` TypeError.
- **Error-to-false** — `logic_pythoncheck` evaluator `FUN_10135290` runs
  `PyRun_String(python_script, 0x102=Py_eval_input, __main__, __main__)`, seeds the result to `0`,
  and on a raise calls `PyErr_Print()` then returns `local_4 != 0` = **false**; a non-`int` result
  is also false. The exec/call paths swallow identically: `FUN_100ce8a0` (exec a source string —
  `ScheduleTask`/level callbacks) and `FUN_100ce990` (the `"__main__.%s"` field-6 call, format
  string `0x1055e370`) both `PyErr_Print()` and continue, never aborting the map. `.dlg` field-4
  is the same engine + same `PyRun_String` eval pattern (residual: not decompiled per-path).
- Bonus recovered en route: the **entity input-function invoker** `FUN_101962a0` (marshals
  `Entity.Method(args)` by fieldtype, fires via vtable `+0x1d8`); `use_icon`/`locked_icon` live at
  entity `+0x5c8`/`+0x5cc` with `GetUseIcon` = `FUN_100c8940` (returns locked_icon when the locked
  byte `+0x5c4` is set); `CBaseButton` vftable `0x1045293c`, object size `0x5d0`; ScriptHide/Unhide
  are vtable slots [77]/[78] = `0x100a8710`/`0x100a8990` (cross-checks `entity_io.md`).
- **`G` default-0 — CONFIRMED** (recorded in `python_bridge.md` → "G — the global flag bag"). G is
  the engine type **`PyDataManager`**, backed by a flag dict (`0x1072b370`) + a `morgue` dict
  (`0x1072b374`). `tp_getattr` (`0x1019b3d0`) resolves `InitMode`→int, `morgue`→dict,
  `Py_FindMethod`(table `0x1058f5d0`)→method, `PyDict_GetItemString(flagdict, name)`→value, and on a
  **miss does `PyErr_Clear()` then `PyInt_FromLong(0)`** — so `G.Story_State` unset returns integer
  `0`, never raises. `tp_setattr` (`0x1019b570`) mirrors it (morgue/methods read-only; `None` deletes;
  else `PyDict_SetItemString`); G is pickled for saves (`FUN_1019b130`, `cPickle`).

**RE1 — button + trigger spawnflag maps CONFIRMED** (full tables in `entity_io.md`). `m_spawnflags`
is a `FIELD_INTEGER` at entity `+0x204` (datamap builder `FUN_100a22f0`).
- **`func_button`** (`CBaseButton::Spawn` `FUN_100c8d60` + use/touch handlers `0x100c9430`/`0x100c9250`,
  vftable `0x1045293c`): `0x1`=DONTMOVE, `0x20`=TOGGLE, `0x40`=timed setup, `0x100`=use handler,
  `0x400`=touch handler, `0x800`=starts-locked (`+0x5c4`), `0x1000`=secondary use-gate (`+0x5c5`).
  `0x2000` is **inert** for buttons (tested nowhere in the class).
- **`trigger_multiple`/`trigger_once`** (`CBaseTrigger::PassesTriggerFilters` `FUN_101c5460`,
  bases `0x1047d08c`/`0x1047da24`/`0x1047dee4`): **matches stock Source** — `0x1`=ALLOW_CLIENTS,
  `0x2`=ALLOW_NPCS, `0x4`=ALLOW_PUSHABLES, `0x8`=ALLOW_PHYSICS, plus the `m_hFilter` entity (`+0x564`,
  its vtable `+0x3c4`); bit `0x80`=remove-after-fire (`trigger_once`).
- Residual (non-blocking): the exact `FL_*` bit values GetFlags tests for client/NPC, and other
  trigger subclasses' extra flags — read per-class when a specific one is implemented.

**Method note (unblocked):** RE1 + `G` were both stalled because their targets are reached via
`m_pfn*`/vtable/immediate loads the default analyzers leave un-referenced. **Re-importing `vampire.dll`
with the `EnableAIF` pre-script** (`run.ps1 -Import … -PreScript EnableAIF`) disassembled the
pointer-only code and unblocked both — G fully, RE1's `Spawn` via the vftable region (the `m_pfn`
handler-string refs stayed absent, so `Spawn` was reached through the vtable rather than the trace
strings). The AIF DB is the current project state; further per-class work rides on it.

**RE4 — datamap export: method CONFIRMED + CBaseEntity base map extracted.** VtMB builds datamaps
**at runtime** (no static `DEFINE_FIELD` arrays), so the export decompiles the per-class **datamap
builder** functions, not a static `.data` walk. The CBaseEntity builder is **`FUN_100a22f0`** (1075
lines); each record carries the internal (`m_spawnflags`) *and* external/Hammer (`spawnflags`) name,
`fieldType`, `fieldOffset`, `flags` (bit `0x8` = keyable/writable, per RE3), and `inputFunc`.
Extracted base contract (validates the P1 base entity, task 1.3): **keyfields** = `angles, model,
target, targetname, spawnflags, health, max_health, flags, velocity, avelocity, basevelocity,
gravity, friction, ltime, waterlevel, watertype, soundgroup, usescript, npc_transparent,
blocks_traces, dmg_filter_name, use_filter_name`; **inputs** = `Kill, ScriptHide, ScriptUnhide,
Use, SetParent, ClearParent, Alpha, Color, SetSoundOverrideEnt, SetFakeSilence`; **outputs** =
`OnUseBegin, OnUseEnd`. `usescript`/`soundgroup`/`npc_transparent`/`blocks_traces`/`SetFakeSilence`/
`SetSoundOverrideEnt`/`ScriptHide`/`ScriptUnhide` are **VtMB-specific base fields** (not stock
Source) — the port must carry them at `CBaseEntity`. Next: `DumpDatamap.java` batches the same
extraction over every class builder (locate each via its `GetDataDescMap` = vtable `+0x148`, or by
the builder-call pattern) → JSON per classname; run before 4.5.

**RE2 — DONE: retail is think-first.** The `vampire.dll` server frame calls
`Physics_RunThinkFunctions` (`FUN_1003bdd0` via thunk, call-site `0x1011ac1b`) and *then*, at
`0x1011ac34`, the sole `CEventQueue::ServiceEvents` (`FUN_100cfac0` → `FUN_100cebb0(g_EventQueue
0x106e7050)`). ServiceEvents walks `m_pEvents` (head `+0x38`) firing every event with
`fireTime ≤ curtime` — type 0 Entity I/O via `AcceptInput` (vtable `+0x1d8`), type 1 `ScheduleTask`
source via `FUN_100ce8a0`, field-6 Python via `FUN_100ce990`, type 2 discipline. So an output
fired *during* a think is serviced after all thinks that frame. **Implication:** flip task 1.4 to
think-first to match retail (recorded in `engine-core.md` Tick note) — unless save-determinism
argues for keeping queue-first; that is the 1.4 tick-order call.

**RE5 (Ghidra-drivable, open):** optionally static-recover the vroll resolver in `vampire.dll` to
cross-check `recovered/dice-system.md` alongside the running-game golden test.

## Options — evaluated, not planned (revisit triggers stated)

- **Remote Control API** (browser inspection of the running game): Cog covers it in-process;
  revisit if second-machine debugging becomes real.
- **Gameplay Debugger category**: redundant with Cog; revisit if a crosshair-centric HUD
  view earns its keep.
- **NetImgui remote**: free with Cog if a headless/remote process ever appears.
- **Dynamic console autocomplete** of targetnames for `ent_fire`: nice-to-have after 2.3.
- **Slate dev console**: superseded (UE console + Cog).
- **Lumen Lite** (new in 5.8): medium-quality GI via irradiance fields with probe occlusion,
  ~2× faster than full Lumen, runs on PC. A potential low-end contingency that is cheaper
  than the parked lump-8 bake — but VtMB's look is bounce-dominated and calibrated against
  full Lumen GI, so it risks the look. HWRT commitment unchanged; revisit if 10.3 floor
  validation fails or a sub-DXR audience becomes a goal.
- **Asset-enhancement track** (offline remaster of VtMB's own art): delight → super-resolve →
  PBR-synthesize (normal/roughness/AO/envmask), as an `elysium.EnhancedTextures` A/B toggle on
  top of the faithful set. Tier 0 (delight + upscale) fixes real deficits for a dynamically-
  relit engine; Tier 1 (PBR synthesis) is style-anchored enhancement. Scaffolding exists
  (`upscale_bench.py`, `sky_upscale.py`, `retex_dds.py`); `M_VtMB_World`'s normal/envmask slots
  are the runtime hooks. **Budget-gated** by the 3060/12 GB floor (2× default, 4× hero only;
  BCn+mips mandatory). Full plan + adjudication test: `asset-enhancement.md`. Revisit after the
  vertical slice plays (P10-ish); not before — it is polish on a shipped look, not a blocker.

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| MegaLights silently disengages (VSM fallback) | frame collapses | 0.2 check lives in the profiling routine forever; 3.1 pins it |
| ~~Cog fails to build on 5.8~~ **(resolved)** | debug layer slips | **0.5 done: Cog builds + runs clean on 5.8** (VS 14.50, no ImPlot patch needed); fallback (VesCodes/ImGui + hand-rolled windows) unused |
| Level scripts exceed the mini-interpreter subset | scripting rework | 5.5 survey + decision before building; CPython embed is the acknowledged fallback |
| Chaos kinematic movers push/block poorly | doors feel wrong | 4.1 prototypes one door first |
| Floor perf unproven (no 3060 on hand) | late surprise | look-gates until 10.3; lump-8 bake parked as contingency; Lumen Lite noted as a cheaper option (see Options) |
| Tutorial-only calibration bias | rework on other maps | 0.3 second map early; all calibration provisional until 10.1 |
| Sidecar space drift (Godot-era leftovers) | subtle geometry/logic bugs | 0.4 audit before any new consumer |
| Save determinism erodes | broken saves late | standing rule since P1: no engine timers, own serializable structs |
| Legal posture | project-ending | bring-your-own-game holds; nothing game-sourced committed — standing constraint on every task |
| Asset enhancement drifts off-style | silent look regression | `asset-enhancement.md` adjudication test + `elysium.EnhancedTextures` A/B toggle keeps the faithful set as reference; per-family review, not per-texture |

## Decision log (append-only)

- **2026-07-22** — **5.5 decided: embed CPython 2.x (option c), plus a working PoC.** The survey
  settles it. The 36 loose level scripts (out/scripts, **16,473 lines** excl. the bundled 2.1 stdlib)
  are full Python 2.1, not an expression dialect: **1,119 `def`, 45 old-style `class`, 345 `for` /
  26 `while`, 30 `try`/23 `except`, 162 `import`, 627 `print` statements, 3 `exec`, list-comps,
  `lambda`, `%`-formatting**, importing `random`/`time`/`types`/`struct`/`string` (+ VtMB's own
  `lib/` pickle/string/random). That is decisively past `ElysiumExpr` (an expression evaluator — no
  statements/classes/control-flow/exceptions/imports); option (a) would mean reimplementing all of
  CPython 2 + a stdlib, and (b) transpiling 16.5k lines of dynamic Py2 with `exec`/pickle/old-style
  classes is huge and fragile. **The 2.1→2.7 delta is ~0** (checked every script: **zero**
  string-exceptions — the one thing removed in 2.6 — and **zero** `from __future__`; classic `/`
  division and old-style-`class` are the default on both), so a maintained 2.7 fork runs the retail
  scripts 1:1. VtMB's own VM is stock CPython 2.1 (`vampire_python21.dll`, 653 exports; `.pyc` magic
  60202), and it saves *through* `pickle` — so a real embed aligns with the R8 save model rather than
  fighting it (G is proxied so C++ and Python share one store).
  **Fork chosen: `qnox/python-2.7`** (CPython **2.7.18**, actively maintained — release `v20260109`,
  Jan 2026). Its `x86_64-pc-windows-msvc` install_only build is **MSC v.1944 (VS2022) / 64-bit** — the
  same toolchain family as UE 5.8 — with headers + `python27.lib` + `python27.dll` + stdlib; verified
  running (classic `7/2==3`, stdlib imports). Tauthon (2.7.18 + Py3 backports, semi-maintained) is the
  fallback; actual 2.1 isn't worth the modern-MSVC pain given the ~0 delta.
  **PoC pulled forward from 9.3 (per the scope call):** vendored the SDK at
  `Source/ElysiumUE/ThirdParty/CPython27/` (dll+lib+headers+PythonHome/Lib); `Build.cs` wires it Win64-
  only (delay-load `python27.dll`, `ELYSIUM_WITH_CPYTHON`, stdlib as a RuntimeDependency); the include
  shim (`ThirdParty/ElysiumPython.h`) undefs `_DEBUG` across `<Python.h>` (else `Py_DEBUG` ABI + the
  `python27_d.lib` auto-link break the build — the standard PythonScriptPlugin trick). New
  `FElysiumPythonVM` (process-global; `GetDllHandle` the vendored dll → `Py_SetPythonHome` → `Py_NoSite`
  → `Py_Initialize`) registers a `vampire` C-module whose only *real* binding is **`G` proxied onto
  `UElysiumGameStateSubsystem`** (attribute get/set = flag read/write, default-0 / assign-None-deletes,
  `keys`/`has_key`/`ClearAll`); a Python **bootstrap** stands up forgiving stubs for the natives 9.3 will
  make C (`FindPlayer`/`FindEntityByName`/… + a stub `vamputil`), and stdout/stderr route to the UE log.
  `FElysiumCPythonScriptHost` slots into the existing `IElysiumScriptHost` seam (`elysium.script.cpython
  [0|1]`), alongside `elysium.py.smoke`/`exec`/`load`/`fire` verbs and a **CPython panel in the
  `Elysium.Scripting` Cog window** (status/version, host toggle, load-level-script, list + fire the On*
  callbacks, a python exec box; the G table + eval log below reflect its evals). **Validated offline
  with the vendored interpreter against the real `tutorial.py`:** it imports cleanly (its module-level
  code + `from vamputil import *` run, `levelscript` loads, `cCelerity=8` resolves), its On* callbacks
  execute, and field-6-shaped statements resolve level-script constants —
  **`G.Tutorial_Discflags |= cCelerity` flips G to 8**, exactly the acceptance ElysiumExpr can never
  meet (NameError). In-engine, all CPython C++ TUs **compile**; the full editor link + in-editor run is
  blocked only by the parallel **P8 glTFRuntime** WIP (plugin not yet installed). **Remainder = 9.3:**
  replace the Python native-stubs with the real C `vampire` bindings (54 methods), auto-load the map's
  `worldspawn.levelscript` at map load, and pin field-6 ↔ level-script name resolution.

- **2026-07-22** — 4.5 landed (tutorial logic/point/brush + trigger classes). **Grounded in the
  decompile, not guessed:** `tools/ghidra/run.ps1 -Script DumpGrep` against the analyzed `vampire.dll`
  recovered every class factory + datamap by classname/field-string anchor. **The one real VtMB
  divergence is `logic_case_toggle`** (`FUN_101344f0` / core `FUN_101346e0`): its `InValue` is a
  *delta* that advances a current-case pointer that many **configured** cases (skipping empty slots,
  wrapping 0..15) and fires the landed case — not stock `logic_case`'s value-string match (whose class
  is 4 bytes smaller, lacking the current-index int). This is why the tutorial wires
  `math_counter.OutValue → logic_case_toggle.InValue`: the counter value is the advance amount. Both
  are implemented (shared Case base); the value flows through a new Source-`COutput<T>` seam
  (`FireOutput(name, activator, value)` fills an empty map-param, else the authored param wins).
  `math_counter`/`logic_timer` confirmed **stock** (present, unmodified). `func_brush` is nearly a
  base entity (`FUN_1013dd30` — trivial ctor); its Solidity/Enable/Disable fold into the body's single
  `SetDormant` switch so they don't fight dormancy. `env_fade` renders on one screen-fade state held on
  the entity world and polled by `AElysiumHUD`. The player is reached from the plain-C++ substrate via
  a new `FElysiumEntityWorld::GetPlayerPawn()` seam (point_teleport, trigger_hurt). Debug: `GetDebugState`
  on every class + a dedicated `Elysium.Logic` Cog window. **RE4 (Ghidra datamap export)** is now the
  proven, low-cost method for any future class pass — DumpGrep on the persisted project, no re-import.
  `trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` left as inert records
  (their stealth/inventory/RoomDSP systems don't exist yet).
- **2026-07-22** — 5.3 landed (native bindings). **Grounded in the exported field-6, not the table
  count:** across all 10 exported maps only `FindPlayer()` (7×) + Character methods off it
  (`RemoveItem`/`SewerMap`/`GiveItem`/`ClearActiveDisciplines`) are native; the heavy callees
  (`spawnCopCar`, `resetHos`, the `cXxx` discipline constants, the level's `On*` callbacks) are all
  **level-script** names → 5.5, correctly still NameError, so the tutorial's `G.Discflags |= cCelerity`
  keeps no-opping. **Zero field-6 references `self`/`activator`** — they are I/O *targets*, not Python
  names — so those bind from the eval context but sit **below** the module globals as a harmless
  fallback. **Stub policy = log-only (roadmap default):** only the two systems that already exist wire
  for real (`SetQuest`/`GetQuestState` → the quest map); the other 9 globals + 22 methods log a stub +
  a sensible default (predicate globals read false). `ScheduleTask` (deferred source) and `ChangeMap`
  (travel) stay stubs to respect their phase owners (5.4 / P4). **Forgiving dispatch:** any attribute
  off a `Character` object binds (unlisted names too), so a call outside the known-24 runs to the
  generic stub instead of raising — matching retail's `__getattr__` fall-through and the slice
  acceptance (`FindPlayer().ClearActiveDisciplines()` runs). NPC handles accept Character methods too.
  One static `GNativeBindings` table drives both membership and the debug view. **Precedence caveat for
  5.5:** `OneOfSet` is both a native global and a `vamputil.py` helper exec'd into `__main__` (the
  latter wins in retail); when level-script names resolve, the level-script definition must shadow the
  native one. Debug: the `Elysium.Scripting` window gains a Native-bindings table (kind/status/live
  call-count) + a Recent-native-calls log, fed by `UElysiumGameStateSubsystem`'s native-call ring.
- **2026-07-22** — 5.2 landed (expression evaluator). **One evaluator, no second dispatch:**
  `ElysiumExpr` (lexer + recursive-descent AST parser + tree-walk) is exception-free — every error
  collapses to Void (error-to-false, RE3). **Scope beyond the literal task line** (calls / attr /
  literals / compare / and-or): added assignment statements + the full `+ - * / % | & ^ << >> **`
  operator set + `None`, because the tutorial's real field-6 is 60% `G.<flag> = <expr>` (46 of 77,
  the only operator being `|` for the `Tutorial_Discflags` accumulation) and the P5 slice needs
  flags to flip. Python-2 semantics (floor int div/mod, `and`/`or` return an operand, chained
  comparisons). **`ent.Input()` dispatches through the existing chokepoints** (`EnqueueInput
  "!self"` targeting the one handle) rather than a new synchronous path — visible in the queue
  window, single-steppable, serializable. **Live field-6 is opt-in** (`elysium.script.live`,
  default off; the null host stays the map-load default) so 5.2 changes no map-load behaviour; 5.4
  makes it the default and adds logic_pythoncheck + ScheduleTask. Bare names + native globals
  (FindPlayer, `cCelerity`, DialogPostProcess) are NameErrors until 5.3/5.5, so `G.x = G.x |
  cCelerity` correctly no-ops for now while plain `G.x = 1` flips visibly. Debug surface: live `G`
  table + eval/exec box + recent-eval log in the `Elysium.Scripting` Cog window, echoed by
  `elysium.eval` / `elysium.exec`.
- **2026-07-22** — 1.6 landed (starter classes). **`logic_auto` owns map-load ignition** via a
  one-shot think (first world tick), replacing the generic `FElysiumEntityWorld::FireMapLoadOutputs`
  bootstrap (removed) — matches retail (logic_auto fires on the first server think, after every
  target has spawned) and exercises the think path. **`CBaseTrigger` is a registry-only chain node**
  (no entity carries that classname) so `trigger_multiple`/`trigger_once` share Enable/Disable/Toggle
  + StartDisabled/wait through one base; this is the pattern the P4.5 trigger family extends.
  **Trigger activation filter reduces to the `ALLOW_CLIENTS` (0x1) bit** in P1.6 because the only
  toucher is the player and its activator is unresolved (the pawn is not an entity until P4);
  empirically the tutorial's triggers carry 0x1 (43/44 `trigger_multiple`, all `trigger_once`), and
  the one `0x8` physics-only trigger *should* ignore the player — so the reduction is faithful, not a
  shortcut. **Remove-on-fire / fast-retrigger spawnflags left unmodelled** for `logic_relay`/
  `logic_auto`: those bits are unconfirmed for VtMB (RE1 only covered button + trigger flags, and
  buttons diverge from stock), and firing `Kill()` on a wrong bit is a worse failure than a spurious
  re-fire, which per-output `times` already bounds. Revisit if a tutorial relay over-fires.
- **2026-07-22** — 1.1 landed. Currency types are plain C++ structs (R1, no reflection):
  `FElysiumVariant` carries the seven runtime categories with total, never-throwing
  coercions (a Void variant is the falsy / error-to-false case). `FElysiumEntityHandle` is
  the value only — `IsSet()` is structural; true falsy-when-dead/stale is decided by
  `FElysiumEntityWorld::Resolve` (1.4), not here. **The `G` store and quest map key
  case-sensitively** (custom `KeyFuncs` + `FCrc::StrCrc32`) because they mirror Python dicts;
  UE's default `FString`/`FName` maps are case-insensitive, which would silently merge
  distinct flags. `G` is variant-valued, default-0-on-miss, and assigning Void deletes the
  key (decompiled `tp_getattr`/`tp_setattr`, `python_bridge.md`). Clock + `G` + quests live on
  the GI subsystem so they survive travel.
- **2026-07-22** — 0.5 Cog spike closed: vendored the **main Cog plugin only** (upstream `cb1b435`
  on `main`, MIT) into `Plugins/Cog/`, minimal deps, `UElysiumCogSubsystem` registering stock
  CogEngine windows. Builds + runs on UE 5.8 with **no source patches** (the flagged ImPlot
  `INFINITY` MSVC error did not reproduce on VS 14.50). Build products gitignored; provenance
  (base commit) recorded here so future updates re-base off `main@cb1b435`.
- **2026-07** — Cog (MIT, vendored, dev-only) adopted as the debug UI shell after tooling
  research; Slate console superseded. (`debug-tooling.md`)
- **2026-07** — Entity object model adopted: plain-C++ entities with optional Unreal bodies;
  one name table per class (I/O + Python + keyvalues + saves + inspector); generation-checked
  handles on stable `.ents` indices; one clock + one queue, no `FTimerManager`; two
  instrumented chokepoints; dormancy as one switch. (`engine-core.md`)
- **2026-07** — Queue-serviced-before-thinks tick order chosen (retail order unknown, RE2).
- **2026-07-22** — RE2 resolved: **retail is think-first** — `Physics_RunThinkFunctions` then
  `CEventQueue::ServiceEvents` in the `vampire.dll` server frame (addresses in the RE2 note above).
  The provisional queue-first choice diverges from retail. Decision for task 1.4: match retail
  (think-first) unless save-determinism argues for queue-first; `engine-core.md` Tick note now
  documents think-first.
- **2026-07-22** — 1.4 tick-order resolved and shipped: **think-first**. No save/load exists yet
  (M6/P9), so no determinism argument for queue-first; `FElysiumEntityWorld::Tick` runs
  `RunThinks(now)` then `ServiceEvents(now)`, matching retail. Save-determinism can revisit at P9
  if needed (the queue and think times both serialize regardless of service order).
- **2026-07** — Debug-substrate-before-M3 sequencing chosen ("foundation now"): P1 → P2 → P4.
- **Standing (from strategy)** — fully dynamic lighting committed (HWRT Lumen + MegaLights +
  VSM, DX12/SM6 mandatory; no baked GI — lump-8 bake parked as low-end contingency);
  no game content in `.uasset`s ever (extend `.emc`-style caches instead); variable timestep
  matching retail (no fixed tick); Unreal-native substitutions where they beat porting
  (Chaos physics props, NavMesh+BT AI, Single Layer Water, deferred decals, Cable ropes,
  native audio submixes); Nanite not applicable; bring-your-own-game legal posture.
- **2026-07-22** — Web-research de-risk pass over the open questions:
  **MegaLights is Production-Ready in UE 5.8** and the per-light control is the
  "MegaLights Shadow Method" property (3.1 is a straightforward property set); MegaLights
  does not light water and Single Layer Water gets forced-mirror Lumen reflections
  (accepted for VtMB water — noted on 7.3). **Audio libraries pinned:** `dr_wav` for
  MS-ADPCM/IMA-ADPCM (6.1), `dr_mp3`/`minimp3` for MP3 (6.2) — all public domain,
  single-header, MP3 patents expired. **CPython 2.x embed fallback confirmed viable**
  (maintained 2.7.18 forks build with VS2019+) — 5.5 decision itself stays pending on the
  script survey. **Cog** (UE 5.5+, active) and **glTFRuntime** (UE 5.7, active) both look
  healthy but lack explicit 5.8 confirmation — the 0.5 and 8.2 spikes stand. **Lumen Lite**
  (new 5.8 medium-quality GI) recorded as an Options entry only — the HWRT-Lumen commitment
  is unchanged; revisit trigger: 10.3 floor validation fails or a sub-DXR audience matters.
- **2026-07-22** — Ghidra extraction pass over the RE-drivable open items (`vampire.dll`; dumps in
  `tools/ghidra/out/re*.txt`). **RE3 confirmed** two load-bearing assumptions with recovered logic:
  `Entity.__setattr__` (`FUN_10195a10`) writes through the same datamap walk as `__getattr__`, falls
  through to the instance `__dict__` on unknown names, holds `_entity_ptr_` read-only, gates writes
  on record flag bit `0x8`, and marshals per VtMB's shifted `fieldtype_t`; **error-to-false** is real —
  `logic_pythoncheck` (`FUN_10135290`) evals with `Py_eval_input`, `PyErr_Print`s on a raise, and
  returns false, and the exec/field-6 paths (`FUN_100ce8a0`/`FUN_100ce990`) swallow errors likewise.
  **RE1 partially** recovered (`CBaseButton` layout/vftable `0x1045293c`, use-icon offsets), spawnflag
  bit→behavior map still pending the Use/Touch handlers. **RE4** (datamap JSON export) and **RE3 `G`
  default-0** scoped with exact seeds — see "Ghidra extraction — findings + plan" above.
- **2026-07-22 (cont.)** — Ghidra pass continued. **RE4 method confirmed**: VtMB datamaps are
  runtime-built, so the export decompiles per-class **datamap builders** (`CBaseEntity` =
  `FUN_100a22f0`), not a static `.data` walk; the CBaseEntity base keyfield/input/output contract is
  extracted and recorded in `python_bridge.md` (RE4 → `[~]`). **RE1** (button spawnflag bits) and
  **`G` default-0** both hit the same wall — their target functions are `m_pfn*`/vtable/immediate-
  reached and carry no references in Ghidra's DB, so xref discovery stalls; unblock via an
  `EnableAIF` re-import or direct PE method-table/vtable parsing (recorded as the "shared blocker").
- **2026-07-22 (cont. 2)** — `EnableAIF` re-import of `vampire.dll` ran and unblocked both stalled
  items. **`G` default-0 CONFIRMED** → RE3 fully closed (`[x]`): G is `PyDataManager`; `tp_getattr`
  `0x1019b3d0` returns `PyInt_FromLong(0)` on a flag miss. **RE1 `CBaseButton::Spawn` map recovered**
  (`m_spawnflags`@`+0x204`; `FUN_100c8d60`) — bits `0x1`/`0x40`/`0x100`/`0x400`/`0x800`/`0x1000`
  decoded into `entity_io.md`; bits `0x20`/`0x2000` + `trigger_multiple` filter still to do (RE1 stays
  `[~]`). Findings live in `python_bridge.md` (G) + `entity_io.md` (button); the AIF-analyzed DB is now
  the project baseline.
- **2026-07-22 (cont. 3)** — **0.4 sidecar space audit done.** Traced all five downstream-consumed
  sidecars through `UE_bsp_to_scene.py`: `.ents`, `.sprites`, `.spawn`, `.water`, `_decals.obj` are
  **already emitted in Unreal cm** (every one routes through `source_to_unreal`/`INCH_TO_CM`, decals
  with winding reversed) — the exporter had been fully migrated and the audit found **no code
  straggler**. The `rebuild-strategy.md` contract table was stale (`.sprites` "Godot metres", `.spawn`
  "Source coords"); corrected, and space made explicit on `.ents`/`.water` too. **PL7 is empty.**
  Runtime `.spawn` reader confirmed verbatim.
- **2026-07-22** — Asset-enhancement direction accepted (design; not scheduled). Stance:
  **faithful baseline + opt-in, code-driven remaster layer** that preserves VtMB's grimy
  gothic-punk art direction, always as an `elysium.EnhancedTextures` A/B toggle. Adjudication
  test: serve the art direction / fix a technical deficit that fights the dynamic relight → in;
  invent or override an artist decision → out. Sequence is dictated by the dynamic relight —
  **delight albedo first** (recover true base color from 2004 painted-in shading), then
  super-resolve, then derive normal/roughness/AO from the *delit* albedo, metallic by hand-mask
  only. Tier 0 (delight + upscale) is a real deficit fix; Tier 1 (PBR synthesis) is
  style-anchored, curated per material family, budget-gated by the 3060/12 GB floor. Scaffolding
  already exists (`upscale_bench.py`/`sky_upscale.py`/`retex_dds.py`); runtime hooks are
  `M_VtMB_World`'s planned normal/envmask slots. Written up in `asset-enhancement.md`; strategy
  principle 6a; Options + risk-register entries added. Web-research pass confirmed the technique
  set (AI PBR-from-diffuse, de-lighting tools, ESRGAN game-remaster practice) and their caveats
  (guesswork needing curation; delighters tuned for photoscans, not hand-painted art).
- **Pending** — 5.5 level-script execution strategy (interpreter vs transpile vs CPython);
  8.2 glTFRuntime confirmation for the skeletal path; 10.6 EnhancedInput migrate-or-remove;
  7.2 decal final path (PMC parity vs `UDecalComponent`) decided after both stages render.

## Traceability (old plan IDs → this doc)

| Old | Here |
|---|---|
| M0 | Done foundation |
| M1.1 Source movement / M1.2 materials / M1.3 prewarm / M1.4 texlights | 4.7 / 7.4 / 3.8 / 3.4 |
| M1 polish (grade, sky orientation, A/B toggles, repo hygiene) | 3.7, 0.7 |
| M2 (props done; decals, water, coronas, A/B) | done / 7.2 / 7.3 / 7.1 / 7.8 |
| M3 | P1 + P2 + P4 |
| M4 | P5 + P6 |
| M5 | P8 |
| M6 | P9 |
| L0.1/L0.2 | 0.1/0.2 · L1.1–L1.4 → 3.1–3.4 · L2.1–L2.3 → 3.5–3.7 · L3.1–L3.4 → 7.5, 7.6, 7.1, 7.7 · L4.1–L4.4 → 10.2, 10.2, 10.2, 3.9 · L5.1–L5.3 → 7.8, 10.1, 10.3 |
| X1 / X2 | 0.3 / 0.4 |
| engine-core Phase 1 / Phase 2 | P1 / P2 |

## Appendix — profiling baseline (0.1)

Captured **headless** by `profile.bat` → the `-ElysiumProfile` harness
(`Source/ElysiumUE/Private/ElysiumProfiler.cpp`) → `tools/profile_report.py`. The harness
pins the camera to each fixed vantage near spawn, warms up, captures per-pass GPU stats
through the CSV profiler, and exits — no manual console typing. Full per-vantage reports
(incl. the heaviest-pass breakdown) regenerate at `tools/out/_profile/<map>_report.md`.
Re-run any time with `profile.bat <map> [cam]`; add a vantage with `elysium.campos` in-game.

**Dev GPU: RTX 5070 Ti · D3D12 / `PCD3D_SM6` · 2560×1440 · warmup 120 / capture 300 frames.**
This card is far above the RTX 3060 floor, so read these for **pass proportions and
regression tracking**, not floor frame rate (the floor is judged by *look* — see
`rendering-perf.md`). Vantage coordinates are baked in `GProfileCams[]` and echoed in each
report's vantage headers.

### sp_tutorial_1 — 395 world lights — GPU ms per vantage

| Pass | spawn | t1 | t2 | t3 | t4 |
|---|---|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.01 | 0.01 | 0.01 | 0.01 |
| Lumen reflections | 0.06 | 0.07 | 0.06 | 0.06 | 0.06 |
| MegaLights | 1.06 | 1.08 | 1.00 | 1.34 | 1.15 |
| ShadowDepths / VSM | 0.83 | 0.88 | 1.02 | 1.11 | 1.03 |
| **Total GPU** (whole frame) | **4.87** | **5.25** | **5.32** | **5.72** | **5.43** |

### sm_hub_1 — 687 world lights — GPU ms per vantage

| Pass | spawn | h1 | h2 |
|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.01 | 0.01 |
| Lumen reflections | 0.05 | 0.05 | 0.05 |
| MegaLights | 1.05 | 1.12 | 1.26 |
| ShadowDepths / VSM | 0.00 | 0.00 | 0.00 |
| **Total GPU** (whole frame) | **3.82** | **4.40** | **4.49** |

### sm_pawnshop_1 — 161 world lights — GPU ms per vantage

| Pass | spawn | p1 | p2 | p3 |
|---|---|---|---|---|
| Lumen GI (ScreenProbeGather) | 0.01 | 0.01 | 0.01 | 0.01 |
| Lumen reflections | 0.06 | 0.05 | 0.06 | 0.05 |
| MegaLights | 1.28 | 1.32 | 1.27 | 1.23 |
| ShadowDepths / VSM | 0.00 | 0.00 | 0.00 | 0.00 |
| **Total GPU** (whole frame) | **3.96** | **4.31** | **4.30** | **4.27** |

**0.2 verdict — MegaLights is engaging, no VSM blow-up.** MegaLights (~1.0–1.3 ms) meets or
beats ShadowDepths on every vantage, and the many-light cost is ~**flat**: 687 lights
(sm_hub_1) and 161 (sm_pawnshop_1) both cost the same ~1 ms as 395 (sp_tutorial_1).
ShadowDepths never dominates and is ~0 on both `sm_` maps. MegaLights is carrying the local lights as designed — 3.1 is **not** the
immediate next task. (The `[VSM] Non-Nanite Marking Job Queue overflow` HUD warning appears
transiently but does not translate into a ShadowDepths blow-up in steady state — worth a
glance if ShadowDepths ever spikes in a future capture.)

**Other reads.** TemporalSuperResolution (~1.1 ms) ties/leads MegaLights as the single
heaviest pass at every vantage — the expected upscale cost of `r.ScreenPercentage=66`. Lumen
GI is ~free here (0.01 ms) *on this card*; on the 3060 floor HWRT Lumen is the dominant cost
(`rendering-perf.md` → "Floor reality"), so this near-zero is a fast-GPU artifact, not proof
Lumen is cheap. Render-thread time collapses to ~0 (idle-waiting on the GPU): the title is
GPU-bound, as expected.
