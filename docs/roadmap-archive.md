# Roadmap archive — as-built records

The full as-built record of every completed roadmap task, moved here verbatim when the task
lands: `roadmap.md` keeps the short summary and the status flip; this file keeps the detail.
Same task IDs, same phase headings — look a task up by its ID. An in-progress (`[~]`) task
appearing here carries the landed-portion record as of the move; its open scope stays in
`roadmap.md`.

Durable how-a-system-works facts belong in the owning doc (`docs/CLAUDE.md` maps them); what
belongs here is the historical record — what was built, what was verified, what was
deliberately not modelled.

Records are verbatim moves out of `roadmap.md`: where one says "the decision log
(above/below)", it means `decisions.md`; "the appendix" means `rendering-perf.md` →
"Profiling baseline".

## The first-beat path (B*) — landing → the second warp point

- [x] **B1 `env_fade` fires `OnBeginFade`** — landed. `Fade` starts the screen fade and then fires
  `OnBeginFade` with no delay, activator passed through — which is the *whole* class: the
  decompiled `CEnvFade` datamap (`0x10568c10` → dataDesc `0x10568c54`, baseMap `0x10552e18`) is
  exactly four records, `duration` (`m_Duration` +0x450), `holdtime` (`m_HoldTime` +0x454), input
  `Fade` (`0x10100F90`), output `OnBeginFade` (+0x458). **`OnEndFade` and `ReverseFade` do not
  exist in VtMB** — neither string is in `vampire.dll`; the `ReverseFade` input 4.5 carried over
  from a later stock SDK is removed. The same pass corrected the fade curve, which 4.5 had
  inverted: `SF_FADE_STAYOUT` is what makes the screen come *back*, not what holds it out (see
  the decision log). *Acceptance met:* `elysium.ent_fire teleport_fade Fade` fires all six
  `OnBeginFade` wires with their authored delays, covers over `duration 1`, holds `holdtime 2`,
  uncovers over another second, and lands the player in the downtown alley — verified frame by
  frame under `-benchmark -fps=10 -dumpmovie` (cover frames 31→40, black 40→60, uncover 61→70)
  with the HUD readout going `src (-14, 7493, -106)` → `src (-256, -176, 25)`, i.e.
  `teleport_player`'s `-256 -176 -32` plus the capsule lift. `teleport_jack.Teleport` resolves
  and no-ops, and Jack's own inputs still land as `[no input]` — both are B3. *Deps:* none.

- [x] **B2 Real entity objects in CPython** *(= 9.3's "do `Entity.__getattr__`/`__setattr__`
  first" slice)* — landed. The bootstrap's `_StubEnt` is gone; `vampire` now carries a real
  **`Entity`** type (`ElysiumPythonEntity.cpp`) plus **`Player`** and all **11 module globals**.
  `Entity` holds a generation-checked handle, not a pointer, so a reference kept across a `Kill`
  or a map travel reports retail's `AttributeError: game entity has been deleted` (verified: a
  stashed `teleport_fade` reprs as `#1366 <stale>` after travelling to `sm_pawnshop_1`, and any
  attribute on it raises). `__getattr__` resolves the type methods + instance `__dict__` first
  (what an old-style class does before calling `__getattr__` at all), then walks the P1
  class-chain tables — an **input** name manufactures a bound callable that fires through
  `EnqueueInput` (so a script call, `ent_fire`, and a map's own wire are one path), a **field**
  name marshals the live value. `__setattr__` is the documented mirror: datamap first, instance
  `__dict__` only on a miss; a non-keyable field *or an input* is `"<name> is read only"` (letting
  an input reach the property bag would shadow it permanently). The 13-entry base-method table is
  there with its readers live (`GetOrigin`/`GetCenter`/`GetAngles`/`GetAngleVectors`/`GetName`/
  `GetModelName`/`IsAlive`, in Unreal cm — the runtime never converts) and its four writers
  recording a native-call stub. `FindPlayer()` returns a sheet-backed `Player`: `clan`, `base_*`
  and any loaded stat read as data (miss → 0, like `G`), every other name binds as a Character
  method, which is what makes retail's unlisted calls (`ClearActiveDisciplines`,
  `MakePlayerKillable`, `Bloodgain`) run instead of raising.
  Two supporting changes came out of it. **The native surface moved to
  `ElysiumScriptNatives.{h,cpp}`**, shared by both hosts — one table, one set of stub defaults,
  one Cog call-counter set, so `elysium.script.cpython 0/1` no longer changes what a name does.
  And **`G` gained its mapping protocol** (`G[k]`/`G[k]=v`/`len`), which the RE shows is literally
  the attribute path (see the decision log): without it `DialogPostProcess()` dies on its first
  line, inside `saveState()`. The `__main__` merge also settles 5.3's open `OneOfSet` precedence
  caveat by construction: a level script's `from vamputil import *` lands in that script's module
  dict, which merges over `__main__`, so the script definition shadows the native one, as retail
  does. *Acceptance met:* `elysium.py.firstbeat` (one token, survives
  `-ExecCmds`) seeds `G.Tut_Jack=1`/`Tut_Patch=0`, runs `DialogPostProcess()` through the installed
  host with no error, and reports `G.Tut_Patch=1` + `#1366 teleport_fade(env_fade) -> !self.Fade()`
  on the queue; the I/O log then shows all six `OnBeginFade` wires and
  `#1362 teleport_player(point_teleport).Teleport()` delivered at t=1.402 — B1's verified warp,
  now driven by the script instead of the console. `elysium.py.poc` still reports ALL PASS.
  Jack's inputs remain `[no input]` (B3). *Deps:* B1, 9.3a.

- [x] **B3 Minimal NPC presence** *(the 8.5 carve-out this beat needs)* — landed.
  `ElysiumNpcClasses.cpp` registers one AI-free character leaf **`FElysiumNpc`** (for `npc_VVampire`,
  `npc_VPedestrian`, `npc_VHumanCombatant`, `npc_VRat`, and 10 more living-NPC classnames; `npc_VCamera`
  is a bodiless camera control, left inert) and **`FElysiumNpcMaker`** (`npc_maker`/`npc_maker_fleshpile`).
  An NPC stands its real glTF skeletal body at its origin through the 8.2 path —
  `AElysiumMapActor::BuildNpcVisual` loads `out/npc/<stem>.glb` (stem = the model file's lowercased
  basename, verified 1:1 for every tutorial NPC — no manifest lookup), caches the `USkeletalMesh` + idle
  clip per stem (a model three Sabbat share loads once), and stands a movable `USkeletalMeshComponent` on
  the map actor playing the glb's idle clip (or the reference pose). `elysium.NpcBodies 0` A/Bs the bodies
  (I/O still resolves; only the visual is gated). It latches `WillTalk`/`UseInteresting`, and
  `StartPlayerDialogRemote` opens a dialog session (fires `OnDialogBegin`, keeps the param for B4); a
  manual **`EndDialog`** input (fireable via `ent_fire`, superseded by B4's `.dlg` runner) fires
  `OnDialogEnd` — Jack's `OnDialogEnd` wire runs `DialogPostProcess()` (B2) → warp #2. `npc_maker.Spawn`
  synthesizes one child NPC at runtime through the new **`FElysiumEntityWorld::SpawnRuntimeEntity`** (a
  runtime def stored past the map's immutable def array; the child takes its class from `NPCType`, name
  from `NPCTargetname`, and the maker's `model`) — ignoring `Flag_StartDisabled`/`SpawnFrequency`/
  `MaxLiveChildren` (retail fires `blueblood_maker.Spawn` without enabling it; No AI). The Cog NPC window
  gained a "Live NPCs" section over the entity world. *Acceptance met:* on `sp_tutorial_1`,
  `elysium.classes npc_VVampire`/`npc_maker` resolve; Jack + the Sabbat/sheriff stand their models;
  `ent_fire Jack WillTalk 1 / StartPlayerDialogRemote 256 / EndDialog` deliver (not `[no input]`), the last
  warping the player; `ent_fire blueblood_maker Spawn` produces `blueblood(npc_VPedestrian)` at the maker
  origin (the downtown alley). `Elysium.Substrate.Npc` proves the registry, the runtime spawn, the WillTalk
  latch, and the OnDialogBegin fire on a bare world. **Feeds but does not close 8.5** (its
  `scripted_sequence` anim-at-marker ×51 and animation-bank retargeting stay open). *Deps:* 8.2, PL4 [x].

## P0 — Ground truth & de-risk

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

- [x] **2.7 Agent-facing MCP surface** *(design: `debug-tooling.md` Layer 3)* — the debug
  layer's console verbs get a third consumer beside the human (Cog) and `-ExecCmds`: an
  MCP server an AI agent (Claude Code, Cursor, the MCP Inspector) drives over loopback HTTP.
  **`UElysiumMcpSubsystem`** (`Private/ElysiumMcpSubsystem.{h,cpp}`, a `UEngineSubsystem`)
  registers **20 `elysium_*` tools** through the engine's experimental `ModelContextProtocol`
  plugin via `IModelContextProtocolModule::AddTool()` — the direct-registration path, so the
  tools work in `-game`/PIE/cooked, not only the editor-only Toolset-Registry adapter. Tools
  (`Private/ElysiumMcpTools.cpp`) are thin structured wrappers over the same runtime state the
  Cog windows render, resolving the live world at **call** time (one stable tool list across
  travel/PIE/idle): map lifecycle (`maps_list`/`map_load`/`map_reload`/`new_game`), player
  (`player_get`/`teleport`/`noclip`), entities (`entity_list` w/ histogram + paging /
  `entity_get` full chain+fields+outputs / `entity_fire` through the real `EnqueueInput`
  chokepoint), queue (`queue_get`/`pause`/`step`), `io_history`, `script_eval` + `g_dump`,
  `audio_state`, `screenshot` (viewport PNG, overlay excluded, shared capture path with 2.9),
  and the escape hatches `console_exec` (runs any verb, merges Ar + log-tap output) +
  `log_tail` (`FElysiumLogTap`, a 2,000-line always-on `FOutputDevice` ring). **On by default in
  dev builds** — auto-starts wherever the (editor-only) plugin is present; `-NoElysiumMcp` opts out,
  `-ElysiumMcp=<port>` pins a port, `elysium.mcp.start`/`stop` toggle it live; binds `127.0.0.1`
  no-auth. An agent connects via the repo-root `.mcp.json` (server `elysium`, port 8000).
  Editor-gated dep (`ELYSIUM_WITH_MCP`, `.uproject` pins the plugin + `AutomationTestToolset` +
  `LiveCodingToolset` to the Editor target); compiles to an empty shell elsewhere. Verified
  end to end: server binds, 20 tools list, `player_get` round-trips live data, `entity_fire`
  drives the substrate. *Deps:* 2.3.

- [x] **2.8 Automation tests (both tiers)** — the first automation suite, inside the module
  (`Private/Tests/`, no `ELYSIUMUE_API` needed to reach the substrate). **Content-free tier**
  (`ElysiumSubstrateTests.cpp`, app-context mask so it runs under `-nullrhi`): variant coercions
  + truthiness, `ElysiumExpr` eval incl. the error-to-false contract (RE3), the KeyValues reader
  (multi-line quoted value, nesting, comments, present-but-empty), event-queue ordering/FIFO/
  cancel/pause-step, the class-registry case-folded base-chain walk, and one **end-to-end I/O
  chain** (`logic_relay`→`math_counter` on a bare `FElysiumEntityWorld`, no PIE/content — drives
  both chokepoints + the "falsy when dead" identity contract). **Content-gated tier**
  (`ElysiumContentTests.cpp`): parses the real exported `sp_tutorial_1.ents` (1,868 records / 80
  classnames — asserts a band, load-bearing classes, the `tutorial` landmark) and
  `sm_pawnshop_1.ents` (parses + offers a travel landmark); **self-skips** (logs + passes) when a
  map is unexported, so an empty `tools/out` stays green. `test.bat` drives
  `Automation RunTest Elysium.<...>` headless with a JSON+HTML report. All 8 tests pass. *Deps:* 1.4, 5.2.

- [x] **2.9 Screenshot-regression harness** — `FElysiumShotRun`
  (`Private/ElysiumShotRun.{h,cpp}`), sibling to `FElysiumProfileRun`: `-ElysiumShots` self-drives
  once the boot map settles, visits the **same fixed vantages as the profiler** (extracted into the
  shared `ElysiumVantages.h`, so a look regression and a cost regression line up frame-for-frame),
  pins the camera, settles (Lumen accumulation + shader compile), captures the viewport (overlay
  excluded) to `tools/out/_shots/<map>/` + a manifest, and exits. Baselines are derived from the
  user's own install → gitignored, never committed. Shares `ElysiumScreenshot.{h,cpp}` (deferred
  capture + PNG encode/save) with the `elysium_screenshot` MCP tool. `shots.bat` mirrors
  `profile.bat`. Verified: a real 2560×1392 rendered frame written. *Deps:* 0.1, 2.7.

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
  **`env_fade`** (`FUN_10100e10` — the `Fade` full-screen colour fade, rendered by `AElysiumHUD` off a
  single screen-fade state on the entity world; its output, spawnflag mapping and fade curve are B1),
  **`func_brush`** (`FUN_1013dd30` — Enable/Disable/Toggle + `Solidity`
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

- [x] **4.6 `trigger_changelevel` + landmark travel** — cross-map travel through a shared
  `info_landmark`, translation-only (level_transitions.md path 2). **`trigger_changelevel`**
  (`ElysiumStarterClasses.cpp`, a `CBaseTrigger` leaf grounded in the decompiled `CChangeLevel`
  `FUN_101c71f0`/TouchChangeLevel `FUN_101c7890`/FindLandmark `FUN_101c7690`) carries `map` +
  `landmark` keyfields; a player touch (unless `SF_CHANGELEVEL_NOTOUCH 0x2` — the scripted-only bit
  the tutorial's changelevels carry) fires `OnChangeLevel` (the field-5 Python exit hooks, e.g.
  `werewolfBloodHavenExit()`), captures the player's offset from the **source** `info_landmark` +
  their view yaw, and requests a deferred landmark travel. It drops `CBaseTrigger`'s ALLOW_CLIENTS
  (0x1) gate — a changelevel's own Touch fires for the player directly. **`info_landmark`** is now a
  first-class leaf (inspectable; fires `OnEnterMapHere` when entered here). Travel can't run inline
  (it destroys the entity world mid-touch + force-GCs), so `UElysiumMapSubsystem::RequestLandmarkTravel`
  queues it and runs it on a **next-tick timer** (`SetTimerForNextTick`); the fresh map's
  `AElysiumMapActor::ResolveLandmarkSpawn` seats the player at `dest_landmark.Origin + offset` (view yaw
  preserved), fires the dest landmark's `OnEnterMapHere`, and falls back to `info_player_start` if the
  landmark is missing. The **scripted path** is real: `ChangeMap(delay, landmark, trigger)` (was a 5.3
  stub) enqueues the named trigger's `ChangeLevel` input through the event queue (`ElysiumExpr.cpp`); the
  same input is the console/debug force-fire. `point_teleport` already shares the player-placement seam
  (`GetPlayerPawn`). `elysium.map <map> [landmark]` gains an optional landmark for a direct/console entry
  (offset zero, lifted onto the landmark, facing its angles). **Debug:** a **Transitions** section in the
  `Elysium.Maps` Cog window (each changelevel → dest @ landmark with a "Change now" fire button, the
  landmarks + origins, a pending-travel banner, "Entered via"), `GetDebugState` for both classes in the
  Inspector, and the transition I/O in the Event Queue window. **Verified headless:** firing
  `trig_leave_tutorial_short.ChangeLevel` on `sp_tutorial_1` queued the travel (offset from the source
  `newgame`), the next-tick swap loaded `sm_pawnshop_1` and placed the player at `dest_newgame + offset`
  (`-4945,25212,873`) with `newgame(info_landmark) -> Radio2.Deactivate()` firing; the direct entry
  `elysium.map sm_pawnshop_1 newgame` seated the player at the landmark `(-5003.8, 6568.4, 388.6)+100`
  lift and released on ground. *Deps:* 1.6, 0.3.

- [x] **4.9 Event-bus classes (`events_player` / `events_world`)** — the two singleton entities
  every map carries, and the seam level scripts hang their world/player event callbacks off
  (0.8 flagged them as top output sources; no task had owned them). Both land as plain-C++ leaves
  in `ElysiumEventClasses.cpp`. **Datamaps recovered from the decompile** (new `DumpDatamap.java`;
  see the Ghidra findings below): **`CPlayerEvents`** (factory `FUN_10226630`, vftable `0x1048d844`,
  datamap `0x105b28c0` → 36 records) = **12 inputs** (EnableOutputs/DisableOutputs,
  CreateControllerNPC/RemoveControllerNPC, AwardExp, ClearDialogCombatTimers, ImmobilizePlayer/
  MobilizePlayer, RemoveDisciplines/RemoveDisciplinesNow, MakePlayerUnkillable/MakePlayerKillable),
  **23 outputs** (OnFrenzyBegin/End, OnWolfMorphBegin/End, OnPlayerTookDamage, OnPlayerKilled,
  OnPlayerSoundLoud, 16 × `OnActivate<Discipline>`), one keyfield `enabled`;
  **`CWorldEvents`** (factory `FUN_1023cf80`, vftable `0x10496acc`, datamap `0x105c2a50` → 31
  records) = **10 inputs** (SetSafeArea, SetCopWaitArea, SetCopGrace, SetNosferatuTolerant,
  SetNoFrenzyArea, AIEnable, FadeGlobalWetness, Hide/UnhideCutsceneInterferingEntities,
  PlayEndCredits) and **21 outputs** (cop pursuit/alert, 5 × masquerade level + changed,
  OnPlayerHasNoBlood, combat/alert/normal music start+end). Both chain to the shared CBaseEntity
  map `0x10552e18`, which is where the `OnUseBegin`/`OnUseEnd` the tutorial's `world` wires come
  from. **Scope:** the input/field surface is complete and faithful and policy state is latched +
  surfaced through `GetDebugState`; the *outputs* are fired by systems that don't exist yet
  (disciplines, frenzy, morph, cop/masquerade AI, music scoring), so this lands the bus, not its
  drivers — each such input records state and logs rather than silently no-opping.
  **Verified headless on `sp_tutorial_1`:** `elysium.classes` resolves 15/13 chain inputs
  (12/10 own + 3 base) and 25/24 fields; the `logic_auto` `OnMapLoad` chain now delivers
  `pc_0(events_player).MakePlayerUnkillable()` and `world(events_world).SetNoFrenzyArea(1)`
  through the real queue instead of hitting `[no input]`. *Next (not in scope here):* fire
  `events_world`'s six music outputs from the P6.3 `EElysiumMusicState` machine, giving it its
  first real driver. *Deps:* 1.6.

- [~] **4.10 `game_sign` / `prop_sign` — sign windows** — **`game_sign` + PL5c landed; `prop_sign`
  and the pixel-faithful panel remain.** Shipped: `UE_extract_signs.py` mirrors all 278 definitions
  + decodes the 57 background materials into `out/signs/`; `ElysiumKeyValues.h` is the shared Source
  KeyValues reader (lifted out of `ElysiumSoundScheme.cpp`, tokenizer rewritten as a character
  stream — 127 of the 187 loose definitions carry a quoted value spanning lines, which the old
  line-based scan truncated); `FElysiumSignData` parses `SignData` + the first-true
  `Sign { dependency; filename }` redirect through `EvalCondition`; `game_sign`
  (`ElysiumSignClasses.cpp`) implements `OpenWindow`/`CloseWindow`/`ChangeFile` +
  `OnUseBegin`/`OnUseEnd`; the panel rides one open-sign state on `FElysiumEntityWorld` (the
  `env_fade` pattern) and `AElysiumHUD` draws background + word-wrapped text blocks, dismissed by
  left-click through `PlayerDismissSign` (honouring `CloseOnLeftClick` + `MinShowTime`).
  **The draw model is RE-verified** — see the decision-log entry below. Verified on the tutorial:
  `popup_3` → click → `OnUseEnd` → `popup_4` through the real chokepoint, and the panel rect
  measures within 1–2 px of prediction on all four edges. Still open: `prop_sign` + its `+use`
  path, `NewspaperData`/multi-column, `ClientCommand` execution, real `.fnt` type (8.8), and
  `fade_out` (dismissal tears the panel down immediately — a fading-out panel needs a lingering
  copy). Original scope follows.
  **73 `game_sign`** (71 of them on
  `sp_tutorial_1`, named `popup_1`…`popup_59`) + **100 `prop_sign`** across the patch map set (35 maps
  carry signs); today every one is an inert record, so the **51 `OpenWindow` wires** (+4 `Kill`) drop
  on the floor and `tutorial.py`'s beat machine — `Find("popup_11").OpenWindow()` ×23 plus `ChangeFile`
  ×22 in `SetClanPopups()` — has nothing to drive once 9.3 runs the level scripts. Both land as leaf
  classes over `FElysiumEntity`: keyfields `definition_file` + `fade_in`/`fade_out`/`pause`/`spawnflags`
  (`game_sign`, bodiless) and `model`/`skin`/`use_icon`/`crossfade_skin_time` (`prop_sign`, whose model
  8.1 already exports); inputs **`OpenWindow`/`CloseWindow`/`ChangeFile`**; outputs
  **`OnUseBegin`/`OnUseEnd`/`OnReadBegin`**; `prop_sign` opens on `+use` through the 4.4 look-cursor
  (its `use_icon`s — 36 × 18 `note`, 31 × 41 `printedpapers`, 22 × 56 `bustopmap` — already resolve to
  HUD reticle icons). **Definition-file loading is RE-grounded** (`run.ps1 -Script DumpGrep` on
  `vampire.dll`): `CGameSign::LoadSignData` `FUN_10212da0` (factory `FUN_10212430`, object `0x474`,
  `definition_file` @`+0x454`) and `CPropSign::LoadSignData` `FUN_10212200` (factory `FUN_102118a0`,
  object `0x770`, @`+0x730`) are the same routine — parse the file as KeyValues rooted **`SignData`**,
  walk its `Sign` sub-blocks, and take the **first** whose `dependency` string evaluates true
  (`PyRun_String(dep, 0x102 = Py_eval_input)`, the RE3 error-to-false eval), redirecting to that
  block's `filename`. Those dependencies are plain `G` reads (`"G.Story_State == 20 and G.Dane_Kills
  >= 3"`), i.e. already within `ElysiumExpr` via the installed script host; **24 of the 278 sign files**
  are such dispatch wrappers (`newspaper_all.txt` is the pattern). The panel this task draws is the
  **substrate-grade** one: parse `SignData` with the existing runtime KeyValues reader
  (`FElysiumSoundScheme`'s), draw `BackgroundImage` + the `TextBlock`/`Label` boxes on `AElysiumHUD`
  (Canvas, following the `env_fade` screen-state pattern), honour `fade_in`/`fade_out`, and close on
  left-click. Full VGUI fidelity is **8.8**. **To settle while implementing:** `pause` (`1` on all 71
  tutorial popups) is presumed *pause the game clock while open* — confirm against `CGameSign`; and
  `game_sign` `spawnflags 5` is undecoded. **Acceptance** on `sp_tutorial_1`: `elysium.ent_fire popup_2
  OpenWindow` shows the blood-pool panel; walking into `trig_popup_feed` opens `popup_3` and its
  `OnUseEnd` chains `popup_4`; `+use` on `sign_chopshop_upstairs` reads *"password: chopshop"*; and a
  sign whose file is a dispatch wrapper picks its variant from `G`. *Deps:* 4.4, 1.6, PL5c; 5.2 for the
  `dependency` redirect.

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
  truth (membership tests + the debug view) — B2 moved it, with the stub dispatch and the call log,
  out of `ElysiumExpr.cpp` into `ElysiumScriptNatives.{h,cpp}` so the CPython host shares it. Debug: the `Elysium.Scripting` Cog window grows a
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
  music-state buttons + per-anchor FadeIn/FadeOut) and the **Audio window's live-voices table**, which also
  carries the **global mute** (`elysium.Mute`, default 1 = muted — a gain gate over the whole voice pool,
  previews included; voices keep running at zero gain so unmuting rejoins the ambience mid-stream). Music-stem
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

- [x] **7.2 Decals** *(M2)* — VtMB `infodecal`s as **deferred `UDecalComponent`s** (owner call:
  went straight to the projection path, skipped the PMC-parity stage — a deferred decal writes the
  GBuffer before lighting, so it is lit exactly like its host wall, Lumen indirect included, which
  the PMC translucent path cannot do). **Offline:** the exporter's decal projection
  (`UE_bsp_to_scene.py`) now discards the clipped mesh and instead writes a **`<map>.decals`**
  projector sidecar — one line per placed decal, `material + centre + room-normal + s/t axes +
  half-extents`, all Unreal cm (point via `source_to_unreal`, unit dirs via `source_dir_to_unreal`,
  extents inch→cm); the materials still ride the shared `<map>.mtl` (`map_Kd`/`map_Ke`/`decal 1`).
  **Content:** a new `M_Decal` master (`tools/make_decal_material.py`, in the `build_content.py`
  umbrella): deferred-decal domain, `DBM_TRANSLUCENT`, `Albedo` RGB→BaseColor + A→Opacity, plus the
  `M_VtMB_World` selfillum path (`Emissive`×`EmissiveScale`). **Runtime:** `FElysiumDecals` parses
  the sidecar; `AElysiumMapActor::BuildDecals` spawns one `UDecalComponent` per line with a MID off
  `M_Decal` via `FElysiumMaterialFactory::BuildDecal`. Orientation: `MakeFromXZ(Normal, SDir)` — local
  +X = the room normal (so −X projects into the wall), and because a deferred decal maps texture
  **U→local Z, V→local Y**, the surface horizontal `SDir` goes on local Z with
  `DecalSize = depth×HalfH×HalfW` (`FadeScreenSize 0`; `elysium.DecalFlipU` mirrors U).
  `elysium.Decals` (default 1) A/Bs the pass, `elysium.DecalDepth` tunes projection depth; the Cog
  Maps/Status windows show the count. **Tests:** `Elysium.Substrate.Decals` (parser + orientation
  math, nullrhi) + `Elysium.Content.TutorialDecals` (real sidecar vs the `.mtl`). *Deps:* 0.4.

- [x] **7.4 Master-material set (rest)** *(was M1.2)* — the four world masters, authored offline like
  `M_Sky` and selected per surface at runtime by the OBJ material's blend flags. **Content:** one new
  generator `tools/make_world_materials.py` (in the `build_content.py` umbrella; retired
  `add_world_emissive.py` + `set_world_material_usage.py`) builds all four from a shared graph —
  `M_World_Opaque` (grown from the old `M_VtMB_World`, which it deletes), `M_World_Masked`
  (`BLEND_Masked`, two-sided, `OpacityMask=Albedo.a`), `M_World_Translucent` (`BLEND_Translucent`,
  per-pixel lit, `Opacity=Albedo.a`), and `M_Additive` (unlit `BLEND_Additive`, `Albedo`→Emissive). The
  three lit masters carry the full feature set as named runtime params: `Albedo`, `Emissive`+
  `EmissiveScale`, `BumpMap`+`BumpAmount` ($bumpmap), `EnvMask`+`EnvStrength` ($envmap → **Lumen
  roughness**, see the decision-log entry — *not* a baked-cube sample), `BaseTex2`+`BlendAmount`
  (WorldVertexTransition, `lerp(Albedo, BaseTex2, VertexColor.r × BlendAmount)`). **Offline:** the
  exporter already emitted every MTL field; added only `additive 1` (from `$additive`, alpha kept).
  **Runtime:** `FElysiumMaterialDef` gains `Bump`/`EnvMask`/`BaseTex2`/`bAdditive`/`bEnvmap`;
  `ParseMtl` reads them; `FElysiumMaterialFactory::Build` picks the master (additive→translucent→
  masked→opaque) and binds params by fixed name, switching each feature scalar on only where its
  channel is present (uniform white `EnvMask` when a reflective surface carries no mask). Normal maps
  and reflectivity masks load **linear** (`FElysiumTextureCache::LoadTex(..., bSRGB=false)`). WVT weights
  ride the existing `<map>.blend` sidecar → per-section vertex `COLOR.r` via `CreateMeshSection` (FColor
  overload, no gamma round-trip); the mesh cook-cache (`.emc`) bumped to `EMC2` for the added Colors.
  `elysium.BumpScale` / `EnvReflect` / `EmissiveScale` A/B the three scalars. Both the world mesh and the
  prop ISMs go through the factory, so prop glow panes (`$additive`, emitted by `mdl.py`) now render
  additive. **Tests:** `Elysium.Substrate.WorldMaterials` (parse of every channel/flag + blend-flag
  exclusivity, nullrhi) + `Elysium.Content.TutorialMaterials` (real MTL: 439 mats — 12 masked, 52
  translucent, 240 reflective, 6 bump, 5 WVT; every texture channel resolves on disk). *Deps:* none.

## P8 — Characters & UI *(design: `rebuild-strategy.md` B5, `remaster-direction.md` axis 1; `m0_menu_build.md` = structural reference, not a port target)*

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

- [x] **8.3 Dynamic props** — the per-entity static-mesh render path for `prop_dynamic` /
  `prop_dynamic_ornament`, closing the 9.3 "bodiless prop" caveat (a leafless prop parsed as a
  logic-valid but invisible record).
  - **Render path** — `AElysiumMapActor::BuildPropVisual(Stem, Location, Quat)` mirrors
    `BuildNpcVisual`: parse `props/<stem>.obj` once, build a `UStaticMesh` through the existing
    `FElysiumStaticMeshBuilder`, cache it per stem (`PropMeshCache`, GC-rooted on the map actor so a
    model placed by several prop entities builds once), and stand a movable `UStaticMeshComponent`.
    **Non-solid** (`ECollisionEnabled::NoCollision`) — 8.3 is visual parity; prop_physics Chaos
    collision is 8.4 (`entity_visuals.md` R2 "collision optional for visuals").
  - **Per-entity, not shared ISMs** — the roadmap text pointed at the Godot R2 MultiMesh design
    ("the ISM path grows per-instance addressability"), but every runtime writer here is per-entity
    (hide = `SetVisibility`, move = `SetRelativeLocationAndRotation`, model-swap = destroy+rebuild), so
    an individual component per prop (sharing the per-stem cached mesh) is the lower-risk fit. Geometry
    is trivial (~20k tris/map; the perf constraint is lights), so ~160 extra components is negligible.
    GAME_LUMP static props stay on their existing grouped-ISM `.props` path (no identity, no double-draw).
  - **The `FElysiumProp` leaf** (`ElysiumPropClasses.cpp`, registered for both classnames): `Spawn()`
    stands the body from the export's `model_mesh` stem + `model_quat` rotation, honouring `start_hidden`
    via a `GateVisual` mirror of the NPC pattern; overrides `OnRuntimeTransformChanged` (follow — yaw-only
    re-face, like the NPC leaf, since a script `SetAngles` carries no pre-converted quat),
    `OnRuntimeModelChanged` (tear down + rebuild from the new model's basename), and `OnDormancyChanged`
    (ScriptHide/Kill → undrawn). Inputs: `Break` (hide the body + fire `OnBreak`, idempotent);
    `Skin`/`SetAnimation` are **logged stubs** — the prop decode is LOD0 static geometry, skin 0 only, so
    faithfully they can only record the request until a skin-family / skeletal-prop export exists (a
    tracked follow-up). The `skin` keyfield is registered read/write (closes the 9.3 `.skin`-on-prop note).
  - **Teardown** — `FElysiumEntityWorld::RegisterPropBody` + a `PropBodies` weak-ref list destroyed in
    `Teardown`, exactly like `NpcBodies`, so a world rebuild on a surviving actor (reload) does not leak.
  - **Orientation at export, read verbatim** — `.ents` left `angles` as a raw Source string (only
    `origin` was pre-converted). Rather than convert at runtime, `UE_bsp_to_scene.py::write_entities` now
    also emits `model_quat` = `source_angles_to_unreal_quat(angles)` (the same function the `.props` path
    uses) for every `model_mesh` entity; `FElysiumEntityDefs::Parse` reads it into `Def.ModelQuat`
    (identity when absent, so an older export still loads — props just unrotated). Honours the load-bearing
    "convert at export, never at runtime" rule.
  - **Verified** — `build.bat` green; the tutorial re-exported (78 `prop_dynamic`, all with
    `model_mesh` + `model_quat`); `test.bat Content` + `Substrate` green, including a new
    `Elysium.Content.TutorialEnts` assertion (prop_dynamic registers, records carry `model_mesh`, a
    sample stem resolves to an OBJ on disk); a headless `shots.bat sp_tutorial_1` load builds all prop
    bodies without crash and the captures show the placed models (cage-lights, palms, beds, barrels, the
    ceiling fan) standing. `elysium.PropBodies 0` A/Bs the bodies off (I/O still resolves). *Deps:* 8.1, 1.3.

- [x] **8.4 Physics props** — `prop_physics` (54 in the patch tutorial, 18 unique models) as simulating
  Chaos rigid bodies and `phys_hinge` (12) as hinge constraints. A "straight Unreal win" — the simulation
  is Unreal-native; only the I/O surface is reproduced.
  - **RE (Ghidra, `vampire.dll`)** — datamaps recovered by vtable (CPhysicsProp `0x10474c44`, CPhysHinge
    `0x10447a54`; `DumpGrep`/`DumpDatamap` over the persisted `vtmb` project). **Key divergence from later
    Source:** CPhysicsProp/CBreakableProp has **`Wake`** (`InputWake`), `Break`, and the skin inputs — no
    `EnableMotion`/`DisableMotion`/`OnMotionEnabled` (added later) and **no `InputSleep` symbol exists** in
    the binary (every `Sleep` hit is NPC AI). Outputs: `OnBreak` + the undriven `OnBreakLevel1..8`/
    `OnBreakLastLevel`/`OnBreakConstraint` gib chain. CPhysHinge/CPhysConstraint: fields
    `attach1`/`attach2`/`forcelimit`/`torquelimit`/`hingefriction`/`hingeaxis`; inputs `TurnOn`/`TurnOff`/
    `Break`; output `OnBreak`. Full record: `decisions.md` 2026-07-24.
  - **Exporter** — `UE_bsp_to_scene` emits `hinge_axis` (normalized `source_dir_to_unreal` of the raw-Source
    `origin`→`hingeaxis` line; pivot = the converted top-level origin) for `phys_hinge`. `prop_collision.py`
    convex-decomposes each `prop_physics` model (**CoACD**, optional dep) into `props/<stem>.hulls` in the
    world-collider format (one hull per line, flat Unreal-cm verts). Params tuned for pipeline speed
    (`threshold=0.2` + lowered MCTS/voxel resolutions — CoACD's default 0.05 cost ~150 s on one 2.5k-vert
    chair; the tuned set is ~10 s for an 8-hull proxy), plus a **skip-if-fresh cache** (`.hulls` newer than
    its `.obj` is reused) so a re-export is instant and `export_all` pays per model once. CoACD absent/failing
    → single whole-model hull line (the baseline spec) — the pipeline never hard-fails.
  - **Runtime** — `FElysiumPhysProp` (`ElysiumPropClasses.cpp`) stands a per-entity `UStaticMeshComponent` via
    `AElysiumMapActor::BuildPhysPropVisual` (mesh cooked with convex collision from the `.hulls`, one
    `FKConvexElem` per line, else a single whole-model hull; cached under a `#phys` key so a model shared with
    a non-solid `prop_dynamic` doesn't clash), `PhysicsActor` profile, `SetSimulatePhysics` + `override_mass`
    (>0 overrides, −1 keeps computed). `FElysiumStaticMeshBuilder::Build` gained a hull-list param +
    `LoadConvexHulls`. `Wake`→`WakeAllRigidBodies`, `Break`→hide+`OnBreak`, skin inputs are stubs (skin 0 only
    exported). `FElysiumPhysHinge` builds a `UPhysicsConstraintComponent` (twist on `Def->HingeAxis`,
    swings/linear locked → one DOF; `forcelimit`/`torquelimit`=0 → unbreakable) in a new **`PostSpawn()` pass**
    (`FElysiumEntityWorld::Load` second loop = Source's `Activate()`, run after every entity spawns so both
    attach bodies exist), wiring `attach1`↔`attach2`/world via `SetConstrainedComponents`; `TurnOn`/`TurnOff`/
    `Break`, `OnBreak`. `FElysiumEntity::GetAttachBody` is the seam (base = brush body; phys prop = simulating
    mesh). Teardown: `RegisterConstraintBody` + the existing `RegisterPropBody`. World `.hulls`/`.dispcol`
    colliders are `BlockAll`, so bodies rest on the floor. `elysium.PhysicsProps` A/Bs simulation (0 = static
    non-solid, visual parity).
  - **Deferred (recorded)** — physics-driven constraint break firing `OnBreak` (the `OnConstraintBroken`
    delegate needs a UObject; the plain-C++ leaf fires `OnBreak` only on the explicit input); the gib
    `OnBreakLevel*` chain (no decomposition-into-pieces system); runtime multi-convex for later maps if
    concave furniture becomes gameplay-relevant (the offline path already covers it).
  - **Verified** — `build.bat` green; re-export writes `hinge_axis` (12/12) + 18 decomposed `.hulls`
    (66 hulls: `bottle`→1, `chairoffice`/`retro_chair`/`trashgarage` multi-hull); `test.bat` Content +
    Substrate green with a new `Elysium.Content.TutorialEnts` assertion (prop_physics/phys_hinge register,
    prop_physics carry `model_mesh` + a `.hulls` sidecar on disk, phys_hinge carry `hinge_axis`); a
    timeboxed `play.bat sp_tutorial_1` load built all bodies + all 12 hinges (`attach1 <-> world`) with no
    crash (`world 'sp_tutorial_1' live: 1868 entities`). The Chaos settle/push feel + hinge swing await an
    owner in-game play test (like 4.1 — physics feel is the one thing headless coverage can't judge).
    *Deps:* 8.1.

- [~] **8.5 NPC presence + `scripted_sequence` minimal** — spawn `npc_*`/`npc_maker` at
  origins via glTFRuntime; play-anim-at-marker handler (×51) long before real AI.
  *Presence slice landed in **B3*** — `npc_*`/`npc_maker` register, stand their glTF body at origin
  (`AElysiumMapActor::BuildNpcVisual`, per-stem cache), `npc_maker.Spawn` creates the child via
  `FElysiumEntityWorld::SpawnRuntimeEntity`, and the dialog-gating inputs latch. **Remaining:** the
  `scripted_sequence` play-anim-at-marker handler (×51) and animation-bank retargeting.
  *Pipeline done (PL4):* the batch emits per-NPC mesh glbs + shared animation-bank glbs +
  `out/npc/npc_manifest.json` (`clip → owning-stem`). The bank work is loading a clip's bank glb
  (cached, shared) and applying it to the NPC skeletal mesh by bone name
  (`bank->LoadSkeletalAnimationByName(npcMesh, clip)`) — the manifest hides the include mechanism.
  *Deps:* 8.2, PL4 [x].

- [x] **8.6a New Game context + story entry** *(carve-out of 8.6, so the game context isn't blocked
  behind the UI work)* — the minimal state a fresh story run starts from, and the boot path that
  enters the tutorial in it. `FElysiumPlayerSheet` (clan in the level-script 2..8 encoding, gender,
  an open `Stats` map 9.4 fills from `vdata/system/*.txt`) + `UElysiumGameStateSubsystem::BeginNewGame`
  seed it: `Story_State=-4` (the intro spine's post-theatre value), `Tut_Jack=0`, `Tut_Patch=0`
  (arms the patch's beat-1 relocation), `Linux_Wine=1` — the last set by **both** `vamputil.setBasic()`
  and `setPlus()`, i.e. by the patch-type selection, without which `logic_pythoncheck linux_check`
  fires `OnFalse -> popup_linux`. The `Patch_Plus` family is left at `G`'s default 0 = the patch's
  "Basic" profile. `UElysiumMapSubsystem::NewGame` then travels to the story entry —
  **`sp_tutorial_1` @ the `tutorial` `info_landmark`, offset zero** — the same landmark a real
  `trigger_changelevel` from `sp_theatre` uses (4.6's path), so chargen + the choreographed intro
  (8.6 / P9) are stood in for rather than faked. **Boot default is New Game**: a bare launch starts
  the story; `-ElysiumMap=<name>` (`play.bat <map>`) keeps the unseeded dev path unchanged, and
  `-ElysiumNewGame=0` boots the story map bare for A/B. `elysium.newgame [clan] [m|f]` is the console
  form (clan by name or 2..8) and the seam 8.6's menu will call. **Verified headless:** default boot
  logs the seed, seats the player at `tutorial + 100 cm` lift (`-35.56, -19029.68, -296.24`, yaw
  -270) and releases on ground; `popup_linux` no longer fires; `-ElysiumMap=sm_pawnshop_1` still
  travels bare with no seeding. *Deps:* 4.6, 4.9, 1.1.

## P9 — Dialogue & persistence *(design: `game_runtime.md`, `rebuild-strategy.md` B7/B9)*

- [x] **9.3a Level-script wiring — CPython is the default host + auto-load at map load.**
  `UElysiumGameStateSubsystem::MakePreferredScriptHost` installs `FElysiumCPythonScriptHost` when the
  module carries the vendored SDK **and the interpreter actually starts**, falling back to the expr
  host otherwise (a dead VM would make every eval Void = error-to-false, indistinguishable from a
  working host and silently wrong). `FElysiumEntityDefs::LevelScriptModule()` reads
  `worldspawn.levelscript` off the **parsed defs**, and `AElysiumMapActor` imports it through
  `UElysiumGameStateSubsystem::LoadLevelScript` **before the spawn pass** — VtMB's own order, so the
  module's top-level code (constants, `from vamputil import *`, the `On*` defs) is in place before any
  entity can evaluate a field-6 payload against it. Importing is a host capability
  (`IElysiumScriptHost::LoadLevelScript`, default "cannot import"), and the module name is remembered,
  so `SetScriptHost` re-imports into any newly installed host — swapping hosts mid-session never
  leaves the new one with a bare `__main__`. `EvalScript` (the `elysium.eval`/`exec` verbs + the Cog
  eval box) now routes through the **installed host** instead of hard-wiring ElysiumExpr, so a
  hand-run eval resolves exactly what a field-6 payload resolves; `IElysiumScriptHost::Eval` gains an
  optional `OutError` for it (a return value alone cannot separate "evaluated to None" from "raised").
  Verified in the built game on `sp_tutorial_1`: `tutorial` imports at map load into host `cpython`,
  `elysium.eval cCelerity` = **8**, `G.Tutorial_Discflags |= cCelerity` flips `G` to **8** — the 5.5
  acceptance, now on the default path — `elysium.script.cpython 0` A/Bs back to expr (NameError again),
  and travel re-imports per map. *Deps:* 5.5.

## Ghidra extraction — findings + plan *(pass dated 2026-07-22; scripts + dumps in `tools/ghidra/`, `out/re*.txt`)*

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

**RE4 — `DumpDatamap.java` landed (the batched extractor the plan called for).** Give it
`map=<datamap_t>` or `vtable=<vftable>` (it reads slot `+0x148`, follows the 5-byte JMP thunk, and
parses the `MOV EAX,imm32; RET` body) and it walks the 44-byte `typedescription_t` records —
external/internal name, fieldType, offset, flags, `inputFunc` — splitting them into
inputs / outputs / fields and following the `baseMap` chain. **Key shape fact:** VtMB datamaps are
**half static**. The leading records (inputs + keyfields) are statically initialised in `.data`, but
the trailing output records *and the `datamap_t`'s own `dataDesc`/`numFields`* are written at
static-init by a builder function — so a raw image read reports `numFields 0` and the input-name
strings carry **no xrefs**. Workflow: `DumpXrefs` the `datamap_t` address → the WRITE site names the
builder → decompile it for the runtime-built output names, and read `_DAT_<map>+0 / +4` at its tail
for the record base + count → re-run `DumpDatamap` with `recs=`/`count=` for the static half.
Proven on `CPlayerEvents` (builder `FUN_10226700`, records `0x105b2904`, 36 = 14 static + 22 built)
and `CWorldEvents` (builder `FUN_1023d050`, records `0x105c2a94`, 31 = 11 static + 20 built) for 4.9.

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

**`OnEnterMapHere` — CONFIRMED `info_landmark`-only.** The string pair `OnEnterMapHere`
(`0x10559aa0`) / `m_OnEnterMapHere` (`0x10559ab4`) has exactly **one** referencing function in
`vampire.dll`: the `CBaseLandmark` datamap builder **`FUN_100b7220`** (the same map that declares
`OnSpawnOneCopCar` / `OnSpawnTwoCopCars` / `OnDelaySpawn*` / `OnCopsInPursuit` /
`OnHeightenedAlert`, at field offsets `0x468`–`0x4f8`, output fieldtype `10`). `point_teleport`
(`FUN_1018d940`, object `0x468`, vftable `0x10472e94`) is a plain `CPointEntity` and declares no
outputs — so the three exported `point_teleport` entities carrying an `OnEnterMapHere` wire
(`sp_tutorial_1`'s `teleport_very_beginning` ×2, `sm_hub_1`'s `sewerB2_street` ×2) are **inert map
data**: the keyvalue lookup finds no such output on the class and drops the wire. **4.6's placement
of the fire on the resolved `info_landmark` is therefore correct as shipped — no change.**
Consequence for the tutorial: nothing fires on arrival; the first beat is armed by
`trig_off_porch`'s `OnEndTouch` (`game_runtime.md` §4, corrected).

**RE5 (Ghidra-drivable, open):** optionally static-recover the vroll resolver in `vampire.dll` to
cross-check `recovered/dice-system.md` alongside the running-game golden test.
