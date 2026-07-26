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
  `scripted_sequence` handler and animation-bank retargeting land in 8.5 itself). *Deps:* 8.2, PL4 [x].

- [x] **B4 `.dlg` parser + dialogue runner** *(9.1's core made playable; UI is interim)* — landed. The
  reusable core is **9.1** (`ElysiumDlg.{h,cpp}`, its own archive entry below); B4 is the in-game wiring.
  **NPC leaf:** `FElysiumNpc::InputStartDialog` now, after `OnDialogBegin`, reads the NPC's `dialogname`
  keyfield (`dlg/Main Characters/jack_tutorial.dlg`), loads the `FElysiumDlgFile`, builds a
  `FElysiumDlgConversation` whose condition/action callbacks normalise dlgexpr (`ElysiumDlgExpr`) and route
  through `FElysiumEntityWorld::EvalCondition` (the installed CPython/expr host — so field-5 writes land in
  the same `G` the level script reads, and inherit error-to-false), `Start()`s it, and hands it to the world;
  a missing/unloadable file falls back to the B3 manual-`EndDialog` seam. `InputEndDialog` now increments
  `times_talked`. **World seam:** an open-dialogue slot mirroring the sign slot — `OpenDialog`,
  `GetOpenDialog`/`GetOpenDialogOwner`, `PlayerDialogChoose`/`PlayerDialogAdvance`, `CloseDialog`, and
  `EndDialogSession`, which routes `EndDialog` to the owner via `!self` so its `OnDialogEnd` (→
  `DialogPostProcess()`) fires through the real chokepoint on close (a teardown/replacement closes silently).
  **UI:** a purpose-built native-Slate **visual-novel box** `SElysiumDialogueBox` (`ElysiumDialogueWidget.{h,cpp}`)
  — a translucent slab + numbered `SButton` rows, engine fonts, geometry-and-transparency only (no art, no
  sign machinery, no VGUI). `AElysiumHUD` ticks it: it polls the world's open conversation, (re)builds the box
  on a turn change, tears it down on close, and holds the player in `FInputModeUIOnly` while it is up (the VN
  freezes the world); a pick routes back through `PlayerDialogChoose`. Number keys 1–9 and clicks both select.
  **Debug:** `elysium.dlg` / `elysium.dlg.choose <n>` / `elysium.dlg.advance` are the scriptable echo
  (`ElysiumDlgConsole.cpp`, non-Shipping), so a headless/MCP agent can walk a beat with no UI. **One bug found
  + fixed during the in-game smoke:** `SButton` stores its `FButtonStyle` by pointer, so a `Construct`-local
  style dangled and crashed on the first paint — the style is now a widget member (`ChoiceRowStyle`). *Acceptance
  met (built game):* firing `Jack.StartPlayerDialogRemote 256` opens `jack_tutorial.dlg` (1116 rows), the box
  renders line 11 with the live-`IsClan`-gated "Who are you?" choice, walking 11→21→id-22 runs the field-5
  action to `eval G.Tut_Jack = Int(1)`, and closing a line fires `OnDialogEnd → DialogPostProcess() →` travel.
  *Deps:* B2, B3.
- [x] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — landed. The console bridge is the fifth
  scripting surface; its full as-built is the **9.3b** record in the P5 section below. In B-track terms:
  binding `vampire.ccmd`/`cvar` lets the real `vamputil.py` import, so `unhidePlus()`/`setPlus()` resolve,
  and the VM's file layer now resolves VtMB's `getcwd()+moddir` paths into `out/` so `setPlus`'s
  `FixKeyBindings` reads `out/cfg/config.cfg` and reaches its Tutorial branch. *Verified in the built game
  (fresh New Game, MCP I/O history):* map load fires `logic_auto → unhidePlus() → ScheduleTask(1.0,
  'c.patchtype="")` → the console runs alias `patchtype` → `setPlus()` (real vamputil) →
  `trig_popup_move.Enable()` + `events_player_plus.EnableOutputs()`, all unassisted; `cubemaps_builder`'s
  field-6 `ccmd.wc_create` reads back `""` (a no-op, since only attribute-*set* executes). The one traceback
  is the documented post-`Enable` gap — `setPlus`'s haven-PC `open("./vdata/hackterminals/haven_pc.txt")`
  (a `moddir`-relative-only path the redirect does not cover) IOErrors and error-to-falses, inconsequential
  on `sp_tutorial_1` because it follows the `Enable`. *Deps:* 9.3.

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

- [x] **0.9 The uasset-bake architecture — decide, document, land** — housekeeping, not graphics:
  a decision entry, a charter-doc correction and a branch merge, with zero rendering work. The two
  stacked spikes (`lumen-coverage-spike.md`, `uasset-bake-spike.md`) had answered a question that
  blocked P3 — a runtime-built mesh can never hold what the editor build produces (DDC-fitted Lumen
  surface-cache cards, Nanite, distance fields, BC7) — and the architecture that followed had been
  running in practice, de facto, since 2026-07-25: the perf retune (4060 floor, stock Epic
  scalability), `.phy` physics collision, prop skins, the decal bake and the whole SKY rework are
  all built on it. This task closed the gap between the running code and the record.

  **The decision** (`decisions.md` 2026-07-26, cont. 6): the world's *look* is baked offline into
  `.uasset`s and everything else stays runtime-built. `bake.bat` → `tools/bake_map.py` bakes each
  exported map into a **gitignored** plugin mount (`Plugins/ElysiumBaked/Content` → `/ElysiumBaked`;
  only the `.uplugin` is committed — bring-your-own-game applied to a new artefact class, not
  excepted for it), and `AElysiumMapActor` **adopts** the baked level at load, bucketing its actors
  by the tag the bake stamped, while collision, ropes, the sky cube, the entity substrate, NPCs,
  audio and scripting stay runtime-built. The entry records what forced it, the split, the
  file-based seam, the fact that light *values* are re-derived at load rather than adopted (so live
  calibration always wins), and the accepted costs: unbaked maps refused at travel, `shots.bat`
  baselines invalidated per re-bake, an overwriting-but-not-pruning bake, and packaging's
  bake-on-first-run question (10.5).

  **The charter docs** were the stale half. `CLAUDE.md` lost "builds all engine objects in code at
  map-load time — no `.uasset` baking, no editor content loop" for the real two-stage description;
  its bring-your-own-game section now names the gitignored mount, the two-clean-halves section
  carries the bake as the second offline stage and adoption-by-tag as the runtime's first act,
  `bake.bat` joined the script table, and `uasset-bake-spike.md` joined the doc index.
  `rebuild-strategy.md` lost "No original game content is ever converted into `.uasset`s"
  (re-stated as the thing that actually holds: none of it is ever *committed*), principle 1's "No
  import step, no bake, no editor involvement", principle 2's "The Unreal editor is never in the
  content loop" (now: offline only, never at runtime), "Nanite is not applicable" (it is on for 311
  of 339 tutorial meshes; the 28 exceptions are the translucent/additive surfaces) and "never
  reintroduce `.uasset` baking"; the Godot→Unreal mapping rows that still described world and prop
  geometry as runtime PMC/ISM were corrected to the baked `SM_*` reality. `uasset-bake-spike.md`
  dropped its "exploratory — not a decision" banner for an adopted-and-on-`main` one, and its
  now-closed gaps (perf unmeasured, one map) were replaced by what is actually still open (~98
  unbaked maps, packaging).

  **The merge** had already landed: `spike/uasset-bake` and `main` are the same commit (0/0
  divergence) with every bake file — `bake.bat`, `tools/bake_{map,lib,verify}.py`,
  `Plugins/ElysiumBaked/ElysiumBaked.uplugin`, `Source/ElysiumUE/Public/ElysiumBakedTags.h`,
  `docs/uasset-bake-spike.md` — tracked on `main`. The "27 commits ahead" the task carried was
  stale; verification, not a merge, is what this half needed.

  **Residue found, handed on rather than fixed** (the task is docs + git, and this is code):
  the superseded Lumen-card path has two dead ends — `tools/export_all.py` still calls a deleted
  `cards.bat` through `bake_cards`/`--no-cards`, printing a "skipped" line on every export run, and
  `ElysiumCardGen.cpp` still compiles into editor targets behind `ELYSIUM_WITH_CARDGEN` for the
  `elysium.cards.probe` verb. The runtime consumer (`elysium.LumenCards`, `AttachLumenCards`, the
  `.cards` sidecar reader) is already gone. Tracked as **PL11**. *Deps:* none — and everything
  since 2026-07-25 informally depended on it.

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

## P3 — Lighting correct & engaged *(parallel lane; detail: `rendering-perf.md`, `lighting.md`)*

- [x] **3.5 Sky IBL onto the SkyLight** *(was L2.1)* — landed inside the SKY rework's C1/C2
  rather than as a standalone task: `BuildSkyCubeFrom` assigns the correctly-assembled cube
  (`SLS_SpecifiedCubemap` + `RecaptureSky()`, `ElysiumMapActor.cpp`), and intensity is the
  map's own type-5 `emit_skyambient` magnitude ÷ the cube's solid-angle-weighted
  upper-hemisphere mean (`bLowerHemisphereIsBlack`) — **zero on the 83 maps with no
  `light_environment`** (owner call D2, `decisions.md` 2026-07-26). The original task text's
  "keep a floor ambient (night skies are near-black)" is superseded by that call: a
  night-sky lift goes through the D3 knobs (Skylight Leaking, this file → SKY/C3), never a
  SkyLight floor. Verified on `sm_hub_1` (no pair → intensity 0.000) and `sp_tutorial_1`
  (`la` cube integrates to 0.00335 against an authored 0.00656 → intensity 1.955). Full
  record: this file → SKY, C1/C2.

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

- [x] **7.5 Real reflections** *(was L3.1)* — the `$envmap` reflection channel: RE'd against VtMB's
  own shipped shaders, calibrated, and extended to props.

  **RE (the faithful term, exactly).** VtMB ships its DX8 pixel shaders as readable ps.1.1
  assembly under `materials/dxshaders/*.psh` (the RE-A9 route), so the composite is a file read:
  `lightmappedgeneric_maskedenvmap.psh` is `(base·vertexColor + cube·mask·tint) · lightmap · 2`.
  **The reflection is an ALBEDO term the lightmap multiplies, not an additive overlay** — so a
  reflective surface in an unlit room stays dark, which is why 22% of the game's materials can be
  reflective without reading as chrome. That is already a light-modulated specular response in all
  but name, which makes a real reflection channel the faithful port rather than a liberty. It also
  **corrects `docs/lighting.md`**, which recorded the term as additive-through-`EMISSION` (the
  Godot prototype's route). `vertexlitgeneric` composites identically, so world and props take one
  treatment. No Fresnel on either — the mask is flat.

  **`$envmapcontrast`/`$envmapsaturation` do not exist.** No term for either appears in any shipped
  `.psh`, and the authoring agrees: over 11,624 VMTs, 2,610 carry `$envmap`, of which saturation is
  authored **0** times and contrast **18** (17× 1.0). Parsed offline, deliberately unconsumed, and
  no longer emitted to the `.mtl`.

  **Channel.** `tools/make_world_materials.py` gains `RoughBase`/`RoughReflect`/`SpecBase`/
  `SpecReflect`/`EnvTint`/`MetalMask`; `env = saturate(EnvMask.r · EnvStrength)` drives
  `Roughness = lerp(RoughBase, RoughReflect, env)` and `Specular = lerp(SpecBase, SpecReflect, env)
  · (1 − f)` (the fog fade `mat_fog.specular` already established, now parameterised rather than a
  hardcoded 0.5). **The non-reflective world is Lambert** — `RoughBase` 1.0 / `SpecBase` 0.0 —
  which is what the material data says (`lighting.md`: METALLIC 0, SPECULAR 0, ROUGHNESS 1) and
  what `UElysiumLightRig` already assumed with `specular_scale = 0`; the previous 0.5/0.5 was an
  unconnected-pin default, never calibrated.

  **`$envmaptint` is VtMB's own metal mask.** The population is bimodal, not a continuum: 2,146
  unset, 362 grey (361 at *exactly* zero channel spread), 102 chromatic — the next spread value
  above 0.00 is 0.05, so the 0.02 threshold separates them with a clear gap. Grey is a
  reflection-strength dim-down and its Rec.709 luma scales `SpecReflect`. Chromatic (brass
  `0.65 0.5 0`, copper `0.74 0.57 0.31`, gold `1 0.7 0`) names a metal, so it drives `Metallic` off
  the mask with `EnvTint` on BaseColor — metalness is **read, never inferred**, which is what
  `asset-enhancement.md` requires. Translucent/additive surfaces are excluded: the blue/teal tints
  there are coloured glass, which stays dielectric.

  **Props gained the channel.** `vertexlitgeneric` is 1,419 of the 2,610 reflective VMTs — the
  *larger* half — and `props/*.mtl` carried no `envmap` line at all. `mdl.py` now emits
  `envmap`/`envmapmask`/`envtint` on the same MTL contract the world uses (tutorial: 0 → 147
  reflective prop materials, 2 metals — `soccurtainrod`, `lantern`).

  **Two exporter corrections.** `$basealphaenvmapmask` masks with **`1 − alpha`**
  (`lightmappedgeneric_basealphamaskedenvmap.psh`: `mul r1, t2, 1-t3.a`); ours used the alpha
  uninverted. And reflectivity is no longer gated on the baked cube decoding — the Lumen path never
  samples `tex/cube/`, so the gate silently matted any surface whose cube failed and every
  `$envmap env_cubemap` face VBSP left unpatched.

  **Live over the baked instances.** The bake authors `MaterialInstanceConstant`s, which have no
  runtime setter, so every `elysium.*` material knob was dead on the path that renders.
  `AElysiumMapActor::ApplyMaterialOverrides` stands one MID per *unique* baked material (≈700 on
  the tutorial, not 1,400 slot-wise) in front of them. It is **lazy**: with every knob neutral no
  MID is created and the baked instances render exactly as authored — because a runtime
  `SetMaterial` drops the primitive's built texture-streaming data and the textures (albedo *and*
  `EnvMask`, which then reads its near-white 1×1 mip and mirrors the surface) fall back to a low
  mip. Knobs read the baked value and write it whole, so the pass is idempotent and returning a
  knob to neutral restores the authored value exactly. `elysium.MaterialOverrides` gates it;
  `RoughBase`/`RoughReflect`/`SpecBase`/`SpecReflect` pin (negative = neutral) and `EnvReflect`
  scales.

  **Tests.** `Elysium.Substrate.WorldMaterials` grows the tint parse + the three-way classification
  (grey / chromatic / chromatic-but-translucent). New **`Elysium.Content.ReflectionParams`** asserts
  each lit master carries every name the bake and runtime bind — they are bound by string from
  three places, so a rename that misses one is otherwise silent — plus the Lambert defaults and
  `EnvStrength` defaulting off.

  *Verified:* `build.bat` + `test.bat` green; all 10 maps re-exported and re-baked; `shots.bat`
  re-baselined on `sp_tutorial_1` (spawn mean 8.37) and `sm_hub_1` (spawn 4.53) — the old baseline
  was invalid against the new bake by B6's rule; `profile.bat` Lumen reflections **0.15–0.22 ms**
  against the committed 0.18–0.25 and Total GPU 5.08–5.54 against 5.08–6.18, so no perf regression.
  *Deps:* 3.5, 7.4.

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

- [x] **8.5 NPC presence + `scripted_sequence` minimal** — landed in three parts. **Presence** came
  with B3 (`npc_*`/`npc_maker` register, stand their glTF body at origin, dialog-gating inputs latch);
  **the vocabulary** with PL4 (per-NPC mesh glbs + shared animation-bank glbs); this entry is the rest.

  **The problem it fixes.** 25 of `sp_tutorial_1`'s 34 NPCs and 24 of `sm_hub_1`'s 86 stood in the
  reference T-pose, with 226 bank idle clips available and unused. The cause was the selection rule,
  not the loading: the old `LoadIdleAnim` took the first clip whose *label contained* "idle" in the
  NPC's own glb — and an NPC's own `.mdl` carries almost nothing but its dialogue clips.

  **Selection now uses the engine's own keys.** `mdl_skel.local_sequences` reads
  `StudioSeqDesc.szactivitynameindex` (the `ACT_*` literal — `activity@12` is -1 on disk because the
  game DLL resolves the name at model load, so the **name** is the durable key) and `actweight`.
  The runtime picks by activity in three tiers: the NPC's `default_disposition` →
  `vdata/system/dispositiontable.txt` `Animation Name` → `Stance_<Name>_Idle_*` (ACT_DISPOSITION),
  then `ACT_IDLE` by weight, then a loose fallback. Label substrings are never matched — `regular_cop`
  resolves 229 clips containing "idle", including `Stance_Dead_Idle_1` (0.07 s) and `Bed_Left_Idle`.
  Verified 54/54 NPCs resolve: 44 by stance, 9 by `ACT_IDLE`, 1 loose.

  **Manifest v2** (`npc_index.json` + `clips/<stem>.json`): clip metadata is stored once per *owning*
  stem rather than per NPC (~3.5k rows instead of 69k), which keeps the manifest at 5.5 MB instead of
  20+; a map parses ~2.2 MB of per-NPC slices. The idle policy needs 2–3 banks / 25 MB resident, not
  the 243 MB the full vocabulary references. Measured in-game: 63 ms + 72 ms, map load 2.51 s, no
  regression.

  **`UElysiumNpcAnimInstance`** — a native C++ anim instance, no Blueprint and no anim-graph asset:
  two `FAnimNode_SequencePlayer_Standalone` and a lerp, 0.25 s crossfade, `elysium.NpcAnim 0` to A/B
  against a single node. It exists because VtMB's stance banks ship almost no authored transitions —
  of 21 dispositions × 2 gendered banks, exactly one carries a `Stance_<D>_Trans_<a>_<b>` clip
  (`Stance_Neutral_Trans_1_2`, male only) — so a stance change cannot route through an authored blend
  the way the naming suggests. The proxy **must** implement `UpdateAnimationNode`: a sequence player
  that is never `Update_AnyThread`'d holds its start frame forever, which is exactly how the first
  cut shipped — poses changed on request and nothing ever advanced.

  **`scripted_sequence` / `aiscripted_sequence`** (×104 + 4) — `ElysiumScriptedSequence.cpp`. A beat
  places its NPC on the mark, plays `m_iszPlay` once, holds `m_iszPostIdle` looping, and fires
  `OnBeginSequence`/`OnEndSequence` either side; `m_iszIdle` is the pose the NPC waits in from map
  load, `m_iszNextScript` chains the next beat, and a beat naming `!playercontroller` runs as a
  timing shell so flow continues. The outputs are the point: 88 wires leave these entities, 48 of
  them `OnEndSequence`, and **67 of the 88 land on inputs that already exist**. `BeginSequence` is one
  registered input serving both the 68 I/O wires and the 68 receiver-qualified script calls, since a
  Python attribute and a Hammer input are the same namespace. `StartPlayerDialog` was registered as a
  second name for `...Remote` so 4 of those `OnEndSequence` wires stop landing on nothing.

  **RE that changed the implementation.** The class is **`CCineNPC`** — an HL1 `CCineMonster`
  derivative, not HL2's `CAI_ScriptedSequence` (`aiscripted_sequence` factory `FUN_101a8fe0` → vftable
  `10477d1c` → datamap `10593628`). That fixes spawnflag bits 1–128, under which **no exported
  sequence carries START_ON_SPAWN**. The HL2 FGD bit order would have auto-started 27 of
  `sp_tutorial_1`'s 51 at map load. Full field/flag/output tables: `entity_io.md` → "Scripted
  sequences".

  **Corrections to `animation_and_movers.md`** found on the way: `fps` is not uniformly 30 (1,436@30,
  54@18 including `run`, 8@60, 4@20 over 1,502 sequences in six banks); a new A.3 records the
  activity-name selection key and that the sequence `flags` bit meanings are **not** established; and
  the `nummovements` row was rewritten — **66 of `move_and_ranged`'s 722 animdescs carry root motion**
  (`walk` 23, `run` 9, `sneak` 1), located but not decoded, which is why a walk clip plays in place
  and the feet slide.

  **Not reproduced:** locomotion (the NPC is placed on the mark, not walked there),
  `OnScriptEvent01..08` (needs decoded animation events), and ambient stance cycling (all three
  `dispositiontable.txt` timing rules are conversation-scoped). All four calls plus the
  `StartPlayerDialog` assumption: `decisions.md` 2026-07-26.

  *Verified:* `Elysium.Substrate.ScriptedSequence` (placement, both outputs, the chain, the
  NOSCRIPTMOVEMENT gate, a player-targeted shell) and `Elysium.Content.ScriptedSequenceClips`
  (106 sequences over 4 maps, 88/88 NPC-targeted animation references resolve); `test.bat` 29/29.
  In-game on `sp_tutorial_1`: `sJack_waveover` turns Jack to the marker's facing, plays `waveover01`
  (2.53 s, `ACT_WAVEOVER`, from `character_shared_male_misc`) and settles to his stance idle;
  `script_7b.BeginSequence` places Jack on his mark 187 m from spawn and its `OnEndSequence` fires
  `Jack.StartPlayerDialog`, which opens the conversation and re-fires his own `OnDialogBegin`.
  A debug-reporting bug surfaced and was fixed alongside: `ent_dump`, the MCP entity view and the Cog
  inspector all printed an entity's **def** origin, so a placed NPC read as still at its spawn point.
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

- [x] **8.7 Ropes** — VtMB strings its overhead wires as chains of `move_rope`/`keyframe_rope`
  nodes linked by `NextKey`; each link is one sagging cable. Rendered with the stock UE 5.8
  **`CableComponent`** plugin's `UCableComponent` (Verlet strand), the roadmap's stated target.
  Built the pure-visual sidecar→map-actor way (like 7.2 decals), **not** the entity substrate — a
  cable spans two entities and the rope nodes carry no meaningful I/O, so they stay inert records in
  `.ents`.
  - **RE correction.** `entity_visuals.md` R3 stated "`keyframe_rope` is a start node, `move_rope`
    a mid/end node." The tutorial data is the opposite: the 31 `move_rope` are exactly the 31
    topological chain starts (a node no other node's `NextKey` targets); the 76 `keyframe_rope` are
    continuations. Chain resolution is **topological** (start = untargeted), so it is correct
    regardless of classname; the doc was fixed. Source rope keys confirmed from the exported
    `.ents`: `Width`, `Slack`, `Subdiv`, `TextureScale`, `RopeMaterial` (`cable/cable` /
    `cable/chain` / `cable/chainb` in the tutorial) present on all 107 nodes; `MoveSpeed`/`MoveTime`/
    `Tension` (the animated-rope behaviour) rendered at rest.
  - **Follow-up audit + `Type` fix (2026-07-25).** The cables still read wrong in game, so the whole
    pipeline was re-audited export → placement. **Placement was never the defect:** every rope node
    re-derived straight from the entity lump on all seven exported maps matches the emitted endpoints
    1:1; first-wins `NextKey` binding and a nearest-position heuristic agree on **every** emitted
    segment on every map; and a sweep of all 108 maps finds no rope node inside any `sky_camera` room,
    so nothing needed the 3D-skybox miniature transform. The apparent broken links are map-data typos
    (122 dangling `NextKey` names game-wide) the engine also draws nothing for — now logged instead of
    silently dropped.
    The real defect was the node count. RE'd in `vampire.dll`: `m_nSegments` comes from **`Type`**
    (`CRopeKeyframe::KeyValue` `0x1019f2b0` — 0 → 10, 1 → 4, else → 2; `Activate` `0x1019e310` clamps
    `[2, 10]`), **not** `Subdiv`, which is client-side render tessellation capped by client.dll's
    `rope_subdiv`. The runtime had invented `clamp(Subdiv × 3, 4, 16)`, which used the wrong field and
    exceeded VtMB's max of 10. **25% of the game's 2,688 rope nodes are `Type 2` = two nodes = one
    span between two locked points, which cannot sag** (observatory lift cables, hanging-lamp and
    crucifix chains); they were being given nine Verlet spans and gravity, drooping ~5 m over a 48.8 m
    run. `Dangling` (clears `ROPE_LOCK_END_POINT` — 51 nodes game-wide), `RopeShader`, `Collide`,
    `Barbed` and `Breakable` were dropped entirely. Sidecar widened to 12 tokens (`nodes` replaces
    `subdiv`, `flags` added); runtime sets `NumSegments = nodes − 1`, `bAttachEnd = !Dangling`, and
    `SolverIterations` 2 → 8 so the shape settles on its catenary. `CableLength = span + slack` was
    **confirmed** against `RecalculateLength` (`0x1019e5d0`) and `RopeThink` (`0x1019efb0`) rather
    than assumed. Also fixed a latent export crash: `write_ropes` used a bare `float()` on origins, so
    `hw_jewelry_1`'s comma-decimal chandelier origins (`"-3496,92 …"`) raised `ValueError` and took
    that map's entire export down; now parsed with C `atof` semantics like the engine. **Open:** the
    sag depth `Slack` forces (median ~4.0 m on `sm_hub_1`'s 25–28 m street wires) matches the RE'd
    rest length and converges on the analytic catenary, but is not yet A/B'd against the running
    original. Full RE + field map: `entity_visuals.md` R3; `decisions.md` 2026-07-25.
  - **The actual misplacement was a second, runtime bug (2026-07-25 cont.):** `UCableComponent`
    resolves `EndLocation` against `AttachEndTo.GetComponent(GetOwner())`, and an unset
    `FComponentReference` falls back to the owner's **root** component (`ExtractComponent`,
    `EngineTypes.cpp`) — never null — so the `EndComponent = this` fallback in `GetEndPositions`
    is unreachable. With cables attached under `SceneRoot` (identity), `EndLocation = B − A` was an
    absolute world point: all 70 cable far-ends converged near the world origin. Fix:
    `EndLocation = B` (SceneRoot space is world space). Verified in-game by MCP screenshot at three
    sites against the rope-node gizmo boxes. `decisions.md` 2026-07-25 (cont.).
  - **Rest length: the client half (2026-07-25 cont. 2).** With placement fixed, the cables sagged
    far deeper than the original. `CableLength = span + slack` — asserted "confirmed" above from the
    server side alone — is wrong: half the computation is in `client.dll`.
    `C_RopeKeyframe::RecomputeSprings` (`0x100bf1a0`, reached from the shared `m_Slack`/`m_RopeLength`
    RecvProxy `0x100be290`) computes `springDist = (m_RopeLength + m_Slack − 100) / (nodes − 1)` and
    `CBaseRopePhysics::ResetSpringLength` (`0x10128ae0`) floors it at 0. So `Slack` is applied
    **twice**, a flat **−100 units** is subtracted (`LEA EAX,[EAX + EDX*0x1 + -0x64]`), and the
    divide is **integer** (`CDQ`/`IDIV`) — rest ≈ `(int)|B − A| + 2·Slack − 100`. Authored `Slack` is
    0..100 game-wide, so the −100 dominates and **most ropes hang taut**: 26 of `sp_tutorial_1`'s 70
    cables now rest below their span, the chophouse meat-hook links drop from 2.6× span to 1.5×, and
    the street wires from ~12% surplus to ~9%. Sag is genuinely simulated — `C_RopeKeyframe::Init`
    (`0x100c04d0`) lerps nodes along the chord then runs `RunRopeSimulation(5.0f)` (`0x100bf360`),
    which the ctor's `m_RopeFlags = 0x48` enables. The arithmetic is integral and in Source units, so
    it is resolved **in the exporter**: sidecar column 9 changes meaning from `slack_cm` to `rest_cm`
    (still 12 tokens) and `BuildRopes` assigns `CableLength = RestCm` verbatim, with
    `RestCm < |B − A|` the normal case. Exporter keyvalue defaults also corrected from the ctor
    (`0x1019dc80`): `Slack` 0 (was 25), `TextureScale` 4 clamped `[0.1, 10]` (was 1), and no `Type`
    key keeps `m_nSegments` 5 (was treated as `Type 0` → 10). Verified in-game by MCP screenshot.
    `decisions.md` 2026-07-25 (cont. 2).
  - **Exporter** (`UE_bsp_to_scene.py::write_ropes`, run in `main()` next to `write_sprites`):
    collects every rope node, indexes by targetname, and for each node with a `NextKey` emits one
    `<map>.ropes` segment — `tex ax ay az bx by bz width_cm rest_cm nodes texscale flags`. Endpoints
    are the two node origins via `source_to_unreal` (as `.ents` does); `Width` is a length
    (`× INCH_TO_CM`). A node whose start lacks a targetname is still iterated (it can only be a chain
    start), so no segment is lost.
    - **`NextKey` resolves first-match by entity order** — the fix for wires "all over the place,"
      grounded in the decompile. `sp_tutorial_1` has **two separate telephone-wire installations that
      reuse the names `tele4`..`tele9`** (one near `[1300, 2600, 900]`, another ~200 m away near
      `[3000, −19000, 400]` — both in the playable world; the `sky_camera` PVS confirms **neither is in
      the 3D skybox**). A last-wins name index cross-linked a node in one installation to the
      same-named node in the other, producing six ~199 m cables slashing across the map. **RE
      (vampire.dll, Ghidra):** `keyframe_rope`/`move_rope` are stock Source **`CRopeKeyframe`**
      (factories `FUN_1019d680`/`6f0`; keyfields `Slack`/`Width`/`TextureScale`/`Subdiv`/`RopeMaterial`
      → `RopeShader` 0/1/2 = `cable/cable`|`cable/rope`|`cable/chain`; networked `m_hStartPoint`/
      `m_hEndPoint`), and `Activate` resolves `NextKey` via `FindEntityByName(NULL, m_iNextLinkName)` —
      the **first** entity of that name in spawn/entity order, i.e. the entity-lump order the exporter
      already iterates. So the index is **first-wins** (not last-wins). Verified: first-wins and a
      nearest-position heuristic give the **identical** 70 segments here (the lump orders each
      installation contiguously), so the faithful engine rule is used; longest span 27 m ≪ the 200 m
      gap, nothing bridges the two.
    - **Coincident endpoints (< 1 cm apart) are dropped** — a chain artifact that would build a
      zero-length cable.
    The `RopeMaterial` `$basetexture` is decoded to `tex/rope_*.png` (the `write_sprites` VMT→TTH/TTZ
    path); a decode miss writes `-` and the runtime falls back to a plain MID. Tutorial: **70 segments**
    (72 links − 2 coincident) / 107 nodes / 3 textures.
  - **Runtime.** `ElysiumRopes.{h,cpp}` (`FElysiumRopeDef` + `Parse`/`ParseLines`, the file-free
    core mirroring `ElysiumDecals`). `AElysiumMapActor::BuildRopes` (after `BuildDecals`, its own
    `Phase("Ropes")`): parses `<map>.ropes` and per segment builds/caches a MID off **`M_World_Opaque`**
    (a lit opaque strand — `FElysiumMaterialDef{Albedo=tex}` through the world material factory, one
    MID per unique texture) and stands a `UCableComponent` on `SceneRoot` — relative location `A`,
    `EndLocation = B − A`, both ends fixed (`bAttachStart`/`bAttachEnd`), `CableLength = dist(A,B) +
    SlackCm` (the slack is exactly the extra length that makes it hang), `CableWidth = WidthCm`,
    `NumSides = 4` (thin tube), `NumSegments` from `Subdiv`, `TileMaterial` length-proportional ×
    `TexScale` (no stretch), no collision. Kept in a `UPROPERTY TArray<TObjectPtr<UCableComponent>>
    Ropes` — freed with the map actor (the map-epoch teardown, no manual cleanup), `RopeCount` for the
    Maps window. `MapRopes()` content path; `elysium.Ropes` cvar A/Bs the pass at map load.
  - **Dependency.** `CableComponent` added to `ElysiumUE.Build.cs` (`PrivateDependencyModuleNames`)
    and enabled in `ElysiumUE.uproject`. It is a stock, enabled-by-default, non-beta first-party UE
    5.8 plugin (`decisions.md` 2026-07-24).
  - **Verified.** `build.bat` clean; `test.bat` Substrate + Content green — new
    `Elysium.Substrate.Ropes` (11-token parse + the `CableLength = distance + slack` contract) and
    `Elysium.Content.TutorialRopes` (every segment has distinct endpoints / positive width /
    non-negative slack / ≥1 subdivision, and every named texture exists on disk). In-game
    (`play.bat sp_tutorial_1`) the map logs `ropes: 70 cables` and the wires hang between the
    utility pole and wall brackets with correct catenary sag. **Cable Verlet settle** (cables
    initialise straight and sag over ~1 s) and finer texture-tiling tuning are cosmetic follow-ups.
    *Deps:* none.

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
- [x] **9.3b Console bridge — `ccmd` + the `cfg` alias table** *(the fifth scripting surface)* — landed.
  **Store:** `FElysiumConsole` (`ElysiumConsole.{h,cpp}`, plain C++, no Python/UObject) parses `out/cfg`
  into an alias table + a cvar table (load order `default → config → autoexec → user`, later shadows
  earlier — so `user.cfg`'s `alias patchtype "setPlus()"` wins), and `Execute(cmd)` resolves each
  `;`-split word: an alias expands (recursively, depth-capped), a known cvar with an arg is set, else the
  word falls through to Python. Owned by `FElysiumPythonVM`, seeded at `EnsureStarted`, its Python sink
  wired to `ExecConsoleLine` (exec in `__main__`; a `NameError`/`SyntaxError` reports "not Python" so an
  engine cvar/command we do not model — `rope_shake`, `+speed` — is dropped with a Verbose note, while a
  real body that raises PyErr_Prints error-to-false like every eval). **Objects:** `vampire.ccmd`
  (attribute-*set* executes `name` + optional arg; attribute-*get* returns `""` and does **not** execute,
  matching retail's SET-only semantics, so a bare `ccmd.wc_create` field-6 is a harmless no-op) and
  `vampire.cvar` (get reads a value as a string — `cvar.name` returned `"Noa"` from `config.cfg`; set
  stores), both data-less singletons in `ElysiumPythonEntity.cpp` forwarding to the console.
  **Bootstrap:** `__main__.ccmd`/`cvar` bound; the stub `vamputil` dropped so the **real `vamputil.py`
  imports**; and a `Character` compatibility class bound (see the divergence below). **File-root
  redirect:** VtMB's scripts touch files as `nt.getcwd() + "\\" + sys.moddir + "\\<tree>\\…"`, so the VM
  sets `sys.moddir = "."` and monkeypatches its own `nt.getcwd` to the absolute `out/` root — contained to
  the interpreter, the UE process cwd untouched — so `setPlus`'s `FixKeyBindings` finds `out/cfg/config.cfg`
  (no `vdiscipline_last`/`feed` binds → empty `data` → no write, no `execonsole`) and returns cleanly,
  letting `setPlus` reach its Tutorial branch. **PL5d** (`UE_extract_cfg.py`, wired into `export_all.py`,
  `--no-cfg`) mirrors `cfg/*.cfg` verbatim, patch-first, into `out/cfg`. **Divergence (`decisions.md`
  2026-07-24):** `vampire` binds a mutable **`Character`** class the patch monkeypatches
  (`vamputil.py:3270` `from __main__ import Character`; `Character.Near = _Near`). A C extension type
  rejects attribute assignment, and our 24 Character methods dispatch off the `Entity`/`Player` getattro
  (not a shared class), so `Character` is a mutable old-style compatibility stub — the monkeypatched `Near`
  does not reach live C entity instances (only the unused `AnimalRadar` path uses it), but the import
  completing is what unblocks the whole real vamputil. **Verified (built game, fresh New Game via MCP):**
  the boot log shows `loading tutorial level script → :::: ZVTOOL LOADED → Loaded level script: tutorial`
  (no `ImportError`) then `Plus Patch` (`setPlus`'s own print); the I/O history shows, unassisted,
  `unhidePlus() → c.patchtype="" → events_player_plus.EnableOutputs() → trig_popup_move.Enable()`, and
  `ccmd.wc_create → ""`; a `Elysium.Substrate.Console` unit test covers the parse + alias→Python fallthrough
  + cvar-set path. *Deps:* 9.3.

- [x] **9.1 `.dlg` parser + dlgexpr** — landed as `ElysiumDlg.{h,cpp}`, three separable, unit-testable
  pieces. **Parser** (`FElysiumDlgFile::ParseBytes`/`LoadFile`): Latin-1 decode, CRLF rows, `}{`-joined
  13-field records → `FElysiumDlgLine` (id / M+F text / link / col-4 / col-5 / short label) with an
  id→index map; tolerates the corpus-wide 14-field `kiki.dlg` typo and skips a stray non-13-field row
  without aborting the file. Roles from col-3: `#`=NPC line, a number=PC choice (`0`=END), empty=padding.
  **dlgexpr normalizer** (`ElysiumDlgExpr::ConditionToPython`/`ActionToPython`): a total string→string
  front layer that rewrites the engine grammar into the pure-Python subset the installed host evaluates —
  a skill-check run `IDENT [relop] INT` (bare ident, not member/call, not a keyword) → `CalcFeat("IDENT")
  relop INT` (implicit `>=`), the condition-level `&`/`|` → `and`/`or`, the action-level `&` → `;`;
  everything else is copied verbatim (source-span rebuild, so pass-through spacing is exact), and a shape
  it cannot classify falls through to the host's error-to-false (RE3). dlgexpr has no bitwise operators,
  which is what makes the `&`/`|` rewrite unambiguous. **Branch machine** (`FElysiumDlgConversation`):
  host-agnostic (injected condition-eval + action-exec callbacks, so it drives from a unit test with a
  fake `G` or in-game through `EvalCondition`). Opens at the first NPC line with non-empty text (blank
  leading NPC lines are not real turns — interim entry rule, `decisions.md`); an NPC line's col-4 **and**
  col-5 are executed when spoken (NPC col-4 = action, resolved by the entry line's `G.Story_State = -3`
  assignment); it gathers the contiguous following PC rows whose col-4 gate passes; a pick runs col-5 and
  follows the col-3 link (`0` or a dangling link ends); a no-choice line is terminal. `game_runtime.md`
  §5/§7 corrected in the same pass (NPC col-4 = action). **Verified:** `Elysium.Substrate.DlgParse` /
  `DlgExpr` / `DlgBranch` (parse incl. 14-field tolerance, skill-check/join rewrites, a synthetic branch
  walk), `Elysium.Content.DlgCorpus` (**every NPC dialogue of the exported test-bench maps — 25 files,
  10,949 rows, all parsed + branch-walked, 0 dangling links**), and `Elysium.Content.DlgJackTutorial` (the
  1116-row beat, walked to the `G.Tut_Jack=1` action and END). Consumed in-game by **B4**. *Deps:* 5.2.

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

## P10 — Scale & ship-shape

- [x] **10.8 OpenLevel map-lifecycle migration** — retired the bespoke persistent-world
  content-swap (destroy the map actor in-place, `FElysiumTextureCache::FlushAll`,
  `ForceGarbageCollection(true)`, respawn a fresh actor in the same `UWorld`) for UE5 standard hard
  travel. **The seam is unchanged:** `UElysiumMapSubsystem::Travel(map, landmark)` and
  `RequestLandmarkTravel` stay the entry points, and the `NextLandmarkSpawn` carry-over still hands
  the destination placement (`dest landmark origin + source offset`, view yaw) to the fresh map's
  `ResolveLandmarkSpawn`. **What changed underneath:** `Travel` stows the target in a GI-scoped
  `PendingMapLoad` and calls `UGameplayStatics::OpenLevel` on the one reused shell (`/Game/Elysium`,
  `make_boot_map.py`); the engine's `LoadMap` tears down the world and GCs; the fresh world's
  `AElysiumGameMode::BeginPlay` sees `HasPendingMapLoad()` and calls `SpawnPendingMap`, which spawns
  the `AElysiumMapActor` (deferred, `MapName` set before `BeginPlay`) that reads the target from GI
  state and builds in code. **Cold boot** skips the redundant re-open: with no map yet loaded,
  `Travel` spawns directly into the already-empty boot shell.
  - **Next-tick defer removed.** `RequestLandmarkTravel` fired from inside the entity-world tick used
    to schedule `FlushPendingTravel` on a next-tick timer because the old `Travel` destroyed the
    ticking actor under the stack. `OpenLevel` sets a travel URL that `UEngine::TickWorldTravel`
    processes at end of frame, so it is safe to call mid-tick — the defer (and the `FPendingTravel`
    struct) are gone. The "one transition per frame, first wins" guard is preserved (a second
    `RequestLandmarkTravel` sees `PendingMapLoad` already valid and bails).
  - **`IsPlayerSeated` gate removed.** It suppressed brush touches while the *persistent* pawn still
    stood at the previous map's coordinates after an in-place swap. A fresh world + fresh pawn per
    map has no such stale position, so the gate (and `AElysiumMapActor::IsPlayerSeated` +
    `ElysiumBrushComponent`'s check) are retired. Residual to watch: the fresh pawn spawns at the
    shell's origin before the map actor teleports it to the landmark, so a trigger volume containing
    origin could in principle fire a one-frame touch — flagged for play-test verification.
  - **Texture cache re-scoped (the freeing half of "hand teardown to the engine").** The cache was a
    process-wide static `TStrongObjectPtr` table whose *only* release was `FlushAll` — retiring
    `FlushAll` alone would have leaked every map's textures forever. So `FElysiumTextureCache` became
    a **per-map instance owned by the map actor** (a `TPimplPtr` member, created at the top of
    `LoadMap`), threaded into `FElysiumMaterialFactory::Build`/`BuildDecal` and
    `FElysiumStaticMeshBuilder::Build`; its strong refs drop when the actor is torn down and GC
    reclaims the textures. It stays an intra-map dedup index (each unique texture decoded/uploaded
    once, shared by every surface) — the engine's own dedup lives at the `.uasset`/`LoadObject` layer
    this project bypasses by decoding loose PNG/DDS into transient textures. The weak-process-index
    model the old `map-architecture.md` "Ownership" section described was never built and is moot
    under hard travel (only one map is ever resident).
  - **Cross-map state verified GI-scoped.** `UElysiumMapSubsystem`, `UElysiumGameStateSubsystem`
    (`G`/quest/sheet + the script host + the embedded CPython VM), and `UElysiumAudioSubsystem` are
    GameInstance subsystems and survive `LoadMap`; the MCP subsystem is an engine subsystem. The
    CPython host resolves the live entity world each call (`CurrentEntityWorld()` → the current map
    actor's world), so the persistent VM tracks the fresh world after travel — the same resolution
    the old swap already relied on. Generation-checked `Entity` handles correctly report "deleted"
    for the torn-down world.
  - *Verified:* `build.bat` clean (editor target); `test.bat` Substrate tier green. Automation does
    not directly drive a travel (it needs a live world), so the migration's correctness rests on the
    build + the logic above; the `Content.TutorialEnts` red in the run was a pre-existing local
    export mismatch (a retail `sp_tutorial_1` export, 1226 entities / 75 classnames, vs the test's
    patch-calibrated 1600–2100 band), unrelated to this task. Owner call + as-built correction:
    `decisions.md` 2026-07-24.

## RE backlog — the sky + ambience RE set (RE10–RE16 + SKY), verbatim rows

Moved from `roadmap.md` on 2026-07-26, when the set closed; the roadmap keeps
one-line summaries. Full write-ups: `docs/sky-ambience.md` (K1–K8).

| ID | Question | Consumed by | Status |
|---|---|---|---|
| RE10 | **Sky-face orientation convention (K1)** — `R_DrawSkyBox`/`MakeSkyVec` + the three `.rdata` tables give the face→axis binding (`rt`=+X, `lf`=−X, `bk`=+Y, `ft`=−Y), the per-face basis and the texcoord flip; **no face needs a rotation or mirror**, and the current `BuildSkyCube` binds every horizon face to the wrong axis. Cross-checked on the decoded faces by `tools/probe_sky_orientation.py`. Full write-up: `sky-ambience.md` → "K1 … (settled)". `R_DrawSkyBox` applies no colour modulation, so the brightness chain is entirely material-side — split out as RE-A9 (K7: the `stdshader_dx8.dll` sky shader, gamma, fog-over-sky) | 3.7, sky-ambience B2/B3 | [x] |
| RE11 | **Labelled-sky probe in the original game (RE-A2)** — six self-describing faces installed as a loose `materials/skybox/` set. **The shipped engine draws every face on the axis K1 predicts, upright and unmirrored**, with five three-face corner agreements, two of them on `dn` — the only image evidence `dn` can have, since VtMB's ground plates are uniform black and the seam probe ties at 0.00 across all eight transforms. K1 now rests on the `engine.dll` tables + the seam probe + the in-game draw. Also found: `GetAngles()` returns the body's angles, so its pitch is not the camera's. Tooling: `tools/sky_probe.py` + `tools/tex_from_png.py` (the `.tth`/`.ttz` writer). Full result: `sky-ambience.md` → "The in-game check" | sky-ambience RE-A2, B1 | [x] |
| RE12 | **Model lighting + the WORLDLIGHTS runtime role (K3, K5 — RE-A3)** — `engine.dll` + `StudioRender.dll`. **World surfaces render from lump 8 alone; lump 15 is runtime data for the model light cache and nothing else** (proved by an exhaustive field scan: the lump-8 and lump-15 consumer sets are disjoint in the binary). A model's ambient term is a **6-face ambient cube** built by a **162-ray radiosity gather** that samples `dface_t.avgLightColor[style]` (not luxels — `avgLightColor` at offset 0 is what the runtime reads) and multiplies by the hit material's **reflectivity**, substituting the `emit_skyambient` intensity on sky hits; on top of it, at most **`r_worldlights` (2)** direct worldlights per model, everything else folded into the cube, plus styled lights/dlights/elights per frame. So VtMB **does** have runtime one-bounce GI — for models. Also settled: the loader's attenuation/exponent/radius fixups, `Engine_WorldLightDistanceFalloff`/`WorldLightAngle`, `mat_fullbright` forced on zero-light maps, and **first-wins** (not last-wins, not summed) for multiple `light_environment`s. Corrections it forces: `lighting.md`'s "the set the engine rendered from", `bake_map.py`'s last-wins type-5, `bsp.read_worldlights`' missing fixups. Full write-up + addresses + ConVar table: `sky-ambience.md` → "K3 / K5 … (settled)" | sky-ambience RE-A3, C0–C2, D2/D6 | [x] |
| RE13 | **Day/night bake selection (K4 — RE-A4)** — **the premise was wrong: there is no second bake and no selector.** `dface_t.day[8]`@56 and `night[8]`@64 are `0x00` in all 4,538,720 bytes of each across all 108 maps, where `styles[8]`@48 in the same faces carries the `0xFF` unused sentinel plus live indices — untouched memory, not authored-and-empty. Per-face closure of lump 8 confirms one bake: 1 luxel grid per lightstyle (288,848 faces) or 4 + a 4-byte average colour when bumped (111,421), **never the 2 or 8 a day+night pair would cost**. In `engine.dll` the FACES lump has exactly three consumers — `Mod_LoadFaces` (`0x200b73d0`), the face-centroid builder (`0x200b9c30`) and `CMod_LoadDispInfo` (`0x20033b30`), enumerated by decoding the lump index at all 55 call sites of the lump accessor `0x200b6670` — and **none reads offsets 56–71**. No day/night string, ConVar, worldspawn key (full 108-map inventory) or entity key exists; the game's one `m_daylight_level` is a 3-bit networked player field driving a **client screen effect** (`client.dll` `0x1019dd08`/`0x1019dd9d`, gated by `cl_obfuscate_daylight`, default `-100`, set to `0` by `autoexec.cfg`) that never touches lump 8, lump 15 or the light cache. Corrections it forces: `sky-ambience.md`'s and `tools/CLAUDE.md`'s "v17 carries two full bakes" (inherited from bspsrc's field names), and the "night set, pending K4" caveat on D1/C4. Instrument: `tools/probe_daynight.py`. Full write-up: `sky-ambience.md` → "K4 … (settled)" | sky-ambience RE-A4, C4, D1 | [x] |
| RE14 | **Full-game sky + ambience inventory (K8 — RE-A7)** — all 108 BSPs scanned from data alone. **Every map names a sky** (11 distinct sets; `holly`/`chinatown` 256², the rest 512²) but only **66** have `toolsskybox` brushwork and only **25** carry the type-3/type-5 pair — the other **83 have no `light_environment` at all**, so "no sky pair → no sky light" (C2) is the majority policy, not an `sm_hub_1` special case; a pair always implies brushwork, never the reverse. **43** maps run the 3D-skybox pass, `scale` 16 on every one (`la_malkavian_4` has two `sky_camera`s), holding **1,442 of the game's 19,197 worldlights**, 1,043 static props and 2,275 entities — B7 is ~6× the ten-map sample. Fog: 65 maps have no `sky_camera`, so they export **no fog at all** (12 of them against `worldspawn fogenable 1`), and 5 get fog the `worldspawn` never asked for — B8's second failure direction. D6 moves exactly one map: 5 maps carry several `light_environment`s and only `sp_observatory_2`'s differ in value; first-wins resolves over **lump-15 order** (which `.lights` preserves), not entity order. Corrections it forces: two rows of RE-A8's sky-area table were PVS-classified, not area-classified (`sp_tutorial_1` 30/59/58, `sm_pawnshop_1` 21 props), and brush entities need their `origin` added to the model bbox centre. Open for K6: `sp_soc_2`'s two `light_environment`s emit one pair, and `sp_observatory_2`'s two pairs share one type-5 magnitude. Instrument: `tools/probe_sky_inventory.py`. Full write-up: `sky-ambience.md` → "The full-game inventory" | sky-ambience RE-A7, B7/B8, C1/C2, D6 | [x] |
| RE15 | **VRAD's sky-pair semantics (K6 — RE-A5)** — data-only: VtMB **ships no map compiler** (the install holds two `.exe`s and no `.fgd`), so there is no rad binary to decompile and never was. Recovered VRAD's photometric transfer exactly — `intensity = (colour/255)^2.2 · (brightness/255) · (const + 100·linear + 10000·quadratic)`, **zero exceptions on 16,378 origin-matched lights across all 108 maps** (worst relative error 2.25e-7). The third factor is the light's own falloff denominator at d = 100 units, so a compiled intensity is that light's radiance at 2.54 m — the `10000` long read as a brightness unit is 100². Lump 8 stores radiance ×255, giving the plan the thing it never had: **lump 8's absolute scale**, `stored luxel = 255 · intensity / falloff`, so a light of brightness `B` lands `(colour/255)^2.2 · B` at 100 units and the authored brightness comes back out of the bake. **The sun measures that scale and confirms its own rule** — `255 · intensity · cos`, gated by a sky-visibility test: on `sp_endsequences_b` the sky-visible luxels give baked/cos p50/p90/p99 = 170.3/170.4/170.6 against a predicted 168.2 (×1.01), carrying the sun's chromaticity (d = 0.019) and not the map's warm fill (d = 0.523). Across all 17 retail maps the colour test partitions the set cleanly: **exactly the three maps whose sun outshines their own fill** (ceilings 203/200/168) land on the prediction — ×1.09, ×0.87, ×1.01, median **1.01** — while every map where the estimator runs away (to ×26.7) has a bright end matching the **fill** instead, `sp_ninesintro` at d = 0.927 to its sun against 0.008 to its fill. The diagnostic fails exactly where it reports failing. So **where sky is visible the pair is a first-class term, not a tint**: the sun's ceiling is a median **332%** of its map's median lit face (max 3811%), the skyambient's **121%** (max 640%) — what limits it is reach, VtMB's maps being mostly enclosed, not magnitude. Multi-`light_environment` bake rule read off `sp_observatory_2`: the ambient is resolved **once, globally, first-entity-wins** and stamped on every type-5 row (its two entities author `_ambient` 20 and 10; both rows read 20/255), while each entity keeps its own type-3 — so **D6 needs no decision**. Also established the **bake provenance split** every future lump-8 measurement needs: the Unofficial Patch recompiles 20 maps and adds 7, with a later VRAD, so 27 of 108 bakes are not Troika's. Corrections it forces: this plan's own RE-A5 target (`sp_tutorial_1` is patch-recompiled and has **no** retail sky pair) and RE-A7's `sp_soc_2` note (retail emits both pairs; the single pair is the patch compiler). Left deliberately **bounded**: the skyambient's cosine-vs-uniform hemisphere weighting is a factor of ~2 that the fill confound and our brush-only occlusion cannot separate — its colour signature confirms it is in the bake, its aperture is not pinned. Instrument: `tools/probe_skyambient.py`. Full write-up: `sky-ambience.md` → "K6 …" | sky-ambience RE-A5, C0–C2/C4, D6 | [x] |
| RE16 | **The sky's brightness chain (K7 — RE-A9)** — `stdshader_dx8.dll` + `engine.dll` + `MaterialSystem.dll`, plus the shader assembly **VtMB ships as data** (`materials/dxshaders/*.psh` is readable ps.1.1 source with Valve's comments; `shaders/{vsh,psh}/*.vcs` the compiled combos — a file read, not a decompile). **The premise was wrong twice and the answer is the identity.** There is no `SkyBox` shader: all 79 `materials/skybox/*.vmt` are `UnlitGeneric` carrying only `$basetexture` and `$nofog`, and the registered `UnlitGeneric` is an alias whose `GetFallbackShader` (`0x1000fa00`) returns the literal `"UnlitGeneric_DX8"`. And there is no scaling: `R_DrawSkyBox` pushes a (1,1,1) colour modulation into the material (`IMaterial` vtable `+0x78`) before binding it, `SetModulationDynamicState` (`0x10001ca0`) folds `$color`×`$alpha` into vertex constant **c38**, all 8 combos of `unlitgeneric.vcs` do `MOV oD0, c38`, and `unlitgeneric.psh` is `tex t0; mul r0, t0, v0` — **a sky pixel is the decoded texel**. The one sky-vs-world asymmetry is `lightmappedgeneric.psh`'s `mul_x2 … (overbrightFactor/2)`, with the factor **pinned** to 2 by `UpdateMaterialSystemConfig` (`0x200718d0`, which rewrites anything but 1.0/2.0 — and any hardware without overbright support — to 2.0), so **sky = texel, world = albedo × lightmap × 2**. Gamma is frame-wide, never per-material (`gamma`/`texgamma` 2.2, `brightness` 0, `linearFrameBuffer` 0, display value `1.6 − clamp(cl_v_gamma − 1, 0, 3)·0.5` = 1.35 by default). Fog: `$nofog` is material-var flag **bit 14** (name table recovered from `MaterialSystem.dll` `.data`, index cross-checked five ways against the shader code that tests `$model`/`$ignorez`/`$decal`/`$nocull`/`$alphatest`), and all five of `CBaseShader`'s fog helpers (`0x10016ca0`–`0x10016d60`) pass `IShaderShadow::FogMode(0)` when it is set — so with `$nofog 1` on **all 66** faces any map's `skyname` selects, **the `sky_camera`'s fog never touches the backdrop**, while the miniature (RE-A8) is fogged by it. Corroborates `color_gamma.md`'s `base × lightmap × 2` from the shipped shader source and upgrades "defaulted" to "pinned". What it changes: B4 loses its unknown and `Brightness 4` becomes divergence **D7** rather than a pending calibration; B8 gains a hard rule (never fog the backdrop, always fog the miniature); B5 inherits a constraint (an upscaled face must preserve absolute texel values). New local Ghidra script `MakeFuncs.java` — promotes disassembled-but-unowned code into functions, without which a plugin DLL's vtable-only-reached shader classes are invisible to every function-walking tool. Full write-up: `sky-ambience.md` → "K7 …" | sky-ambience RE-A9, B4/B5/B8, D7 | [x] |
| SKY | **Sky + ambience rework — Phases B and C (`sky-ambience.md`)** — the whole rework the RE set up, landed 2026-07-26. **B:** the backdrop's face→slice binding *and* per-slice rotation fixed under one re-derived table (B3) with a standing `Elysium.Substrate.SkyCube` test and an in-engine labelled-cube confirmation (B1); the export-side orientation contract recorded as `skyconv` (B2); the backdrop shipped at **parity** per D7 with the transfer measured end to end — the tonemapper's toe crushes a night sky up to ×9 and no multiplier can undo a curve, so the residual belongs to 3.6/3.7 (B4); the enhanced-face A/B gated on absolute-texel preservation (B5); the **whole** 3D skybox split by `dleaf_t.area` — props, sprites, entities, worldlights, collision brushes — and placed under the miniature transform, ending the floating debris (B7); world fog re-sourced from `worldspawn`, fixing 12 maps that lost it and 6 that never asked, and the backdrop exempted from fog game-wide (B8); sky-framing vantages plus `shots_diff.py` so a sky regression is a number (B6). **C:** the four RE-A3/RE-A5 pipeline corrections including `.tth` reflectivity, verified against the decoded albedo at r = 1.0000 (C0); the sky light's level driven by the map's own `emit_skyambient` magnitude, scaled through the cube's measured upper-hemisphere mean, and **zero on the 83 maps with no `light_environment`** (C1/C2); the per-map Lumen art-direction PPV, neutral by default (C3); and the bake measured in **absolute units** with no free gain, which says direct light explains ~0% of a median lit face and the bounce floor *is* the ambient level (C4/C5). Corrections it forced: RE-A7's fog counts (6 wrongly-fogged maps, not 5; 27 disagreeing sets, not 31), and **D3's Indirect Lighting Intensity, which is a no-op on this render path** — it scales precomputed indirect only and no Lumen shader reads it. **B8b** closes the fog: the world and the miniature share screen depth — measured, the miniature's bounds sit 0–4,868 cm from the world's against world diagonals of 11,124–40,334 cm, and on 6 of 8 maps it lies *inside* the world's box — so no engine-side mechanism can scope them, and Source's own linear distance fog moves into the material as a per-primitive Custom Primitive Data term (neutral by construction, `Elysium.Substrate.FogPack`), leaving the height fog the volumetric layer alone; the authored fog colour is decoded with a plain 2.2 like every other VtMB colour. It also measured the shots harness's own noise floor — rebuilding **byte-identical** materials moves `sp_tutorial_1` `spawn` by 10.15 mean while the stable `pw` vantage is bit-exact — which retires the unexplained 32.46 residual left open on the same map. Still open and named: the volumetric layer's own calibration, and the skyambient's hemisphere aperture, which C4 shows is not identifiable from this data | 3.7, sky-ambience B1–B8b + C0–C5 | [x] |

## SKY — sky + ambience rework, Phases A–C: the full as-built record

Moved verbatim from `docs/sky-ambience.md` on 2026-07-26, when Phases B and C
closed; that doc keeps the engine-neutral facts (K1–K8, the inventory, the
instruments). Task IDs: RE-A1–A9 (Phase A), B1–B8b (Phase B), C0–C5 (Phase C),
decisions D1–D7 (dated entries: `decisions.md` 2026-07-26). The open residue
named in "Sequencing" below is promoted to roadmap tasks 3.10–3.13 and RE17.

## Phase A — RE: settle the facts

- **RE-A1 — decompile the sky draw. Done** (2026-07-26). `engine.dll`: the loader, the three
  `.rdata` tables, `R_DrawSkyBox` and `MakeSkyVec` give the face→axis binding, the per-face
  basis and the texcoord flip, and establish that the draw applies **no** colour scaling — so
  the brightness chain is entirely material-side. Closes K1 (with RE-A2). Full write-up above
  ("K1 …"). The material-side half became **RE-A9**.
- **RE-A2 — labelled-sky probe in the original game. Done** (captured 2026-07-26). The
  shipped engine draws all six faces on their predicted axes, upright and unmirrored, with
  five three-face corner agreements — including two on `dn`, which the seam probe cannot see
  at all. The set-up, prediction table, capture protocol and full result are above ("The
  in-game check"). K1 is closed on three independent legs.
- **RE-A3 — model lighting + worldlight runtime role. Done** (2026-07-26). `engine.dll` +
  `StudioRender.dll`. World surfaces render from lump 8; lump 15 is runtime data for the model
  light cache alone. A model's ambient term is a 6-face ambient cube built by a 162-ray
  radiosity gather that samples `dface_t.avgLightColor` and multiplies by material
  reflectivity, plus at most `r_worldlights` (2) direct worldlights. Closes K3 and K5, and the
  runtime half of the multi-`light_environment` question (first-wins). Full write-up above
  ("K3 / K5 …"); addresses, ConVar table and the consequences for our rig are there.
- **RE-A4 — day/night selection. Done** (2026-07-26). **There is no reader and no second
  bake.** `day[8]`/`night[8]` are `0x00` in all 4,538,720 bytes of each, over all 108 maps
  (against `styles[8]`'s `0xFF` sentinel + live indices in the same faces); lump 8 stores one
  bake per face, 1 luxel grid per style or 4 + an average colour when bumped, never the 2 or 8
  a pair would cost; and the FACES lump's three consumers in `engine.dll` — `Mod_LoadFaces`
  (`0x200b73d0`), the face-centroid builder (`0x200b9c30`) and `CMod_LoadDispInfo`
  (`0x20033b30`), enumerated by decoding the lump index at all 55 call sites of the lump
  accessor — read none of offsets 56–71. No day/night string, ConVar, worldspawn key or entity
  key exists; `m_daylight_level` is a client screen effect gated by `cl_obfuscate_daylight`.
  Closes K4 and removes the "night set, pending K4" caveat from D1/C4. Instrument:
  `tools/probe_daynight.py`. Full write-up above ("K4 …").
- **RE-A5 — VRAD skyambient semantics. Done** (2026-07-26). Data-only: VtMB ships no compiler,
  so the decompile fallback the task named does not exist. Recovered VRAD's photometric
  transfer exactly — `(colour/255)^2.2 · (brightness/255) · (const + 100·linear +
  10000·quadratic)`, zero exceptions on 16,378 lights over all 108 maps — and with it **lump 8's
  absolute scale**, `stored luxel = 255 · intensity / falloff`, measured off the sun. Confirmed
  the sun is baked at `255 · intensity · cos` behind a sky-visibility test, by magnitude (×1.01
  on `sp_endsequences_b`) and by chromaticity (d = 0.019 to the sun, 0.523 to that map's fill).
  Where sky is visible the pair is a **first-class term**: the sun's ceiling is a median 332% of
  the map's median lit face, the skyambient's 121%. Settled the multi-`light_environment`
  bake rule on `sp_observatory_2` (ambient global, first-entity-wins; sun per-entity), and
  established the retail-vs-patch bake provenance split that any lump-8 work needs. The
  hemisphere weighting is left **bounded, not exact**, and the write-up says why that is the
  right call. Closes K6; corrects this plan's own target map (`sp_tutorial_1` is
  patch-recompiled and has no retail sky pair) and one RE-A7 observation. Full write-up above
  ("K6 …"); instrument `tools/probe_skyambient.py`.
- **RE-A6 — reference capture set.** From the user's install: screenshots at the `shots.bat`
  vantage equivalents on 3–4 sky maps (`sp_tutorial_1`, `sm_hub_1`, `ch_temple_1`,
  `sm_oceanhouse_1`), plus one sky-only view per skyname. The visual target for Phase B/C
  calibration. RE-A9 narrows what the captures are for: the sky's own transfer is the
  identity, so they serve the **world** half of the sky-to-scene ratio (what
  `albedo × lightmap × 2` looks like next to a sky texel, for B4) and the general look
  reference — not a sky-brightness measurement. Before any capture is used
  *quantitatively*, settle the K7 loose end: whether `snapshot` grabs pre- or post-ramp —
  the display gamma (1.35 at default `cl_v_gamma`) is a device LUT a back-buffer grab would
  omit, which would make captures comparable to our `shots.bat` PNGs but not to what a
  player's monitor showed. (Owner-run.)
- **RE-A7 — full-game inventory scan. Done** (2026-07-26). All 108 BSPs, data only:
  `skyname` + face resolution, `light_environment` rows, worldlight type histogram, both fog
  sets, `toolsskybox` faces, and the RE-A8 area columns (sky `area`, its faces, and its
  prop/entity/worldlight population). Closes K8, and corrects two rows of the RE-A8 content
  table that had been PVS-classified. What it changes: C2's population is 83 maps, not an
  `sm_hub_1` special case; B7 is ~6× the sample's size (1,442 sky worldlights game-wide);
  B8 gains a second failure direction (65 maps export no fog at all, 12 of them against an
  enabled `worldspawn`); D6 is observable on exactly one map (`sp_observatory_2`), and
  first-wins resolves over lump 15, not the entity lump. Instrument:
  `tools/probe_sky_inventory.py`. Full write-up above ("The full-game inventory").
- **RE-A8 — the 3D-skybox pass. Done** (2026-07-26). `client.dll` + `vampire.dll`: the pass is
  a second render of one BSP area through the ordinary world + renderable path, the membership
  rule is `dleaf_t.area`, the placement transform is `world(v) = scale·(v − origin)` with
  `scale` an integer field, and `sky_camera` fog is the skybox pass's own. Full write-up above
  ("The 3D skybox — what the pass actually draws"). Feeds B7 and B8.
- **RE-A9 — the sky's brightness chain. Done** (2026-07-26). `stdshader_dx8.dll` +
  `engine.dll` + `MaterialSystem.dll`, plus the shader assembly VtMB ships as data. **The
  transfer is the identity.** There is no `SkyBox` shader: all 79 sky VMTs are `UnlitGeneric`
  with `$basetexture` (+ `$nofog` on all 66 faces any map actually names), the `UnlitGeneric`
  shader is an alias whose fallback is the literal `"UnlitGeneric_DX8"`, `R_DrawSkyBox` pushes
  a (1,1,1) colour modulation into the material before binding it, that reaches the pixel
  shader as `c38 → oD0 → v0`, and `unlitgeneric.psh` is `tex t0; mul r0, t0, v0` — one
  multiply by white. No overbright on the unlit path, against `lightmappedgeneric.psh`'s
  `mul_x2 … (overbrightFactor/2)` with the factor pinned to 2 by `0x200718d0`, which is the
  single sky-vs-world asymmetry. Gamma (`gamma`/`texgamma` 2.2, `brightness` 0,
  `linearFrameBuffer` 0, the `1.6 − clamp(cl_v_gamma − 1, 0, 3)·0.5` display value) is
  frame-wide, never per-material. `$nofog` is flag bit 14 and maps to `IShaderShadow::FogMode(0)`
  in all five of `CBaseShader`'s fog helpers, so the backdrop is unfogged game-wide while the
  miniature is fogged by `sky_camera`. Closes K7; turns `Brightness 4` from an uncalibrated
  constant into a divergence needing a decision, and gives B8 a hard rule. Full write-up above
  ("K7 …").

## Phase B — sky rendering rework (K1 and K2 both settled — nothing blocks it)

- **B1 — labelled-cube probe in our runtime. Done** (2026-07-26). `elysium.SkyProbe 1` builds
  the cube from the RE-A2 labelled faces (`tools/out/_skyprobe/<skyname><suf>.png`) instead of
  the map's own, so both ends of the orientation chain are checked against **one** set of
  images — the same six the shipped VtMB engine drew. Captured on `sp_tutorial_1` (`la`) with
  `ShowFlag.StaticMeshes 0`, which leaves only the backdrop (a PMC) drawing, so all six axes
  are unoccluded from any standing position.

  **Every face reads upright, unmirrored and on its predicted axis**, and each face's own edge
  tags name the face actually adjoining it:

  | Look (Unreal) | Face | Its label | Screen-left / right |
  |---|---|---|---|
  | +X (yaw 0) | `RT` | `la +X`, look yaw 0 | `bk` / `ft` |
  | +Y (yaw 90) | `FT` | `la −Y`, look yaw 270 | `rt` / `lf` |
  | −X (yaw 180) | `LF` | `la −X`, look yaw 180 | `ft` / `bk` |
  | −Y (yaw 270) | `BK` | `la +Y`, look yaw 90 | `lf` / `rt` |
  | +Z (pitch +90) | `UP` | `la +Z`, look pitch −90 | `bk` / `ft`, `rt` toward the faced horizon |
  | −Z (pitch −90) | `DN` | `la −Z`, look pitch +90 | `bk` / `ft`, `rt` toward the faced horizon |

  The Source-axis labels come back **Y-negated** against the Unreal heading (`+Y` reads at
  Unreal −Y), which is `source_to_unreal` showing up in the picture. The faces are *not*
  mirrored, because the reflection is exactly cancelled by the handedness change: looking down
  +X, Source screen-right is −Y and Unreal screen-right is +Y, and those are the same
  direction.

  So there is no residual, and B1 covers what the `Elysium.Substrate.SkyCube` test cannot —
  the bulk-data layout (face-major, slice order, rows top-down) and `M_Sky`'s sampling vector.
  Instrument note: `elysium_player_teleport` grew a `pitch` argument for this, since the two
  pole captures need one.
- **B2 — canonical face orientation at export. Done** (2026-07-26). The `UE_` contract now
  covers sky, as a recorded contract rather than a transform: the decoded faces already *are*
  the canonical orientation, so the exporter emits them verbatim and states the convention it
  emitted them under — **`skyconv 1`** in `<map>.env`, against
  `ElysiumEnvironment::SkyConventionVersion`, which `ApplyEnvironment` warns on a mismatch of.
  `sky_upscale.py`'s seam-solver is retired for the constant `bk, rt, ft, lf`, unflipped; the
  seam error it used to minimise survives as a one-line **read** on the input faces
  (`ring_seam_err`), printed before the upscale so faces that are not what the contract says
  are caught up front rather than silently stitched.
- **B3 — correct cube assembly. Done** (2026-07-26). `BuildSkyCube` packs the canonical faces
  into UE slices under one table carrying both halves of the transform — the binding
  **`rt, lf, ft, bk, up, dn`** for `+X,−X,+Y,−Y,+Z,−Z` *and* each slice's rotation, applied by
  an index remap at blit time (`RotSource`/`BlitRotated`). Both errors are gone together; the
  table is documented face by face in the code with its derivation.
  The transform is **re-derived, not copied**: solving K1's six face directions against K2's
  six slice directions yields exactly one `(face, rotation)` pair per slice, matching the table
  below with no residual. That derivation is now a standing test —
  **`Elysium.Substrate.SkyCube`** re-solves it in C++ over a 7×7 texel grid per slice
  (`SkySliceFace`/`SkySliceSource` expose the two halves), so a wrong binding, a wrong rotation
  or a mirror each fail the suite rather than the eye. In Unreal space a face pixel `(u, v)`
  looks at

  ```
  rt: ( 1,  s,  t)    lf: (-1, -s,  t)    bk: ( s, -1,  t)
  ft: (-s,  1,  t)    up: (-t,  s,  1)    dn: ( t,  s, -1)
  ```

  (`s = 2u − 1`, `t = 1 − 2v`). And every slice takes the rotation UE's D3D-derived layout
  requires (K2): **`rt` 90° CCW, `lf` 90° CW, `ft` 180°, `bk` none, `up` 90° CCW, `dn` 90° CCW**
  — a renamed array alone still draws wrong. B1 is the acceptance check.
- **B4 — backdrop verification + brightness calibration. Done** (2026-07-26). `M_Sky` sampling
  is verified by B1, and the hand `Brightness 4` is gone: the shipped default is **parity**
  (D7), with the multiplier surviving as **`elysium.SkyBrightness`**, a live debug cvar that
  re-applies to the built backdrop on change.

  The remaining question — what our side of the seam does to a texel — is now **measured**
  rather than argued. Aim the camera down Unreal +X so one face fills the view, map every
  screen pixel back to the texel it shows, and compare displayed 8-bit sRGB against source
  8-bit sRGB (which is exactly what VtMB wrote to its framebuffer, RE-A9). Auto-exposure is
  off (`r.DefaultFeature.AutoExposure=False`), bloom and fog disabled for the measurement, so
  what is left between the two is the **tonemapper**.

  One pass would only sample the sky's own value range, and VtMB's skies are night skies — so
  the multiplier is *swept* instead, and each sweep re-expressed as the effective source texel
  it is equivalent to (it scales the linear value the emissive gets, so texel `v` at multiplier
  `k` is the input that texel `sRGB(k·linear(v))` would be at 1). Stitched, that covers the
  whole range on real texels — 545,300 samples over ×0.25…×16 of `la` on `sp_tutorial_1`:

  | Effective source | 3 | 6 | 12 | 20 | 27 | 39 | 55 | 77 | 107 | 138 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | Displayed | 0.3 | 1.3 | 4.7 | 10.3 | 17.3 | 30.3 | 52.3 | 85.3 | 132 | 169 |
  | ratio | ×0.11 | ×0.23 | ×0.39 | ×0.52 | ×0.63 | ×0.79 | **×0.95** | ×1.11 | ×1.24 | ×1.22 |

  **The deviation from parity is a curve, not a gain**, so no multiplier can restore it. The
  filmic toe crushes everything under ~40 (by ×9 at the bottom), unity crosses at **≈ 55**, and
  above that the shoulder lifts ~20%. And the decoded skies sit almost entirely below that
  crossing — `la`'s `rt` face is p50 **0** / p90 **20**, `pier`'s p50 **0** / p90 **43** — so
  **the whole sky lives in the toe**. That is what the old `4` was reaching for; but 4 does not
  undo a toe, it just moves the sky up the same curve, landing ~×2.3 *above* parity across
  `pier`'s working range. The residual is the tone curve's, and it belongs to **roadmap 3.6/3.7**
  (pinned exposure + neutralised tone curve), not to the backdrop multiplier — which is what D7
  says.

  Found while measuring, and handed to B8: **our height fog inscatters into the backdrop.**
  With `sm_hub_1`'s fog on, the sky's displayed mean goes 16.6 → 29.1. RE-A9 is categorical
  that the 2D backdrop is never fogged, on any map, at any distance.

  `SkyLight` IBL was re-checked after the B3 fix — a correctly oriented cube changes the
  directional distribution the capture integrates, and C1/C2 take the intensity over from
  there.
- **B5 — upscaled faces as the enhancement A/B. Done** (2026-07-26). **`elysium.EnhancedTextures`**
  (off by default, the family toggle `docs/asset-enhancement.md` names) makes the runtime prefer
  `tex_hi/sky_*.png`; the faithful default stays the decoded originals (512², or 256² for
  `holly` and `chinatown`). The preference is per-map and **all-or-nothing**: `HasSkyFaces`
  tests all six before switching, so a map with no enhanced set — or a partial one — keeps its
  faithful faces instead of losing its sky. The map-load line names which set was used
  (`faithful` / `enhanced` / `labelled probe`), so an A/B is never ambiguous. Verified on
  `sp_tutorial_1` across all three cases: absent, complete, and 5-of-6.

  RE-A9's constraint is now an **acceptance gate, not a note**. Because the original's transfer
  is the identity, an upscaled face has to preserve **absolute** texel values, not just
  structure — a model that shifts the mean shifts the sky's brightness one-for-one, and one
  that reshapes the histogram changes its contrast. `sky_upscale.py` measures both per face
  against its source (per-channel mean drift, and the worst gap over the 1/5/10/25/50/75/90/95/99
  percentiles, all in 0–255 texel units) and **writes nothing** if any face exceeds
  `--max-mean-shift` (1.0) or `--max-hist-shift` (6.0). `--allow-drift` keeps them anyway, still
  reported. A resolution change may not smuggle in a grade.
- **B6 — regression baselines. Done** (2026-07-26). Two pieces were missing, not one: the
  harness had no vantage that *frames* sky, and no way to compare two runs.

  **Vantages** — the existing ones frame walls, so a sky change barely moves their pixels.
  `t1sky` (`sp_tutorial_1`) and `h1sky` (`sm_hub_1`) pitch up from the same two points to put
  the backdrop **and** the 3D-skybox miniature in one frame — the two things a sky regression
  breaks. Both maps draw sky (`la`, `pier`) and both run the miniature pass.

  **`tools/shots_diff.py`** — `--save` promotes a run to `out/_shots/_baseline/<map>/`, and a
  later run diffs against it per vantage: mean and p99 absolute difference, the percentage of
  pixels moved by more than `--tol` levels, and a heat map written for any vantage over
  `--max-changed`. Non-zero exit when any does, so it can gate a change rather than just report
  on one. Baselines live under `out/`, so they are game-derived and gitignored like every
  capture — a local instrument, not a committed fixture.

  Verified both directions: a run against itself is 0.00% on all 10 vantages, and a +6-level
  lift over the sky band of one shot is caught at 27.78% of that vantage's pixels with the other
  three unmoved.

  **One hard limit, measured: a baseline is only valid against a fixed bake.** Re-baking a map
  from **byte-identical inputs** and re-shooting moves the render by up to **5.4 mean / 55% of
  pixels** (`sp_tutorial_1` `t1`; `spawn` 1.0/20%, `t3` 1.2/21%, `t4` 0.02/0.05%). The bake
  builds Nanite meshes and Lumen's surface cards, card packing is order- and DDC-dependent, and
  on a map where direct light explains ~0% of a median lit face (C4) the bounce carries almost
  everything — so a reshuffled surface cache moves the whole frame a little. By contrast a map
  left un-rebaked is stable to **≤ 0.23 mean / ~1% of pixels** across a runtime change
  (`sm_hub_1`), which is the floor of Lumen's own temporal accumulation.

  **The same limit applies to regenerating the master materials, and B8b pinned the numbers
  down.** Three runs over one fixed bake of `sp_tutorial_1`, a map that authors no fog at all, so
  B8b's term is inert on it by data:

  | comparison | `spawn` | `t1` | `t3` | `pw` | `t2` / `t1sky` / `t4` |
  |---|---|---|---|---|---|
  | same assets, run twice | — | — | — | 0.00 | — |
  | with the fog term vs without | 14.75 | 6.60 | 1.68 | **0.00** | 0.40 / 0.37 / 0.03 |
  | rebuilt **byte-identical** materials | 10.15 | 1.82 | 3.74 | **0.00** | 0.14 / 0.20 / 0.04 |

  Read the last two rows together: authoring a *different* graph and authoring the *same* graph
  again move the render by the same order, so the movement is the toolchain's, not the change's.
  Every generator deletes and recreates its asset, which forces a full shader recompile and a
  Lumen surface-cache recapture, and that recapture is as order-dependent as the bake's card
  packing. `pw` is the control that makes this legible: it is bit-exact across all three
  comparisons, so the render itself is deterministic for a fixed set of assets — it is *producing
  the assets* that re-rolls, and it re-rolls hardest on the vantages whose frame is
  bounce-dominated.

  This also closes an item left open on 2026-07-26: the unexplained `sp_tutorial_1` `spawn`
  residual of 32.46 recorded against a re-bake was not a defect in anything. That vantage's Lumen
  solution is simply the least stable in the set, across bakes and asset rebuilds alike.

  So the tool answers "did this runtime change alter the look" — which is what it did cleanly for
  C2 — and it does **not** answer "did anything change across a re-bake or a content rebuild": at
  that scale its own noise swamps the signal. Re-baseline after either, attribute across one only
  with a margin well above the numbers above, and for a small effect compare **two runs over one
  fixed set of assets** (a cvar A/B, as B8b used for the fog) rather than against a baseline.
- **B7 — split the whole 3D skybox, not just its world faces. Done** (2026-07-26; RE-A8).
  The exporter's `SkyScope` computes the miniature's BSP area once — the engine's own
  membership rule, `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))` — and applies
  **one test to every content class**: world faces, static props, `env_sprite`s, entities
  (point *and* brush, a brush entity classifying by its model bbox centre plus its `origin`),
  worldlights, and the collision brushes. Each carries a sky flag into its sidecar: an 11th
  `.props` field, a 16th `.lights` field, a 12th `.sprites` field, `"sky": true` in `.ents`.
  The PVS classifier is retired (it is set up from a player-derived viewpoint and missed up to
  31 faces per map).

  **Two `sky_camera`s** — `la_malkavian_4` alone — resolve **first by entity-lump order**, the
  engine's own `FindEntityByName(NULL, …)` first-match rule, the same one the rope chains and
  VRAD's sky-ambient resolution follow. The exporter says so when it sees more than one.

  Placement is the one transform `_sky.obj` already took, now applied to the whole set:
  `world(v) = scale · (v − origin)`. The **bake** places sky props and sky lights under it
  (uniform actor scale, never solid, no shadow, out of the ray-tracing scene), and the
  **runtime** carries sky-scope entities through it in the `.ents` parser — origin and hulls
  together, once — so every downstream consumer (brush bodies, prop and NPC bodies, gizmos, the
  click-pick) is placed correctly without knowing the miniature exists. What the point transform
  cannot express stays on the `bSky` bit: a body's uniform mesh scale, and that miniature
  geometry is scenery the player can never touch. Animating sky entities stay live — the
  `func_rotating` ferris wheel turns, the `logic_timer`-driven window glows blink.

  Sky-area **worldlights** come out of the world rig unconditionally and are re-placed inside
  the transform — scaled position, reach × `scale`, floored at `MinSkyReachCm` so a degenerate
  authored radius does not scale to nothing. Deleting them would un-light authored content:
  VtMB's light cache lit the miniature's props from exactly those lump-15 rows (RE-A3). It also
  takes them out of the fill-vs-fixture sample in `docs/light-attribution.md`, which had been
  measuring lights placed at miniature coordinates as if they lit the map.

  Sky-area **collision brushes are dropped** (190 of `sp_tutorial_1`'s 2,561): a hull would
  collide at the raw miniature coordinates it was authored at, while the geometry is drawn
  16× away — an invisible wall standing where nothing is drawn.

  Verified against RE-A7/RE-A8's independently measured numbers: `sp_tutorial_1` area 2 /
  1,680 faces / 30 props / 59 entities / 58 lights, `sm_hub_1` area 4 / 589 / 49 / 123 / 60 /
  54 sprites, and the whole-game rollup still 1,043 props, 2,275 entities, 1,442 worldlights
  over 43 maps at `scale` 16. In-engine, the LA skyline stands behind the tutorial alley and
  the floating debris is gone.

  One consequence of porting the pass as real geometry rather than a second render: VtMB's
  miniature draws into a cleared depth buffer and so is **always** behind everything, whatever
  its size. Ours shares one depth buffer, so the miniature can occlude. It does not in practice
  because the sky area is authored as a sealed shell around its own camera, which scales into a
  shell around the map — but it is a property of the authoring, not a guarantee of the port.
- **B8 — fog: world vs skybox. Mostly done** (2026-07-26; RE-A8/RE-A9). `<map>.env` now carries
  **both** sets, from their real owners: `fog*` off **`worldspawn`** (the world's), and
  `skyfog*` off **`sky_camera`** (the 3D-skybox pass's own), the latter with its distances
  already ×`scale` into world units — the pass renders at 1/scale, so a skybox-space distance is
  `scale` times as far in the world. Both are read by `FElysiumEnvDef`.

  Measured over all 108 maps, sourcing the world's fog from `worldspawn` changes three things:
  **12 maps regain fog** they authored and never got (`ch_temple_4`, `hw_ash_sewer_1`,
  `hw_sinbin_1`, `la_chantry_1`, `la_crackhouse_1`, `la_empire_1`, `sm_bailbonds_1`,
  `sm_pawnshop_2`, `sm_smoke_1`, `sm_tattoo`, `sm_warehouse_1`, `sp_genesisdevice_1`), **6 stop
  being fogged** against a `worldspawn` that never asked (`la_parkinggarage_1`,
  `sm_oceanhouse_1`, `sp_endsequences_a`, `sp_endsequences_b`, `sp_soc_2`, `sp_theatre`), and
  of the 43 maps carrying both, **27** were using the wrong numbers. *(This corrects two RE-A7
  counts: 6 wrongly-fogged maps, not 5; and 27 disagreeing sets on the render-relevant fields —
  30 if `fogcolor2`/`fogdir` are counted, which nothing we ship reads.)*

  **The backdrop is now exempt, game-wide.** RE-A9's rule is categorical — every sky face
  carries `$nofog 1` → `FogMode(0)` — but our backdrop is ordinary opaque geometry and Unreal's
  deferred fog pass fogs by depth alone, so the world's fog was inscattering straight into the
  sky. The fix is `FogCutoffDistance`, set from the backdrop box's own half-extent so the two
  cannot drift apart (Epic documents that knob for exactly this). Measured on `sm_hub_1`: the
  sky's displayed mean was 16.6 unfogged against 29.1 fogged; it is now **16.5 either way**,
  while the world keeps its fog.

  Both sets reach the render in **B8b**, below, which is where the scoping is actually done.
- **B8b — the miniature's own fog, as a per-primitive term. Done** (2026-07-26; D4 amended,
  `decisions.md`). Source fogs the world and the miniature with two different linear fogs and can
  scope them trivially, because the miniature is a separate pass with its own fog push/pop. Ours
  is one scene, and the two **share screen depth** — measured over the exported set, the placed
  miniature's bounds sit **0–4,868 cm** from the world's own against world diagonals of
  **11,124–40,334 cm**, and on **6 of the 8** maps with a miniature the miniature's geometry lies
  *inside* the world's bounding box (`sm_hub_1`, `sm_pawnshop_1` and `sp_tutorial_1` at distance
  0). Only `sp_observatory_1` and `sp_soc_1` separate at all. So `FogCutoffDistance`, a
  `LocalFogVolume` and a second fog actor are all ruled out by data, not by argument, and a
  deferred fog pass offers nothing else.

  **So the distance fog moved into the material, per primitive.** Custom Primitive Data carries
  one fog set per primitive — colour, start, `1/(end − start)` — and the term is Source's own
  `f = saturate((PixelDepth − start) · invRange)`, so it reproduces the original fog rather than
  approximating it. The bake stamps every world / prop / miniature component (so the level is
  right when opened in the editor) and `AElysiumMapActor::ApplySceneFog` re-derives it from
  `<map>.env` at load, the same way the rig re-derives every light. `elysium.Fog` A/Bs it live.

  It is applied as `BaseColor ×= (1−f)`, `Specular = 0.5 · (1−f)`, `Emissive = Emissive·(1−f) +
  colour·f`, which is exactly `lerp(shaded, fog, f)` for a deferred surface — the specular term
  is there because scaling BaseColor alone leaves a Lumen reflection shining through the fog at
  full strength. **Neutral by construction:** an unwritten custom-data slot reads as zero, zero
  is `invRange`, and `f = 0` passes every output through unchanged — so "not fogged" and "never
  written" are the same state, with no branch to get wrong. `Elysium.Substrate.FogPack` guards
  that property.

  Two things it does not cover, both bounded: a `UDecalComponent` is a `USceneComponent` and
  carries no custom primitive data, so a decal takes the world's set from named parameters the
  bake binds into its instance instead (a decal is only ever a world surface, so it needs no
  per-primitive scoping — but `elysium.Fog` does not reach it, and a map with world fog needs a
  re-bake, not just a reload, for its decals to follow); and the 2D backdrop is exempt game-wide,
  as RE-A9 requires.

  **The height fog is no longer the map's distance fog.** What is left to it is the volumetric
  layer — participating media the map's hundreds of dynamic lights shaft through, which is the
  modernization D4 sanctioned and which no per-surface term can produce. Its analytic
  contribution is now a residue rather than a design, and a small one: **the engine divides both
  `FogDensity` and `FogHeightFalloff` by 1000** (`FExponentialHeightFogSceneInfo`, `SceneCore.cpp`),
  so `3/end` integrates to **under 0.2% across a whole map**, and the backdrop is cut off before
  it regardless. *(That /1000 also corrects this plan: `fog_height_falloff = 0.02` does not put
  the fog "below z ≈ 2 m" — it is 2 × 10⁻⁵ per cm, which halves the density every ~500 m and is
  therefore effectively uniform over a VtMB map. The real defect was never the height profile; it
  was that the density was ~1000× too thin for the world, which is why the only place it ever
  showed was the 5 km backdrop.)* Calibrating the volumetric layer for its own sake is open — at
  this density it, too, is near-invisible.

  **The fog colour is decoded, not used raw.** `.env` transports the authored value verbatim
  (`/255`); the consumers raise it to 2.2, because VtMB's colours are gamma-encoded and its own
  math decodes them that way (RE-A5's `(colour/255)^2.2 · …`), and because that is what every
  other authored colour in this pipeline becomes. The magnitudes settle it: C1 measures a map's
  own sky radiance at 0.0034–0.0066 and C4 puts a typical lit surface near there, so `sm_hub_1`'s
  authored `17 20 25` would be **0.067 undecoded — three to thirteen times brighter than the
  world it hangs in** — against **0.0021 decoded**, a dark haze just under the walls. What this
  does not close is the display transfer: a linear value still meets the filmic toe B4 measured
  at up to ×9, so a saturated fog displays under its authored level. That is one named
  calibration for the whole render (D7, roadmap 3.6/3.7), not a per-term fudge, and nothing here
  compensates for it.

  Measured on `sm_hub_1` (`worldspawn` 500→5000, `sky_camera` 500→5000 ×16 = 20,320→203,200 cm),
  fog on against fog off over the same assets: mean **0.23–0.76**, p99 5–12, 5–19% of pixels. The
  `spawn` vantage barely moves (0.33) because it stands inside `fogstart`, which is the authored
  behaviour. On `sp_tutorial_1`, which authors no fog, the term is inert.

## Phase C — ambience rework (K3/K4/K5/K6 settled — no RE blocks it)

- **C0 — apply the RE-A3/RE-A5 corrections to the pipeline. Done** (2026-07-26). Four
  corrections, none of them a divergence:

  **(a) first-wins on the type-5 skyambient**, in both consumers (`bake_map.py` and
  `UElysiumLightRig::Adopt`), by **lump-15 order** — which `.lights` preserves — not entity
  order. Both assigned unconditionally in the loop, i.e. silently *last*-wins. D6 needs no
  decision: first-wins matches the runtime (RE-A3) and the bake (RE-A5) alike, and since VRAD
  stamps one globally-resolved value on every type-5 row, it changes which row is read, not
  what is read.

  **(b) the engine's load-time fixups**, in `bsp.read_worldlights`, so anything fitted against
  lump 15 fits the values the engine actually lit with: zero-attenuation point/spot →
  `quadratic = 1`, zero-exponent spot → `exponent = 1`, `radius < 1` → `radius = 0` (*no*
  cutoff, not a tiny one). Measured game-wide, this moves **7 lights of 19,197** — 6 spot
  exponents and 1 radius, and not one attenuation case. A correctness fix that changes almost
  nothing, which is worth knowing: the weight of C0 is in (d), not here. `raw_values=True`
  opts out, for `probe_skyambient.py`, which measures what VRAD *wrote* and whose transfer law
  is checked against the very attenuations these fixups rewrite.

  **(c) material reflectivity** — `vtex`'s own average albedo, 3 floats at +32 from the `.tth`'s
  `VTF\0` marker — decoded alongside every texture and written to the `.mtl` as
  `reflectivity r g b`. It is the missing input for any reproduction of the bounce gather: the
  light cache multiplies every one of its 162 rays by the reflectivity of the material it hit
  (RE-A3). Verified by construction: over `sp_tutorial_1`'s 412 materials the decoded value
  correlates with the mean **linear** albedo of the decoded texture at **1.0000** (0.9597
  against the gamma-encoded mean — which is how we know it is a linear average). Range 0.000
  (`effects/black`) to 0.514 (`glass/brbwndwa`), median 0.117. Nothing in the render path reads
  it; it is data for C4.

  **(d) de-normalisation before fitting.** `dworldlight_t.intensity` is VRAD's radiance
  *divided* by the light's own falloff denominator at d = 100 units, so it is not comparable
  across lights until that is multiplied back. `read_worldlights` now returns `falloff` beside
  it and `probe_light_calibration.py` applies it. It matters: measured over all 108 maps the
  denominator takes **13 distinct values from 1 to 80,000** — 10,000 on 16,292 of the 16,702
  point/spot lights, and something else on the other 410, so those were being fitted up to
  10,000× off. Types 0/3/5 carry no denominator at all (attn is `(0,0,0)` on every texlight,
  sun and skyambient in the game — a texlight's intensity comes from its material's emission,
  a `light_environment` has no attenuation keys), so `falloff` reports **1** for them rather
  than 0; that keeps `intensity × falloff` correct everywhere without each consumer having to
  remember which types are which. (b) and (d) land together because (b) rewrites the very
  attenuations (d) is built from.
- **C1 — skyambient with magnitude. Done** (2026-07-26). Bake and rig both kept only the
  type-5 *normalized colour* and threw the magnitude away, against a flat
  `SKYLIGHT_INTENSITY = 1.0`. The magnitude now travels: `UElysiumLightRig::SkyAmbientMag`
  beside `SkyAmbient`, and the bake writes it onto the SkyLight actor so the editor carries the
  map's real data rather than a placeholder.

  RE-A5 makes the derivation need no calibration — a type-5 intensity is a lump-8 luxel value
  ÷ 255 already, because VRAD divides no falloff out of a `light_environment`. What it needs is
  a *bridge*, because VtMB's sky is one number and ours is an image. The bridge is one line:
  **scale the cube so its own average radiance is that number.** `BuildSkyCubeFrom` returns the
  cube's solid-angle-weighted mean linear radiance over the **upper** hemisphere (the part that
  lights, since the SkyLight runs `bLowerHemisphereIsBlack`), and the intensity is
  `magnitude / that`. The modernization — a real IBL with real occlusion — sits entirely in the
  *distribution*; the *level* stays VtMB's own. No free gain, which is what leaves C4 a
  measurement rather than a fit.

  Measured on `sp_tutorial_1`: the `la` cube integrates to 0.00335 and the map authors 0.00656,
  giving intensity **1.955** — the sky delivers about twice what the decoded faces alone would.
  That the two are the same order of magnitude is the first evidence the units line up at all.

  The magnitude also forced a sidecar fix: `.lights` wrote intensity at **three** decimals, so
  `sp_tutorial_1`'s 0.006558 came back as 0.007 — a 6.7% error on the map's entire ambient
  level, and worse on the maps authored near 0.005. It writes six now.

  Range and zeros are as RE-A7 states, re-measured: 25 pair maps, type-5 magnitude 0.00500 to
  0.09804 (1.28 to 25.00 stored-luxel units), authored to exactly **zero** on `hw_chinese_1` and
  `sp_observatory_1`, with `hw_cemetery_1` zeroing the sun instead. Zero is honoured as a
  reading, not defaulted away.
- **C2 — interior SkyLight policy. Done** (2026-07-26; D2). One function,
  `AElysiumMapActor::SkyAmbientIntensity`, is the whole policy, and it has three cases and no
  fallback: **no pair → 0**, **pair authoring zero → 0**, **pair → C1's cube-scaled magnitude**.
  It runs on every map including the ones with no `.env` and no sky faces at all, because the
  case it exists to remove — the bake's placeholder constant-fill SkyLight — was precisely what
  a map with no sky kept.

  "Shows sky" and "is lit by sky" are now genuinely independent, as RE-A7 requires: the flag
  that draws a backdrop is `.env`'s `skybox`, and the level comes from the rig's type-5 row.
  41 maps draw sky and are lit by none of it.

  Verified on `sm_hub_1` — the doc's own example, an outdoor night street with **no**
  `light_environment`: `skyambient 0.00000 -> SkyLight intensity 0.000`. The B6 harness measures
  what that changed: 3 of its 4 vantages moved (h2 most, 3.20% of pixels, p99 12 levels) and the
  enclosed `spawn` vantage barely at all (0.05%). The change is real but small — the old flat
  1.0 against a near-black `pier` cube was contributing little to begin with, which is itself
  the point: this replaces a term that was arbitrary, not one that was load-bearing.

- **C3 — Lumen art-direction knobs via a per-map PostProcessVolume. Done** (2026-07-26; D3).
  The bake places one tagged, **unbound** PPV per map (`elysium.ppv`, beside the SkyLight and
  Fog it pattern-matches) and `AdoptBakedLevel` adopts it. The runtime owns how it *applies* —
  enabled, unbound, full blend weight — the same way it owns every light's values, because a
  bounded volume silently doing nothing outside its brush is indistinguishable from a knob that
  does not work.

  It ships **neutral**: not one `bOverride_` is set, so it changes no pixel. Three cvars reach
  it live, all on the convention **negative = neutral** (the override is *cleared*, not set to a
  nominal default, so "not touching this" and "set to what it would have been" stay
  distinguishable):

  | cvar | what it is | state |
  |---|---|---|
  | `elysium.SkylightLeaking` | the sanctioned replacement for VtMB's load-bearing author fill, where Lumen has nothing in the room to bounce off | **works** |
  | `elysium.SkylightLeakingDistance` | the ramp to full leaking | **works** |
  | `elysium.LumenDiffuseBoost` | `pow(albedo, boost)` on what the bounce sees — below 1 brightens | **unverified** |

  Measured on `sp_tutorial_1` (frame mean of 255): neutral 80.206 → leaking 0.25 **80.342** →
  leaking 1.0 **80.763**; adding a 200 m full-leak ramp drops it to **80.500** and a 1 m ramp
  raises it to **80.885** — monotone, and in the right direction for both (a longer ramp means
  less leak nearby). Clearing the overrides returns exactly to **80.193**. The mechanism is
  proven end to end.

  **A correction to D3's payload: Indirect Lighting Intensity cannot do the job it was named
  for, and is not wired.** It reaches the shaders as `View.PrecomputedIndirectLightingColorScale`
  — which scales *precomputed* indirect lighting — and **no shader under `Shaders/Private/Lumen/`
  reads it at all**. Our render path is fully dynamic with no precomputed lighting, so it is a
  no-op: measured, an `IndirectLightingIntensity` of 3 changed not one pixel. Lumen's own
  bounce-strength control is `LumenDiffuseColorBoost` (`LumenDiffuseColorBoost.ush`), which is
  what the third cvar drives instead — but it is carried as **unverified**, not as a working
  knob: it produced no measurable change here either, live or across a map load, and the reason
  was not chased. It has no cvar form in 5.8 and is consumed where the surface cache is written,
  so a live change plausibly cannot re-cache; that is a hypothesis, not a finding. C4/C5 have a
  proven knob to work with (leaking) and a lead on the second.

  **Ambient Cubemap stays banned**, as D3 says: a flat occlusion-ignoring term is the
  contrast-killer both Epic and the direction charter warn against.

- **C4 — calibration against the bake. Done** (2026-07-26). `probe_light_calibration.py` grows
  three things RE-A5 made possible.

  **A provenance gate.** The Unofficial Patch recompiles 20 maps and adds 7 with a later Source
  VRAD, so 27 of 108 bakes measure a different compiler. The probe now names which it is
  looking at and says plainly when the absolute half does not apply. (`sp_tutorial_1`, its own
  default map, is one of the patched ones.)

  **An absolute prediction, with no free gain.** Everything the probe did before was a
  *relative* fit with a fitted scale `a`. RE-A5 removed the need: a light's contribution to a
  luxel is determined — `255 · intensity / (const + linear·d + quadratic·d²)`, occlusion-traced,
  in the units lump 8 stores. So the direct term is **predicted**, and what is left over is not
  residual noise but a measurement of everything else in the bake.

  The result is the old R² ≈ 0 headline restated in a far stronger form. On both retail maps
  measured, **direct light does not explain the median lit face at all**:

  | map | baked median | predicted direct | residual median |
  |---|---|---|---|
  | `ch_fishmarket_1` | 9.76 | **0.00** (p90 17.06) | 7.18 |
  | `ch_temple_1` | 3.75 | **0.00** (p90 13.34) | 1.69 |

  The median lit face receives *no unoccluded direct light whatsoever* — 71% of face→light rays
  are blocked — and its brightness is entirely bounce. This is no longer "a direct model fits
  badly"; it is "the thing being modelled is not what lit the map."

  **A sky-aperture fit — which does not converge, and that is the finding.** RE-A5 left exactly
  one thing unpinned: the skyambient's hemisphere weighting, cosine vs uniform, a factor of ~2.
  The probe now regresses the residual on per-face sky visibility (traced with
  `probe_skyambient`'s own `SURF_SKY` brush tracer — the distinction matters, since a ray that
  merely leaves the map is *not* sky, and treating "not in solid" as sky reports 0% openness on
  every map). It gives **0.45× the uniform ceiling on `ch_fishmarket_1`** — which would be
  cosine weighting — and **−0.62× on `ch_temple_1`**, which is physically impossible.

  So the aperture is **not identifiable from this data**, and C4 confirms RE-A5's bound rather
  than closing it. The reason is the one RE-A5 named: on a mostly-enclosed map the faces that
  see sky are also the faces furthest from the author's fill, so sky visibility carries the
  fill's sign and not the sky's. Closing it needs a map open enough that the two decorrelate,
  or a fill-subtracted target — not a better regression.

  What the fit *does* give is the bounce floor in absolute units: **12.13** stored-luxel units
  on `ch_fishmarket_1` against a median lit face of 9.76, and **18.20** on `ch_temple_1` against
  3.75. The bounce is not a correction to the map's ambient level — it *is* the map's ambient
  level.

- **C5 — survey interplay. Done** (2026-07-26). The question was whether the `sm_hub_1`
  15-light disagreement would dissolve once sky-glow ambience was modelled correctly. **It does
  not, and the premise is now dead: there is no sky glow on that map to model.** `sm_hub_1`
  carries **no `light_environment`** — it is one of the 83 — so C2's data-driven policy puts its
  SkyLight at **zero**, where it previously sat at an arbitrary flat 1.0. Correcting the sky made
  the sky term *smaller*, so its fill is **more** load-bearing than before, not less. The
  hypothesis in `light-attribution.md` is strengthened, not resolved.

  B7 does not move it either, though it does clean the sample. `sm_hub_1`'s 60 sky-area
  worldlights are now out of the world rig — they were being measured as if they lit the map,
  which is exactly the confound RE-A8 flagged — but every one of them sits at X −3868 to −2497,
  the miniature's own corner, while the shortlist is the **eastern** strip at X > 7000 (206
  world lights). **None of the 60 is on the shortlist**, so the 15 disagreements survive B7
  untouched.

  C4 supplies the frame the adjudication needs: direct light explains ~0% of the median lit
  face, and the bounce floor *is* the map's ambient level. So the doc's own reading — fill is
  killable only where GI demonstrably replaces it — is the right one, and the fix is the second
  gate it proposes rather than a better fill detector. What has changed is that there is now a
  **sanctioned replacement to gate against**: C3's Skylight Leaking is landed and measured
  working, which is what D3 reserved for exactly this case. The next step is unchanged and
  unblocked — adjudicate the 15 by in-engine A/B — but its outcome now has somewhere to go.

## Decisions (all resolved — dated entries in `decisions.md`, 2026-07-26)

| # | Decision | Default per charter |
|---|---|---|
| D1 | The faithful-ambience target: lump 8 at the shared vantages, measured — not "looks right". **RE-A4 removed the fork:** there is one bake, keyed by `styles[8]`. **RE-A5 gave the target absolute units** (`stored luxel = 255 · intensity / falloff`), so "measured" now means in known units with no free gain — and the measurement must respect the retail-vs-patch provenance split | **Decided 2026-07-26** (`decisions.md`): reproduce — the measured target, in absolute units, retail bakes only |
| D2 | SkyLight-as-IBL is a modernization. **RE-A3 makes the divergence exact:** the sky lights the *world* only through lump 8, and lights *models* only as the colour of a sky-hitting bounce ray | **Decided 2026-07-26** (`decisions.md`): data-driven — the actor stays everywhere, intensity from the type-5 magnitude at the RE-A5 scale on the 25 pair maps, **zero** on the 83 without |
| D3 | Skylight Leaking / a bounce-strength knob — non-physical knobs replacing author fill; adjudicated like the light survey (does it serve the direction?). **C3 corrects the second one:** Indirect Lighting Intensity scales *precomputed* indirect only and no Lumen shader reads it, so it is a no-op on this render path; Lumen's own control is `LumenDiffuseColorBoost` | **Decided 2026-07-26** (`decisions.md`): mechanism lands now with neutral defaults; a non-neutral value only on a measured C4/C5 deficit, one dated entry per map |
| D4 | Fog model: exponential height + volumetric vs Source's planar distance fog (existing accepted divergence — formalise it). **B8b amends it:** the two fogs share screen depth (measured), so no engine-side mechanism can scope them and the exponential height fog cannot be the distance fog | **Decided 2026-07-26**, **amended 2026-07-26** (`decisions.md`): the RE scoping stands — world fog from `worldspawn`, `sky_camera` fog on the miniature only, the backdrop never fogged — but the distance fog is now Source's own linear model as a per-primitive material term, and the height fog keeps the volumetric layer alone |
| D5 | Enhanced (upscaled) sky faces as default-off A/B layer | **Decided 2026-07-26** (`decisions.md`): modernize behind the toggle, with B5's absolute-texel-preservation acceptance check |
| D6 | Multi-`light_environment` maps: sum vs first-wins (we currently do silent **last**-wins). **RE-A3 settled the runtime: first-wins, never summed**, for both halves of the pair. **RE-A5 settles the bake and shrinks the decision to nothing:** VRAD resolves the skyambient once, globally, first-entity-wins and writes that same value into every type-5 row, so the ambient half is moot by construction; the sun half is observable on `sp_observatory_2` alone (RE-A7), where summing vs first-wins moves the sun from 3.1% to 5.7% of the median lit face | **No decision needed** — adopt first-wins (C0a), which matches runtime and bake and changes no number |
| D7 | **Sky backdrop brightness.** RE-A9 makes the faithful transfer exact — a sky pixel is the decoded texel, unscaled and unfogged — so `M_Sky`'s `Brightness 4` is a divergence, not a pending calibration | **Decided 2026-07-26** (`decisions.md`): parity is the shipped default — B4 calibrates the displayed-parity value under our tonemapper — and the multiplier survives only as an `elysium.*` debug cvar; any night-sky lift goes through the D3 knobs, never the backdrop |

## Sequencing

**Phase A is finished except for the owner-run captures** (RE-A6), and **Phases B and C have
landed** — every task B1–B8b and C0–C5 is done. What is still open, named rather than quietly
dropped:

- **The volumetric fog layer is uncalibrated.** B8b took the distance fog off the
  `ExponentialHeightFog` actor and into a per-primitive material term, which leaves the actor
  owning only the volumetric haze and light shafts. At the density it still carries (`3/end`,
  which the engine divides by 1000 again) that layer is near-invisible, so the modernization D4
  sanctioned is present as a mechanism and not yet as a look. Calibrating it is its own task, and
  it can now be done freely: the distance fog no longer rides on the same number.
- **A decal does not follow `elysium.Fog`, and needs a re-bake rather than a reload.** Its fog is
  bound into its baked material instance because a `UDecalComponent` carries no custom primitive
  data. Correct per map, but not live-tunable with the rest.
- **The skyambient's hemisphere aperture** stays open, and C4 explains why it is not merely
  unmeasured but *unidentifiable* from this data: on an enclosed map the faces that see sky are
  the faces furthest from the author's fill, so sky visibility carries the fill's sign. RE-A5
  bounded it at a factor of ~2 and C4 confirms that bound rather than closing it.

What the two phases changed, in one line each: the backdrop draws correctly and at parity (B3,
B1, B4); the whole 3D skybox is real geometry rather than debris in the playable world (B7);
world fog comes from `worldspawn` and never touches the backdrop (B8); the world and the
miniature each take their own authored fog, per primitive, in Source's own linear model (B8b); a
sky regression is a number, with its noise floor measured (B6); the lump-15 corrections and the material reflectivity the bounce needs are in the
pipeline (C0); the sky light's level is the map's own authored radiance, zero on the 83 maps
that authored none (C1, C2); the art-direction knob that can replace load-bearing fill is landed
and measured (C3); and the bake is now measured in absolute units, which says direct light
explains ~0% of a median lit face and the bounce floor *is* the ambient level (C4, C5).

---

## P11 — Runtime spine

- [x] **11.0 Adopt the spine** *(landed 2026-07-26)* — the seven `runtime-architecture.md` §16
  owner calls recorded as one dated entry (`decisions.md` 2026-07-26 cont. 4): player as entity
  (A), box pawn on `APawn` (B), the sheet to `FElysiumCombatCharacter` + `FElysiumPlayerRecord`
  (C), the four-map New Game chain with the theatre owned by P12 (D), pause = engine pause +
  clock hold with the single-application dilation rule (E), the view-state seam with the input
  scope stack as sole input-mode authority over CommonUI's action router (F), boot out of the
  game mode (G). Adoption followed a four-way verification sweep (source state, VtMB-facts docs,
  design docs, tracker) that corrected the doc set in the same pass: think-first re-cited to RE2
  and `game_runtime.md` §7's stale open question retired; the movement-order claim demoted to
  inferred (new RE21); the chain's fourth map named (`sm_pawnshop_1`); `m_lifeState` removed
  from 4.9's latched surface; the player record reconciled with the save Player block (health
  stays a `Save`-flagged entity field; email flags + equipped handles added; owned RNG streams
  on the session record); the Play-tier beat test separated from the Substrate-tier digest
  compare; `debug-tooling.md`'s "three layers" corrected to four (0–3); "no HUD" corrected to
  "no vitals HUD"; `logic_choreographed_scene` given a real owner (P12); the `env_fade`-under-menu
  comment fixed to match the code (the quad is suppressed too). Both design docs flipped to
  adopted. Same day, `decisions.md` cont. 5 made the playable path (PP0–PP6) the master
  sequence, with P11 as its PP0 rung.

- [x] **11.1 Frame + clock ownership** *(landed 2026-07-26)* — `runtime-architecture.md` §3's tick
  table pinned in the engine's own tick graph (**S2**), and §4's one pause/time-scale facade over
  the clock **and** engine time (**S1**).

  **The frame.** `AElysiumMapActor` now registers **two** tick functions. The gameplay pass is
  `PrimaryActorTick` in `TG_PrePhysics` (steps 2–4: advance the clock, run the substrate think-first,
  then the audio/scheme pass); the post-move pass is `FElysiumPostMoveTickFunction` in
  `TG_PostPhysics` (step 7), a `USTRUCT` tick function on the same actor — the engine's own answer to
  work that straddles physics, where two actors would reintroduce the ordering question tick groups
  solve. The `+use` look cursor moved to the post-move pass: it traces, so before the pawn's move it
  picked against last frame's geometry, which reads as a door you cannot use until you stop walking.
  Order is **declared, not observed** — `AddTickPrerequisiteActor(PlayerController)` on the gameplay
  tick, the movement component prerequisite on that tick, and the post-move tick prerequisite on it.
  Both ends appear later than `BeginPlay` (no controller on a fresh world, no pawn at all on the menu
  backdrop), so `EnsureTickPrerequisites` re-checks each gameplay tick until each is bound and rebinds
  if the pawn is replaced. `dumpticks` on the running game reads the whole chain back: PC →
  `ElysiumMapActor[TickActor]` (TG_PrePhysics) → `CharMoveComp` → physics →
  `ElysiumMapActor[AElysiumMapActor::PostMoveTick]` (TG_PostPhysics).

  **The clock.** `FElysiumGameClock` made `Advance`/`SetPaused`/`SetScale`/`Reset` **private** and
  friends `FElysiumTimeControl` alone, so "one clock, advanced in one place" stopped being a
  convention and became a compile-time property; the subsystem exposes the clock read-only beside the
  facade. `Advance` no longer multiplies by scale: engine dilation has already scaled the tick's
  delta, and **scale is applied exactly once** (`decisions.md` 2026-07-26 cont. 4, call E). The facade
  sets both halves and reads the dilation back off `AWorldSettings` (which clamps it) before recording
  it, so the two can never disagree; `ApplyToWorld`, called from the map actor's `BeginPlay`,
  re-stamps pause and dilation after a travel, because both are per-world state and the clock is not.
  `StepFrames(N)` releases the world and the post-move pass counts the frames back down, so a step
  needs no paused-tick host. Verbs: `elysium.timescale`, `elysium.pause`, `elysium.step`.

  **The pause split.** `bTickEvenWhenPaused` is false on both gameplay passes and true on the
  presentation side — `AElysiumHUD` (so a held world still draws a live HUD and a menu can still take
  the dialogue box down) and `UElysiumEntityDebugSubsystem::IsTickableWhenPaused` (overlays, gizmos
  and I/O beams have to stay on screen through a pause and a frame step, which is exactly when they
  are read).

  *Acceptance.* Two new Substrate tests: `Elysium.Substrate.TimeControl` (a 0.25x scale still applies
  a 0.4 s delta whole — the single-application rule as an assertion — plus hold/step/resume/rewind
  semantics) and `Elysium.Substrate.FrameOrder` (the declared tick groups and the pause split off the
  class defaults, plus think-before-queue inside one world tick: a `logic_timer` due this frame has
  its wire delivered in that same frame). Live on `sp_tutorial_1`: over an identical
  sample-sleep-sample procedure the clock bought 11.33 s at 1x and 2.88 s at 0.25x (ratio 0.254 — one
  application, not two); `elysium.pause 1` froze `Now` across 9 wall seconds with three inputs left
  undelivered in the queue and the door unmoved; `elysium.step 1` bought one frame (8 ms) and
  `elysium.step 30` bought 0.25 s, each re-holding; releasing serviced the queued `Unlock`+`Open`,
  ran the mover to `AtTop` and scheduled its autoclose think. The HUD drew throughout the hold, and
  the `+use` cursor resolved `#66 tutchopdoorc(func_door_rotating)` from its new post-physics slot.

- [x] **11.2 World services** *(landed 2026-07-26)* — `runtime-architecture.md` §7's outbound seam:
  the substrate stopped reaching *up* into the engine.

  **The bundle.** `FElysiumWorldServices` (`Public/ElysiumWorldServices.h`) is four raw pointers —
  `IElysiumEmbodiment` / `IElysiumAudio` / `IElysiumTravel` / `IElysiumPresenter` — handed to
  `FElysiumEntityWorld` at construction. `AElysiumMapActor` implements the first three (multiple
  inheritance off `AActor`, the `UEngine : public UObject, public FExec` pattern) and builds the
  bundle in `LoadMap`, one line before the world. The `FElysiumSoundSchemeManager` is now constructed
  *before* the world rather than after, because a `start_enabled` `ambient_soundscheme` fades its
  scheme in from its own `Spawn()` and now reaches it through `IElysiumAudio`.

  **What moved.** Every `Cast<AElysiumMapActor>(World->GetOwnerActor())` is gone — 4 in
  `ElysiumNpcClasses.cpp`, 5 in `ElysiumPropClasses.cpp`, 1 in `ElysiumSoundScheme.cpp` — and so are
  `FElysiumEntityWorld::AudioSubsystem()`, `MapSubsystem()` and `GetPlayerPawn()`, which walked the
  owner actor to a GI subsystem or to `GetFirstPlayerController()`. `AActor* Owner` stays, but only as
  the component outer and the VLOG context; nothing reads behaviour off it, which is what lets the
  whole substrate run on `nullptr, nullptr, {}`. `phys_hinge` now outers its constraint to
  `World->GetOwnerActor()` rather than to a downcast map actor.

  **The player's body is an embodiment.** `GetPlayerViewPoint` / `GetPlayerOrigin` / `TeleportPlayer` /
  `DamagePlayer` / `TraceUseCursor` sit on `IElysiumEmbodiment` because the pawn *is* the player's body
  (**S3**); 11.4 turns them into ordinary entity operations. Two details moved to the body where they
  belong: `point_teleport`'s capsule half-height lift (Source places feet, an Unreal capsule is
  centred) and the `+use` trace itself, which needs the pawn to ignore and the engine channel to trace
  on — the substrate now hands it a segment and gets a handle back, keeping the usability arbitration
  on its own side. `trigger_hurt` and a door closing on the player both route through `DamagePlayer`,
  so `UGameplayStatics` left the substrate entirely.

  **Audio is the voice API, not the subsystem pointer.** `IElysiumAudio` exposes
  `PlayVoice`/`StopVoice`/`SetVoiceVolume`/`IsVoicePlaying` plus `FadeInScheme`/`FadeOutScheme`/
  `ActiveSchemeRel`; the map actor forwards the voices to the GI-scoped `UElysiumAudioSubsystem` and
  the scheme calls to its own manager. The manager's own signatures are untouched — it is still
  handed the subsystem explicitly and still ticked by the map actor.

  **Presenter has no production implementation.** 11.8 is what builds `UElysiumPresentationSubsystem`;
  until then the bundle's `Presenter` is **null in play**, and the world announces `StartFade` /
  `OpenSign` / `CloseSign` / `OpenDialog` / `CloseDialog` *in addition to* holding the state
  `AElysiumHUD` still polls. So 11.8 removes the polling path rather than migrating it, and in the
  meantime the announcements are what let a headless run assert "the chain faded the screen" with no
  HUD to look at. Recorded as a deliberate gap, not an oversight.

  *Acceptance.* `Elysium.Substrate.WorldServices` builds a tutorial-shaped `logic_auto` chain by hand
  — OnMapLoad wires at an NPC (`WillTalk`, `SetAnimation`), a door (`Lock`), an `ambient_generic`
  (`PlaySound`), an `env_fade` (`Fade`) and a delayed `math_counter` wire — plus a `start_enabled`
  `ambient_soundscheme` and a `trigger_changelevel`, and runs it against a recording stub
  (`Private/Tests/ElysiumTestServices.h`, which hands back real transient components so the leaf
  classes take their body-carrying path). It asserts all four seams were reached, then **re-runs the
  same defs with a default (all-null) bundle and asserts the counter lands on the same value** — the
  seam's actual claim, that embodiment/audio/travel/presentation are outputs of the logic and never
  inputs to it — and that `UpdateUseCursor`/`PlayerUse` with no embodiment are safe no-ops. No RHI,
  no actors, no `tools/out`. Live on `sp_tutorial_1`: Jack stands his skeletal body, a `prop_physics`
  simulates at its authored 3.00 kg, the `SP_Tutorial_City` scheme is active with its music state and
  random voices, 38 `ambient_generic` voices play, `elysium.ent_fire teleport_player Teleport` seats
  the player at the destination with the capsule lift, and the `+use` cursor resolves
  `#196 tutwareelevdra(func_door)` with `use_icon 10`.

  *Not in scope, named:* `IElysiumPresenter`'s production side (11.8) and the player-entity rehoming
  of the player-body calls (11.4).
