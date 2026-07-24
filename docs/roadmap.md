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

Rebuild VtMB as a playable game **— remastered —** on UE 5.8 + C++, runtime-loading this repo's
exported intermediates — no game content in `.uasset`s, bring-your-own-game holds (strategy and
principles: `rebuild-strategy.md`). Everything is proven on `sp_tutorial_1` first
(1,226 entities, 75 classnames — VtMB's own vertical slice), then scaled across ~100 maps.

**Direction (`remaster-direction.md` — read it):** keep VtMB's tone, ambience, feel and logic;
raise the craft. **Presentation** (UI, type, HUD, textures, post) modernizes freely under the
art-direction test. **Feel** (movement, camera, combat) is built faithful first and polished
only by explicit call. **Logic and content** is reproduced — a behavioural divergence requires
the faithful behaviour to be RE'd and understood *first*, plus a dated owner decision in the
log below. Default is always reproduce. The UI has no classic mode (VtMB's screen structure,
re-skinned with vector type on a resolution-independent stack); the **world** keeps its
faithful baseline with enhancement as an A/B toggle.

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
| **P8 — Characters & UI** | NPCs stand in the world; New Game from a real, modern menu | P7 tail |
| **P9 — Dialogue & persistence** | **tutorial completable as retail**, save/load works | — |
| **P10 — Scale & ship-shape** | all maps, floor validated, packaged story | ongoing after P4 |

## The first-beat path (B*) — landing → the second warp point

The cross-phase priority ladder for the first *playable* game beat: the player lands on
`sp_tutorial_1` and the tutorial's opening runs unassisted up to the second warp. The tutorial's
"warp points" are its teleport stations. **Warp #1** is where the player already lands: the porch
at `teleport_very_beginning` (the `tutorial` `info_landmark` — 8.6a seats the player there).
**Warp #2** is the patch's relocation to the downtown alley: `teleport_fade` (an `env_fade`) fires
`OnBeginFade -> teleport_player.Teleport` + `teleport_jack.Teleport`.

**The data flow** (traced from `sp_tutorial_1.ents` + `tutorial.py`, patch flow):

1. **Map load** — `logic_auto.OnMapLoad -> unhidePlus()` → `ccmd.patchtype` → `setPlus()` arms
   `trig_popup_move` (`StartDisabled 1`). Dies today at the unbound `ccmd` (9.3b), so the
   movement popup never arms.
2. **Step forward** — `trig_popup_move -> popup_1.OpenWindow` (movement popup). Trigger +
   `game_sign` already work.
3. **Walk off the porch** — `trig_off_porch.OnEndTouch` → `Jack.WillTalk(1)` +
   `Jack.StartPlayerDialogRemote(256)` + `blueblood_maker.Spawn` + `pc_0` bus inputs. The
   trigger fires today; `Jack` (`npc_VVampire`) and `npc_maker` are inert records, so every NPC
   input drops as `[no input]`.
4. **Jack's dialogue** — `jack_tutorial.dlg` field-5 actions set `G.Tut_Jack = 1`; `OnDialogEnd`
   fires the field-6 payload `DialogPostProcess()`. No dialogue system exists yet (9.1/9.2).
5. **`DialogPostProcess()`** (tutorial.py, on the CPython host) — the
   `Tut_Jack==1 and Tut_Patch==0` branch calls `Find("teleport_fade").Fade()` → **warp #2**;
   the follow-up branch opens `popup_2` (blood pool) and sets `G.Tutorial_Feeding = 1`.

Ordered so each step is independently observable with the existing debug layer — after B1 the
warp is console-fireable, after B2 the script warps the player, after B3 Jack stands there and
takes his inputs, after B4 walking off the porch runs the whole beat unassisted, after B5 the
first popup arms itself:

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
- [ ] **B3 Minimal NPC presence** *(the 8.5 carve-out this beat needs)* — an `npc_*` leaf that
  stands the model at its origin (8.2 glTF path; placeholder body acceptable first), latches
  `WillTalk`/`UseInteresting`, accepts `StartPlayerDialogRemote` (hands off to B4), fires
  `OnDialogBegin`/`OnDialogEnd`; `npc_maker` `Spawn` spawns its `NPCTargetname` entity.
  No AI. *Acceptance:* Jack stands on the porch; `trig_off_porch` outputs resolve instead of
  `[no input]`; `blueblood_maker.Spawn` produces `blueblood`. *Deps:* 8.2.
- [ ] **B4 `.dlg` parser + minimal dialogue runner** *(the 9.1 core, pulled forward; UI is
  interim)* — parse `jack_tutorial.dlg`, eval field-4 (dlgexpr on the installed host), **exec
  field-5** (what writes `G.Tut_Jack`), fire `OnDialogEnd` on exit; a substrate-grade Canvas
  panel (the 4.10 sign-panel pattern) until 8.6/9.2 replace it. *Acceptance:* walking off the
  porch → Jack's dialogue → exit → `G.Tut_Jack=1` → `DialogPostProcess()` → warp #2, unassisted.
  *Deps:* B2, B3.
- [ ] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — `unhidePlus()` resolves, `setPlus()`
  arms `trig_popup_move` and the Plus gates. *Acceptance:* fresh New Game shows `popup_1` on the
  first steps. *Deps:* 9.3.
- [ ] **B6 Feed interaction (post-warp-2 continuation)** — `+use` feed on the blueblood fires
  `OnFedUponBegin`/`OnFedUponEnd`; the maker's `OnFedUponEnd` wires set `G.Tutorial_Blueblood=1`
  and enable `trig_dialog_outside_chopshop`, opening the `Tut_Jack=2` chopshop beat.
  *Acceptance:* feeding on the blueblood enables the chopshop dialogue trigger. *Deps:* B3.

Parallel, non-blocking: 4.7 Source movement (the current pawn walks the beat fine),
`PlayDialogFile` audio (decode is 6.2-done; the call path lands with B4), PL4 batch NPC export
for Jack's real model. B-tasks that are slices of phase tasks (B2/B4/B5) flip here **and** feed
their parent task's status in the same change.

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
- [ ] **4.7 Source movement component** *(was M1.1; parallel-capable)* — port `CGameMovement`
  friction/accel/airaccel/StepMove into a `UCharacterMovementComponent` override
  (`source_movement.md`, Godot `SourceMovement.cs`). **Faithful first** — this is the feel
  layer's known-good baseline and the thing every later tuning delta is measured against, so it
  lands line-by-line from the decompile and stays A/B-able (`remaster-direction.md` axis 3).
  Frame-rate independence, high-polling-rate mouse input and FOV control ride along (identical
  behaviour, modern plumbing); any *behavioural* delta — accel curves, air control, step feel —
  is a separate, owner-approved decision after this runs. *Deps:* none.
- [ ] **4.8 Rotating/linear/elevator family** — `func_rotating` (spin-up/down, hurt-touch),
  `func_movelinear`, `func_elevator` (`GotoFloor`, floor Z table), keyframed movers if the
  tutorial needs them. *Deps:* 4.1.
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
- [ ] **8.3 Dynamic props** — `prop_dynamic` from records with `Skin`/`SetAnimation`/`Break`
  inputs (the ISM path grows per-instance addressability per `entity_visuals.md` R2).
  *Deps:* 8.1, 1.3.
- [ ] **8.4 Physics props** — `prop_physics` ×54 / `phys_hinge` ×12 as Chaos bodies +
  constraints, convex from render mesh. *Deps:* 8.1.
- [ ] **8.5 NPC presence + `scripted_sequence` minimal** — spawn `npc_*`/`npc_maker` at
  origins via glTFRuntime; play-anim-at-marker handler (×51) long before real AI.
  *Pipeline:* **PL4 batch NPC export + `mdl_skel.py` include-model resolution** (shared
  animation banks). *Deps:* 8.2, PL4.
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
- [ ] **8.6 UI foundation — design system + shell** *(replaces the VGUI port)* — the modern UI
  stack every other screen sits on. **Not** a `.res`-driven VGUI renderer: a Slate/UMG component
  set with **vector/SDF type**, resolution-independent layout (real widescreen/ultrawide, DPI
  scaling, no 640×480 canvas, no `//ws-fix` pairs), and a design-token layer (palette, type
  ramp, spacing, panel treatments) carrying VtMB's paper/ink/blood language. Structure and
  content come from the original — screen inventory, panel anatomy, reading order, iconography,
  strings — read off `.res`/`trackerscheme.res` as **intent** (PL8), not executed as layout.
  Ships with it: the main menu + pause menu on the new stack, and the New Game flow calling
  8.6a's `UElysiumMapSubsystem::NewGame` seam (chargen writes the sheet first).
  **Acceptance:** main menu and pause menu are legible and correctly proportioned at 1080p,
  1440p, 4K and 21:9 with no letterboxing or bitmap-font blur; New Game enters `sp_tutorial_1`
  through the 8.6a seam. *Deps:* PL8; 8.6a for the seam.
- [ ] **8.7 Ropes** — `keyframe_rope`/`move_rope` (×107 in tutorial) → Cable Components.
  *Deps:* none.
- [ ] **8.8 Sign / popup panels on the UI foundation** — 4.10's Canvas panel re-drawn on 8.6's
  stack. The **authored layout is honoured as proportion and grouping** (block rects, ordering,
  emphasis) and re-set with vector type on the resolution-independent layout — the `CSignUI`
  1024×768 uniform-scale canvas model (decision log, 2026-07-23) stays the *reference* for what
  the author intended, not the runtime coordinate system. Carries the rest of the sign format:
  the `resource/TrackerScheme.res` font roles mapped onto the type ramp (`ParagraphText` 185 uses
  / `Newsprint` 113 / `Trebuchet` 105 / `Headline` 36 / `Vamp_Handwriting1` 27 / `Tahoma`),
  `Label` justification (`Alignment`) vs `TextBlock` word-wrap, `TextRGBA`/`BackgroundRGBA`,
  `Tiled` backgrounds, `Image` sub-blocks, the **`NewspaperData`** root (30 files) and its
  `Columns`, and the panel keys `CloseOnLeftClick`/`MinShowTime`/`ClientCommand`. The
  per-resolution `Font_640`…`Font_1600` overrides are **dropped** — vector type scales
  continuously. *Deps:* 8.6, 4.10.
- [ ] **8.9 HUD on the UI foundation** — retire the Canvas HUD as the player-facing surface:
  the +use reticle/use-icon (4.4), blood/health and status, the sign/screen-fade states, and a
  subtitle slot, composed on 8.6's stack with the same design tokens. Player pose/mode/FPS is
  dev-only and already lives in the Cog Maps window, separate from the game HUD. Use-icon art
  comes from the PL3 atlas, upscaled under the presentation test. *Deps:* 8.6, 4.4, 4.10.
- [ ] **8.10 Accessibility & options backing** *(`remaster-direction.md` axis 4 — additive only;
  changes what the player can configure and perceive, never what the game does)* — full
  key/button remapping + gamepad navigation across the 8.6 component set; UI text scaling;
  subtitle size/background controls; colourblind-safe status colours + a high-contrast option;
  FOV control; real graphics/audio options screens backing settings the engine already exposes.
  Difficulty and balance are **not** in scope here — those are the logic layer.
  *Deps:* 8.6, 10.6 (input path decided).

**Slice acceptance** *(M5 criterion)*: New Game starts from a real, modern menu that is legible
and correctly proportioned from 1080p to 4K and at 21:9; the HUD and the tutorial's popup signs
draw on the same stack; NPCs stand in the world at their entity origins.

## P9 — Dialogue & persistence *(design: `game_runtime.md`, `rebuild-strategy.md` B7/B9)*

- [ ] **9.1 `.dlg` parser + dlgexpr** — 13-field CRLF Latin-1 parser + branch machine; the
  dlgexpr grammar on the 5.2 evaluator; **error-to-false** on the 89 malformed retail
  snippets (RE3 confirms; implement as documented regardless). *Deps:* 5.2.
- [ ] **9.2 Conversation UI + audio-by-path** — dialogue screen on the 8.6 UI foundation, line
  audio via 6.2. Content is **reproduced verbatim** (lines, conditions, branch structure,
  ordering); presentation modernizes — vector type, reflowing line lists, speaker/emotion cues,
  the 8.10 subtitle path. *Deps:* 9.1, 6.2, 8.6.
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
- [ ] **9.3 Level-script execution** — the remainder of the 5.5 decision. **B2 landed the core**:
  `Entity.__getattr__`/`__setattr__` over the P1 class-chain tables, the `Player` object, all 11
  module globals as real C bindings, `G`'s mapping protocol, and the `__main__` namespace
  (`LoadLevelScript` merges the level module's public names into `__main__` and payloads evaluate
  there, matching the `__main__.%s` dispatch — so `ScheduleTask`'s deferred strings already resolve
  against a live `__main__`). What is left:
  - **Fill the remaining stubs demand-driven** off the Scripting window's native-call counters —
    the 22 unbacked Character methods (inventory, disposition, camera, barter, feats) and the four
    spawn/query globals (`CreateEntityNoSpawn`, `CallEntitySpawn`, `OneOfSet`, `SquadSeesPlayer`),
    plus `Entity`'s four writers (`SetOrigin`/`SetAngles`/`SetModel`/`SetName`), which need a body
    to move, a name index to re-key, and a model to swap.
  - **Field-table audit** *(surfaced by B2)* — the Entity attribute namespace is only as complete
    as the class field tables, and they are **input-complete but not field-complete**: a leaf class
    that reads a keyvalue straight at `Spawn()` without a `Field(...)` registration is invisible to
    a script. `env_fade` was the proven case (`duration`/`holdtime` — its four-record datamap is
    fully RE'd, so B2 registered them); walk the other registered classes for the same shape. Where
    the retail datamap has not been decompiled, RE it rather than guessing which fields are keyable.
  - **`vamputil` for real** — the bootstrap's stub module lacks `RandomLine`, so `santamonica` (and
    the other maps whose scripts import it) still fails to import; logged, non-fatal, map load
    continues. The real file cannot import until `ccmd`/`cvar` are bound, i.e. **9.3b**, which is
    also why `IsClan`/`IsIdling` are still bootstrap stubs (now returning 0, so a clan gate falls
    to its `else` instead of always taking the first branch).
  *Deps:* 9.3a, B2.
- [ ] **9.3b Console bridge — `ccmd` + the `cfg` alias table** *(the fifth scripting surface)* —
  the scripts drive the engine console, and the console drives the scripts back. `__main__.ccmd`
  is a console-command object whose *attribute assignment* executes a command:
  `c = __main__.ccmd; c.patchtype = ""` runs the console alias `patchtype`. Aliases and settings
  come from `cfg/user.cfg` (`alias patchtype "setPlus()"`, `vchar_skip_intro`, `torchlight_*`,
  the `run`/`walk`/`automove` movement aliases, …), and a command the console does not recognise
  falls through to Python — so the round trip is **Python → alias → Python**.
  **This is how the Unofficial Patch selects Basic vs Plus**: the installer writes one of two
  `user.cfg` files differing only in whether `patchtype` expands to `setBasic()` or `setPlus()`,
  and one shared script tree asks the console which install it is running under. Nothing in any
  `.py`/`.ents`/`.dlg`/`.bsp` names `setPlus`/`setBasic` — the only reference in the whole install
  is that one `.cfg` line, which is why it reads as dead code until you search the config tree.
  **Load-bearing, and it starts on map load:** `logic_auto.OnMapLoad -> unhidePlus()` is wired on
  **107 of 108 maps** (`sp_tutorial_1` included), and `setPlus()` is what arms `trig_popup_move`
  (the 4.10 movement-popup gate, `StartDisabled 1`) plus the haven/beachhouse/condom
  ScriptHide/Unhide sets and the `plus_handle*` door locks. Today `unhidePlus()` evaluates,
  hits the unbound `ccmd`, and dies error-to-false — so those entities stay dormant and
  `G.Patch_Plus` stays 0, silently selecting Basic-mode behaviour everywhere.
  Scope: bind `ccmd` (attribute-set = execute), a cvar/alias store seeded from a **PL5d** copy of
  `cfg/*.cfg`, and the console→Python fallthrough. *Deps:* 9.3; needed by 4.10's trigger arming.
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
| PL5 | Copy sound schemes (a) [x] + `vdata/system/*.txt` (b) + `vdata/Signs/*.txt` ×278 + the 57 referenced background materials (`hud/signs/*`, `interface/Pop_Ups/*`) → `out/signs/` — `UE_extract_signs.py` (c) [x] | 6.3, 9.4, 4.10 |
| PL5d | Copy `cfg/*.cfg` (the alias/cvar tables — `user.cfg` carries the Basic/Plus `patchtype` alias) → `out/cfg/` | 9.3b |
| PL6 | Texlight merge in exporter | 3.4 |
| PL7 | Sidecar space fixes surfaced by the audit — **none (0.4: all sidecars already Unreal cm)** | 0.4 [x] |
| PL8 | UI source inventory for the re-skin — extend `menu_extract.py` to mirror `.res` layouts, `trackerscheme.res`, UI bitmaps and strings into `out/ui/` as **design intent + source art** (screen inventory, panel anatomy, palette, iconography). The `.fnt` bitmap atlases are extracted for reference/metrics only — they are not the runtime type. | 8.6 |

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
| RE9 | Screen-fade flag semantics — the client owns the curve, not `env_fade`. `CEnvFade::InputFade` `0x10100F90` (spawnflags → fade flags), `CViewEffects::Fade` `client.dll 0x10196FE0` (builds `FadeEnd`/`FadeReset`/speed), `CViewEffects::FadeCalculate` `client.dll 0x10197190` (the per-frame alpha + the `0x20` auto-reverse). **`SF_FADE_STAYOUT` uncovers again; the permanent bit is a different one env_fade never sets. No `OnEndFade`/`ReverseFade` exists.** | B1 | [x] |

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
- ~~**Asset-enhancement track**~~ — **no longer an option; it is in scope** as remaster axis 2
  (`remaster-direction.md`). Delight → super-resolve → PBR-synthesize
  (normal/roughness/AO/envmask), as an `elysium.EnhancedTextures` A/B toggle on top of the
  faithful world set (the toggle stays — it is the regression guard, and the faithful set stays
  the reference forever). Tier 0 (delight + upscale) fixes real deficits for a dynamically-relit
  engine; Tier 1 (PBR synthesis) is style-anchored enhancement; Tier 2 is out of bounds.
  Scaffolding exists (`upscale_bench.py`, `sky_upscale.py`, `retex_dds.py`); `M_VtMB_World`'s
  normal/envmask slots are the runtime hooks. **Budget-gated** by the 3060/12 GB floor (2×
  default, 4× hero only; BCn+mips mandatory). Full plan + adjudication test:
  `asset-enhancement.md`. **Sequencing is unchanged** — scheduled at P10, after the vertical
  slice plays; it is polish on a shipped look, not a blocker.

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| MegaLights silently disengages (VSM fallback) | frame collapses | 0.2 check lives in the profiling routine forever; 3.1 pins it |
| ~~Cog fails to build on 5.8~~ **(resolved)** | debug layer slips | **0.5 done: Cog builds + runs clean on 5.8** (VS 14.50, no ImPlot patch needed); fallback (VesCodes/ImGui + hand-rolled windows) unused |
| ~~Level scripts exceed the mini-interpreter subset~~ **(resolved)** | scripting rework | **5.5 + 9.3a done: CPython 2.7.18 is embedded and is the map-load default host**, importing the map's own `worldspawn.levelscript` — retail's scripts run as written |
| Embedded VM fails to start in a packaged build | whole scripting surface silently error-to-false | 9.3a: `MakePreferredScriptHost` checks the VM actually started and falls back to the expr host, logging a warning, rather than leaving every eval Void |
| Chaos kinematic movers push/block poorly | doors feel wrong | 4.1 prototypes one door first |
| Floor perf unproven (no 3060 on hand) | late surprise | look-gates until 10.3; lump-8 bake parked as contingency; Lumen Lite noted as a cheaper option (see Options) |
| Tutorial-only calibration bias | rework on other maps | 0.3 second map early; all calibration provisional until 10.1 |
| Sidecar space drift (Godot-era leftovers) | subtle geometry/logic bugs | 0.4 audit before any new consumer |
| Save determinism erodes | broken saves late | standing rule since P1: no engine timers, own serializable structs |
| Legal posture | project-ending | bring-your-own-game holds; nothing game-sourced committed — standing constraint on every task |
| Asset enhancement drifts off-style | silent look regression | `asset-enhancement.md` adjudication test + `elysium.EnhancedTextures` A/B toggle keeps the faithful set as reference; per-family review, not per-texture |
| Modern UI loses VtMB's voice (reads generic/AAA) | the remaster stops feeling like VtMB | 8.6 keeps the original's structure, palette and iconography and re-skins only the craft; presentation test applied per screen; `m0_menu_build.md` + extracted `.res`/scheme (PL8) are the intent reference every screen is checked against |
| "Polish" leaks into the logic layer | silent divergence from retail behaviour, unfindable later | `remaster-direction.md`'s governing rule: RE first, owner's call, dated decision-log entry recording faithful *and* chosen behaviour; default is reproduce, and layer assignment happens before the work, not after |
| No classic-UI mode to A/B against | a UI regression has no reference | the original's structure is captured as data (PL8) and in `m0_menu_build.md`, so screens are checked against intent rather than pixels; the *world* keeps its faithful A/B path unchanged |

## Decision log (append-only)

- **2026-07-24** — **Brush touch requires a pawn toucher, and waits until the pawn is seated.**
  Bug fix. `elysium.newgame` (or any travel) *from an already-loaded map* warped the player off
  the tutorial porch into the downtown alley and looked like the spawn "moving to the next spawn".
  Cause: two unrelated things collided. (1) Every brush body on a map is a component of the one
  `AElysiumMapActor`, so when the new map builds its bodies, Unreal fires begin/end overlap for
  every trigger∩trigger and trigger∩solid pair at once — and `UElysiumBrushComponent::RouteTouch`
  forwarded *all* of them as touches, since it never checked who was touching. (2) The pawn carries
  over from the previous map and is placed at the new map's spawn only on the first tick *after*
  the entity world exists, so for a moment it stands wherever the old map left it. Together, the
  phantom geometry-overlap touches ran real scripted beats — `trig_feed_fix.OnStartTouch ->
  fix_fade.Fade -> teleport_player.Teleport` — and teleported the player. Fix: `RouteTouch` now
  routes only when the overlapping actor `IsA<APawn>` (VtMB never treats geometry∩geometry as a
  touch — only movers touch), and only once `AElysiumMapActor::IsPlayerSeated()` is true (the pawn
  has been moved to this map's info_player_start/landmark, or this map requested no placement).
  Verified: `elysium.newgame` from a loaded `sp_tutorial_1` now produces zero `teleport_player`
  deliveries, zero `fix_fade`/`trig_feed_fix` fires, and exactly one legitimate touch — the player
  genuinely standing in the (inert, StartDisabled-on-arrival) `trig_theater_to_tutorial` changelevel
  volume at the porch, which fires nothing. Do not remove either guard. When NPC pawns (8.5) and
  physics props (8.4) can trip triggers, widen the `APawn` test rather than dropping it.
- **2026-07-24** — **Agentic QA is Layer 3 of the debug architecture, not a new track (P2.7–2.9).**
  `debug-tooling.md` already framed the `elysium.*` console verbs as "the thin scriptable layer for
  `-ExecCmds` automation and headless runs" — the third consumer beside the human (Cog) and the
  script. An AI agent is that third consumer made first-class: the same runtime state, reached
  through structured MCP tools instead of parsed console text. Owner call — chosen shape and why:
  (1) **Direct `AddTool()` registration, not the Toolset-Registry adapter** — the adapter is
  editor-only, but this project's whole loop is `-game`/cooked with no editor content loop, so tools
  register through `IModelContextProtocolModule::AddTool()`, which serves in every target. (2)
  **Tools live inside `ElysiumUE`, not a separate module** — `FElysiumEntityWorld` and the substrate
  carry no `ELYSIUMUE_API` exports (plain C++, Unreal supplies bodies only), so a sibling module
  couldn't link them; the MCP layer follows the vendored-Cog precedent (non-Shipping/editor private
  dep). (3) **On-by-default in dev, loopback + no-auth** — the server auto-starts wherever the plugin
  is present (editor target only, so never in Shipping/Test), because a QA surface you must remember
  to enable is one you forget; `-NoElysiumMcp` opts out. (4) **Two-tier tests** — a `-nullrhi` content-free suite that is the real
  regression net (substrate paths), plus a content-gated suite that self-skips on an empty
  `tools/out` so a fresh checkout stays green; the LLM is never the oracle — it *drives* Unreal's own
  automation runner. (5) **Screenshots share the profiler's vantages** (`ElysiumVantages.h`) so a
  look regression and a cost regression are the same frame; baselines are game-derived → gitignored.
  Industry survey that informed this (Epic's in-box `ModelContextProtocol` plugin, the community
  `unreal-mcp` servers, Gauntlet vs. functional-test guidance, the fire→screenshot→assert playtest
  loop): the editor-centric MCP toolsets buy little here, so only `AutomationTestToolset` +
  `LiveCodingToolset` are enabled beside ours; Gauntlet is deferred (single platform, no net
  sessions). Full design: `debug-tooling.md` Layer 3.
- **2026-07-24** — **Field-6 payloads evaluate in `__main__`, not the level module (B2 revises
  9.3a).** RE first. `G`'s type object (`PyDataManager`, `0x1058fa08`) carries a `tp_as_mapping`
  (`0x1058f9f8`) whose `mp_subscript` (`0x1019b4a0`) literally **tail-jumps into `tp_getattr`**
  after `PyString_AsString`, and whose `mp_ass_subscript` (`0x1019b720`) calls `tp_setattr` the
  same way — so `G[k]` **is** `G.k`, default-on-miss 0 and all. Reproduced, because
  `DialogPostProcess()` calls `saveState()` (`for k in G.keys(): G_tut[k] = G[k]`) on its first
  line and would otherwise never reach the beat branch. Two knowing divergences on that surface:
  a **non-string key on assignment raises `TypeError`** where retail tail-calls `PyDict_SetItem`
  with the manager object in the dict slot (a latent bug no shipped script reaches — every G key
  is a string), and our proxy keeps a **`has_key` method** that retail's 2-entry table does not
  have (retail resolves `G.has_key` to a flag read of 0; no script calls it, and the method
  predates this task on the expr host's G surface too).
  The namespace change is the second half. 9.3a evaluated payloads in the **level module's** dict;
  VtMB wraps every payload as `__main__.%s` (`0x1055e370`), which resolves the leading name as an
  attribute of `__main__` — so the level script's own `def`s must be *in* `__main__`, and so must
  the engine globals. Measured over the 10 exported maps' 363 field-6 payloads, the module-dict
  choice breaks 2 of them: `hw_609_1` fires a bare `FindPlayer().ClearActiveDisciplines()` and
  `hollywood.py`, unlike `tutorial.py`, never aliases `FindPlayer` at module level. `LoadLevelScript`
  now merges the imported module's public top-level names into `__main__` and everything evaluates
  there. A function keeps its defining module's globals, so `DialogPostProcess` still reads its own
  `G_tut`/`Find`/`statemap`; only the entry-point lookup moved. `__main__.Level = __name__` is the
  cross-check that retail imports-then-merges rather than exec'ing into `__main__` — the assignment
  only carries information if `__name__` is the script's own module name.
  Third, smaller call: the bootstrap's `IsClan`/`IsIdling` stubs (vamputil's, not engine API — they
  exist only so `tutorial.py`'s import-time guard resolves) now return **0** instead of a truthy
  stub object. A truthy predicate made every clan gate take its *first* branch, i.e. silently play
  as Brujah; 0 falls to the `else`, which is the honest "no clan matched". Real behaviour arrives
  with the real `vamputil` in 9.3b/B5.
- **2026-07-23** — **B1 landed, and `SF_FADE_STAYOUT` is the flag that brings the screen *back*.**
  RE first, over `vampire.dll` + `client.dll`. `CEnvFade::InputFade` (`0x10100F90`) maps
  spawnflags to the client's fade flags as `SF_FADE_IN`(0x1) → **0**, else `0x2` (`FFADE_OUT`)
  `| 0x20` when `SF_FADE_STAYOUT`(0x8); `SF_FADE_MODULATE`(0x2) → `|0x4`; `SF_FADE_ONLYONE`(0x4)
  sends to the activator alone. It then fires `OnBeginFade` at delay 0 and returns — no think, no
  second output. `CViewEffects::FadeCalculate` (`client.dll` `0x10197190`) is where the meaning of
  `0x20` lives: when a fade passes both `FadeEnd` and `FadeReset` it is normally **dropped**, but
  with `0x20` it instead flips to a fade-in (`flags &= ~0x22 | 0x1`), negates its speed and takes
  one more `duration` to uncover. The client's genuinely-permanent bit is `0x8` (which re-pushes
  `FadeReset` to `curtime + 0.1` every frame) and `env_fade` never sets it; `0x40` disconnects to
  the menu when the fade ends. **4.5 had this backwards** — it held `STAYOUT` covered forever and
  ramped the plain fade back — which would have left warp #2 on a black screen through Jack's
  dialogue. Corrected: cover over `duration`, hold `holdtime`, then uncover over `duration` only
  when `SF_FADE_STAYOUT`, else expire. That reproduces `teleport_fade`'s authored timing exactly
  (`duration 1`, `holdtime 2`, clear at t=4 — precisely when its `Jack.StartPlayerDialogRemote`
  wire fires). **`OnEndFade` and `ReverseFade` are not VtMB** (no such string in `vampire.dll`);
  the `ReverseFade` input is removed rather than kept as a debug affordance, so the Inspector's
  input list stays a faithful mirror of the datamap. `SF_FADE_IN`'s flag-0 path is reproduced
  as-is including its quirk — `FadeCalculate`'s `flags & 0x3` test fails, so the colour sits flat
  at full alpha for `holdtime + duration` and then snaps clear, with no ramp either way; no
  tutorial `env_fade` sets it. The fade stays **one slot** rather than VtMB's fade list: the list
  sums colours and maxes alphas, which is indistinguishable from one slot while every fade on a
  map is the same colour, and all seven on the tutorial are black.
- **2026-07-23** — **Inspection is click-to-select, not crosshair-follow (P2.6).** Owner call. The
  live crosshair inspector is removed: the Entity Inspector no longer traces the camera ray every
  frame. Instead, while the Cog menu owns the mouse, LMB over the world picks whatever is under the
  cursor and RMB clears — no pause (the world keeps running under the cursor; Time Scale stops it
  when wanted). The pick lives in `RenderTick`, which Cog runs for every window regardless of
  visibility, so it works with the inspector closed. Three things this settled:
  **(1) Physics cannot answer the pick, so two of the three sources are CPU ray-casts.** Under the
  default `elysium.BrushCollision 1` the world *render* mesh is built with collision off — the
  `.hulls` convex set is the collider, and it carries no material and no face, so a trace could
  never name the surface it hit (the old crosshair readout only worked at all under
  `elysium.BrushCollision 0`). Separately, a solid prop's cooked collision is a **single convex hull
  of the whole model**, and non-solid props have none. So `ElysiumPick::Trace` physics-traces only
  the entity bodies (`LineTraceMulti` on `ECC_Visibility`, which returns trigger overlaps too) and
  CPU-casts the prop instances and the world/sky sections against their real triangles. The map
  actor retains the geometry for it (`FPropPickSoup` per unique model, ~3 MB on the tutorial; a
  section → OBJ-group-key table), all `#if !UE_BUILD_SHIPPING`.
  **(2) A world pick highlights the BSP face, and that falls out of the exporter for free.**
  `UE_bsp_to_scene.py`'s `emit()` appends a fresh vertex per face corner (no dedup across faces) and
  `BuildMeshFromObj` remaps sections keyed on the *global* index, so the triangles of one face share
  local indices and adjacent faces share none. Flooding across shared edges therefore stops exactly
  at the face boundary — no position weld, no normal threshold, no bleeding around a corner. The
  coplanarity guard only bites on displacement grids, where one face is a whole curved patch. The
  flood costs an edge map over the section, so it runs on click; hover previews the single triangle.
  **(3) The highlight is imgui, not scene geometry.** Considered and rejected: custom-depth stencil
  outline (best silhouette, but cannot outline an invisible trigger volume and lights every instance
  of a prop model at once), a retained x-ray box (bounds-only, coarse on a world section), and a
  material tint/checkerboard on `M_VtMB_World` (cheapest code, worst granularity — the world OBJ is
  grouped per material, so it would highlight every face sharing that texture map-wide). Projected
  fill + outline + label is the only one exact for all three target kinds, needs no assets, and
  keeps drawing when the world is time-scaled to a stop, since Cog's render tick is not the game
  tick. Known bound: an entity's highlight is one box per def hull (the hulls are vertex sets with
  no faces) — exact for the axis-aligned box brushes nearly every trigger and door is made of.
  **(4) A World Viz gizmo marker outranks the geometry it is drawn over.** Owner call. The marker is
  a deliberate "select me" handle, and it is the *only* clickable representation the ~1,000 bodiless
  entities on a map have (on `sp_tutorial_1`, 394 of 1,868 records are `light`/`light_spot` alone —
  they are in the `.ents` lump as inert records, so they carry gizmos). Three sub-calls: **drawn is
  the whole test** — `Visible` depth-tests the cubes so one behind geometry does not pick, `All` is
  x-ray so any does, `Off` skips the source — which needs the nearest *rendered* hit tracked apart
  from the pick winner, since brush bodies render nothing and must not occlude a marker behind them;
  **the hit test is the exact 28 cm cube**, not a screen-space tolerance, so dense clusters stay
  separable at the cost of distant markers being small targets; and **clicking a cluster again
  cycles** to the next marker behind the current one, wrapping. With gizmos on, the bodiless-entity
  perpendicular fallback (2 m, inherited from the `ent_*` picker) is suppressed — it is far looser
  than the cube and would undo the precision — and stays armed only with gizmos off. The marker's
  anchor and size are consolidated into one definition (`ElysiumGizmoColor.h`) shared by the ISM
  layer, the label/beam overlays and the pick; they had been duplicated in two files, and the
  what-you-see-is-what-you-click rule only holds if they cannot drift.
  The `ent_fire` crosshair picker and the HUD's `+use` reticle are unaffected; they are separate
  systems.

- **2026-07-23** — **Direction: VtMB *remastered*, not a pixel-perfect recreation.** Owner call.
  Tone, ambience, feel and game logic are kept; craft is raised with tools 2004 did not have.
  Written up as `docs/remaster-direction.md` (the charter); `rebuild-strategy.md` principle 7
  and this doc's north star restated to match. Four decisions carry the weight:
  **(1) Three change layers, three rules.** *Presentation* (UI, type, HUD, textures, post) modernizes
  freely under the art-direction test, no approval gate. *Feel* (movement, camera, combat) is
  built faithful first, kept A/B-able, and polished one delta at a time by explicit call.
  *Logic and content* (entity semantics, I/O, scripts, dialogue, stats, saves) is reproduced.
  Layer assignment happens before the work, not after — the boundary is *game state*, not
  visibility.
  **(2) The governing rule: we only change what we understand, and only on an explicit call.**
  RE comes first — a behavioural divergence may only be *proposed* once the faithful behaviour
  is known and recorded, and it lands only with a dated owner decision in this log carrying both
  the faithful and the chosen behaviour. Default resolves to reproduce. This is why the RE
  backlog does not shrink under a remaster direction: you cannot judge what to keep until you
  know what is there.
  **(3) The UI drops its faithful path; the world keeps its.** The pixel-faithful VGUI port is
  **not built** — there is no classic UI mode. VtMB's screen structure (inventory, panel
  anatomy, reading order, palette, iconography, strings) is kept and **re-skinned**: vector/SDF
  type replacing the `.fnt` bitmap atlases, resolution-independent layout replacing the 640×480
  proportional canvas and the patch's `//ws-fix` pairs, restrained motion, gamepad-navigable
  components. The bitmap fonts are the loudest defect in the game on a modern display, and
  illegibility was hardware, not art direction. `m0_menu_build.md` and the `.res`/scheme/`.fnt`
  decoders become **reference and extraction machinery** (PL8), not a runtime layout stack. The
  world is untouched by this: geometry/placement/lighting stay anchored to VtMB's data and the
  lightmap calibration, with `elysium.EnhancedTextures` as the A/B toggle on top.
  **(4) Asset enhancement leaves the Options list and becomes scope** (remaster axis 2), with
  its tiers, its per-family curation and its A/B toggle unchanged, and its **P10 sequencing
  unchanged** — it is polish on a shipped look, not a blocker.
  Task deltas: P8 renamed *Characters & UI*; **8.6** recast from "VGUI menu port" to the UI
  foundation (design system + shell + menus + New Game); **8.8** recast from "sign window — VGUI
  fidelity" to sign/popup panels on that foundation (the `CSignUI` 1024×768 canvas model stays
  the *intent* reference, not the runtime coordinate system; the `Font_640`…`Font_1600`
  per-resolution overrides are dropped — vector type scales continuously); **8.9** (HUD on the
  foundation) and **8.10** (accessibility & options backing) added; **4.7** gains the
  faithful-first feel-layer note; **9.2** dialogue content verbatim, presentation modern;
  **PL8** added for the UI source inventory. Three risks registered: UI losing VtMB's voice,
  polish leaking into the logic layer, and having no classic mode to A/B against.

- **2026-07-23** — **9.3a: the level scripts are wired; CPython is the default host.** The 5.5 PoC
  proved the embed offline but nothing connected it to a map — `LoadLevelScript` was reachable only
  from `elysium.py.load` and the Cog button, and the map-load host was still ElysiumExpr. Three
  decisions closed that:
  **(1) The import happens off the parsed `.ents`, before the spawn pass** — not off the live entity
  world after it. VtMB runs the level script's top-level code first and spawns entities into that
  namespace; doing it after `FElysiumEntityWorld::Load` would leave a spawn-time field-6 payload
  evaluating against a module that does not exist yet. Hence `FElysiumEntityDefs::LevelScriptModule()`
  rather than a worldspawn walk over the built world.
  **(2) Importing is a host capability, and the module name is remembered.**
  `IElysiumScriptHost::LoadLevelScript` defaults to "this host cannot import", which the expr and null
  hosts inherit truthfully; `SetScriptHost` re-imports the remembered module into whatever host is
  installed next. Without that, `elysium.script.cpython 1` mid-map would install a CPython host with a
  bare `__main__` and every level constant would read NameError — an A/B that lies.
  **(3) A CPython VM that fails to start falls back to the expr host.** Its evals would all be Void,
  which is exactly what error-to-false looks like (RE3) — so a missing `python27.dll` in a packaged
  build would degrade the entire scripting surface **silently and plausibly**. `MakePreferredScriptHost`
  checks `IsUsable()` and warns.
  Routing `EvalScript` through the installed host came with it: the console verbs and the Cog eval box
  hard-wired ElysiumExpr, so with CPython live they would report NameError for names the game itself
  resolves — a debug surface contradicting the runtime. `IElysiumScriptHost::Eval` gains an optional
  `OutError` (a Void return cannot distinguish "evaluated to None" from "raised"), and the eval/exec
  split collapses to one path: the CPython host already tries `Py_eval_input` then `Py_file_input`, and
  `ElysiumExpr::Exec` returns its last statement's value.
  Verified in the built game, not offline: `sp_tutorial_1` imports `tutorial` at map load into host
  `cpython`; `elysium.eval cCelerity` = **8**; `G.Tutorial_Discflags |= cCelerity` flips `G` to **8**;
  `elysium.script.cpython 0` returns NameError; travel to `sm_pawnshop_1` re-imports per map.
  That last one surfaced the first real gap: `santamonica` raises `ImportError: cannot import name
  RandomLine` — the bootstrap's `vamputil` stub is thinner than the retail module. Logged, non-fatal,
  map load continues. It is 9.3 work, and it is evidence for doing `Entity.__getattr__` and a real
  `vamputil` before hand-porting the 24 Character methods.

- **2026-07-23** — **There is a fifth scripting surface: the console (9.3b + PL5d).**
  `python_bridge.md` lists four Python surfaces (level scripts, `.dlg`, entity field-6,
  `logic_pythoncheck`); the console is a fifth path and it is **bidirectional**. Scripts execute
  console commands by attribute-assigning on `__main__.ccmd` (`c.patchtype = ""`), and a command
  the console cannot resolve falls through to Python. The Unofficial Patch uses that round trip as
  its whole Basic/Plus switch — the install variant lives in `cfg/user.cfg`
  (`alias patchtype "setPlus()"`), not in any script or map, so `setPlus`/`setBasic` appear
  uncalled to any search of `.py`/`.ents`/`.dlg`/`.bsp`. Found while asking why `trig_popup_move`
  never fires: `logic_auto.OnMapLoad -> unhidePlus()` is wired on 107 of 108 maps and is the
  ignition for the whole chain, so with `ccmd` unbound every Plus-mode entity tweak silently
  no-ops and `G.Patch_Plus` stays 0. Tracked as **9.3b**, with the cfg copy as **PL5d**.
- **2026-07-23** — **The sign panel draws on a 1024×768 canvas, uniformly scaled by height (4.10).**
  The layout is **client.dll's `CSignUI`**, not `vampire.dll` — the game DLL owns only the entity and
  `LoadSignData`, which is why the earlier `sign.txt` dump had the file resolution but no paint code.
  `FUN_10061520` parses; `FUN_10061830` (background) and `FUN_10060560` (text block) map the authored
  rect through the doubles at `0x10227ec8` = 1/1024 and `0x10227eb8` = 1/768 and call SetPos/SetSize
  (`FUN_101a9950`). Three findings the data alone could not give:
  (1) **The scale is uniform, driven by height** — `FUN_100cd100`, which the width math divides by
  1024, is a 4:3-proportional width (`ScreenH·4/3`), not the backbuffer width, so `Wide·A/1024`
  collapses to `Wide·ScreenH/768`. The canvas keeps its aspect and letterboxes horizontally rather
  than stretching. Measured against the retail game on 16:9: panel width is **1.513×** the screen
  width, where a stretch model predicts 2.0× and this one predicts 1.50×.
  (2) **Text blocks are children of the panel**, so `XPos`/`YPos` are offsets inside the
  `BackgroundImage` rect, not screen coordinates — `FUN_10061830` is a `CSignUI` method setting the
  panel's own bounds. The shipped data corroborates: the patch moved `tutorial_popup_moving1`'s body
  text 324 → 836 in the same edit that widened the background 1024 → 2048, and a centred 2048-wide
  panel starts at virtual −512, so −512+836 lands on retail's original pixel.
  (3) When `XPos + YPos == 0` the background takes a **centring branch**, which is why
  `interface/Pop_Ups/general` at `2048×1024` deliberately overscans and bleeds off every edge.
  Three keys the entity-side survey had missed also turned up: **`HideHUD`**, and **`ClientCommand`**
  inside a **`Rules`** block (with `CloseOnLeftClick`, whose retail default is the panel's constructed
  value = **true**, and `MinShowTime`). Fonts pick by *exact* screen-width match on
  640/800/1024/1280/1600, else the plain `Font`, else `"Default"`.
- **2026-07-23** — **Sign windows tracked, split substrate/UI (4.10 + 8.8 + PL5c).** `game_sign` /
  `prop_sign` had no task: the only mention anywhere was `entity_visuals.md` R5, which called them
  "textured quads/decals… small, cosmetic; last" — wrong, and the reason they were never scheduled.
  The decompile settles what they are (`CGameSign::LoadSignData` `FUN_10212da0` /
  `CPropSign::LoadSignData` `FUN_10212200`): a **full-screen VGUI window** whose `definition_file`
  is a `SignData` KeyValues panel, opened by `OpenWindow` or `+use`, with a first-true `Sign
  { dependency filename }` redirect evaluated as Python (`Py_eval_input`, error-to-false) — i.e. a
  UI + scripting subsystem, not geometry. They are also **load-bearing for the tutorial**: 71 of the
  73 `game_sign` in the game sit on `sp_tutorial_1` as the `popup_*` help windows, and `tutorial.py`'s
  beat machine opens/rewrites them directly, so "tutorial completable as retail" (P9) can't be met
  without them. **Split:** the entity classes + a Canvas panel land in P4 as **4.10** (so the
  substrate is complete and the tutorial's 51 `OpenWindow` wires stop dropping, before P5's scripts
  start firing them), and the pixel-faithful panel waits for the VGUI stack the menu port brings, as
  **8.8**. The definitions + background materials export as **PL5c**. R5 in `entity_visuals.md` is
  corrected to point here.
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
