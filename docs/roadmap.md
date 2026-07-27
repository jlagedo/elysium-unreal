# Elysium-Unreal — Consolidated Roadmap (single source of truth)

**This document is the one work tracker.** It consolidates and supersedes the plan/tracking
sections of `rebuild-strategy.md` (milestones M0–M6, pipeline backlog),
`roadmap-lighting-unreal.md` (lighting tasks L*, absorbed — that file is now a stub),
`debug-tooling.md` (build order), and `engine-core.md` (Phases 1–2). Those docs remain the
**design/reference detail** behind the tasks here; this doc owns **sequencing, status, and
decisions**. If a task exists anywhere, it exists here.

This file is the tracker of a three-file set. Its two companions carry the history so this
one stays readable in a single pass:

- **`roadmap-archive.md`** — the full as-built record of every `[x]` task (and the landed
  portion of `[~]` ones), verbatim, under the same IDs and phase headings.
- **`decisions.md`** — the append-only, dated decision log.

## How to use this doc

- Status marks: `[ ]` open · `[~]` in progress/partial · `[x]` done (verified) · `[P]` parked
  (deliberately deferred — revisit trigger stated).
- Every open task: **ID — name — why/where — acceptance — deps**. Detail lives in the linked
  design doc; don't duplicate it here — link it.
- **Landing a task:** flip its checkbox here in the same change, move its full as-built
  record to `roadmap-archive.md`, and keep a 2–4 line summary here (what landed, what was
  verified, deps). Durable how-it-works facts go to the owning doc (`docs/CLAUDE.md` maps
  them), not the archive.
- **Decisions:** append to `decisions.md`, dated. Record a decision when it's made, including
  "pending" decisions with their trigger.
- Old plan IDs (M1–M6, L0–L5, X1/X2, engine-core Phase 1/2) map to new IDs in the
  **traceability table** at the bottom; other docs may still say "M3" — that table resolves it.

## North star and the slice ladder

Rebuild VtMB as a playable game **— remastered —** on UE 5.8 + C++ from this repo's own
exported intermediates; **bring-your-own-game holds** — nothing game-sourced is committed
(strategy and principles: `rebuild-strategy.md`). The world's *look* is **baked offline into a
gitignored `.uasset` plugin mount** (`/ElysiumBaked`, regenerable like `tools/out/`) and adopted at
load, while collision, entities, scripting, audio and NPCs stay runtime-built — adopted 2026-07-26
(`decisions.md`, cont. 6; roadmap 0.9). Everything is proven on `sp_tutorial_1` first (1,226 entities, 75 classnames — VtMB's own
vertical slice), then scaled across ~100 maps.

**Direction (`remaster-direction.md` — read it):** keep VtMB's tone, ambience, feel and logic;
raise the craft. **Presentation** (UI, type, HUD, textures, post) modernizes freely under the
art-direction test. **Feel** (movement, camera, combat) is built faithful first and polished
only by explicit call. **Logic and content** is reproduced — a behavioural divergence requires
the faithful behaviour to be RE'd and understood *first*, plus a dated owner decision in
`decisions.md`. Default is always reproduce. The UI has no classic mode (VtMB's screen structure,
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
| **P7 — Dressing & parity** | A/B match vs the original's captures on tutorial + hubs | P4–P6 |
| **P8 — Characters & UI** | NPCs stand in the world; New Game from a real, modern menu | P7 tail |
| **P9 — Dialogue & persistence** | **tutorial completable as retail**, save/load works | — |
| **P10 — Scale & ship-shape** | all maps, floor validated, packaged story | ongoing after P4 |
| **P11 — Runtime spine** | New Game boots, plays, pauses, saves and loads on one clear API | P4, P8, P9 |
| **P12 — The theatre** | the intro cinematic plays for real — choreo, camera, audio, subtitles, live faces | P8 tail |
| **P13 — Tutorial mechanics** | stealth, disciplines, firearms — every retail tutorial beat | P9 |

## The playable path (PP0–PP6) — the master sequence

**Owner call, 2026-07-26 (`decisions.md` cont. 5).** One path to a real, played game drives all
sequencing: **menu boot → New Game (genesis chargen) → the theatre cinematic → land on the
tutorial with Jack → complete the tutorial.** Everything on the path lands first; everything off
it waits. Three standing rules:

1. **Logic and interactions first.** Gameplay systems outrank everything else.
2. **Graphics and performance are frozen.** No look polish, no perf tuning, no pretty-graphics
   work of any kind (P3/P7 open tasks, 10.1–10.3, asset enhancement) until the path lands — the
   game must *run and be felt* before another hour goes into how it looks. A simple-but-working
   screen beats a polished absence. The render path stays exactly as configured today; the only
   exceptions are rendering bugs that block gameplay.
3. **Acceptance is played, not injected.** A rung completes when its beats run from real input in
   the built game, beat-scripted in the Play tier (11.10) so the claim is a CI run — console
   state-injection and dev shortcuts are implementation aids, never acceptance evidence.

| Rung | Delivers | Tasks (in order) |
|---|---|---|
| **PP0 — the core refactor** | the spine: one clock/frame, world services, app states + pause, the player entity, input scopes, commands + user command, the view seam, the play harness | 11.5, 11.6, 11.8, 11.10 *(11.0, 11.1, 11.2, 11.3, 11.4 [x])* |
| **PP1 — New Game & genesis** | chargen for real: clan, **name**, sex, spends — onto the player entity, Python-readable; `sp_genesisdevice_1` played, not skipped | 9.4 (+ `sp_genesisdevice_1` export/bake), 9.7d, 8.6's New Game click path |
| **PP2 — the theatre cinematic** | the intro plays start to finish: choreography, scripted camera, line audio, subtitles, **eyes and lipsync — all block** (cont. 5) | 11.7, 12.1–12.5 (+ `sp_theatre` export/bake) |
| **PP3 — land the tutorial** | the chain hands the player to Jack; the first conversation runs with sound and reactions | 9.2, 9.9 |
| **PP4 — core mechanics** | faithful movement (owner call: **in** the path), camera modes, feeding, items + object interaction, dice, the vitals HUD | 4.7, 10.6, B6, 9.8, 9.6, 8.9 |
| **PP5 — persistence** | save / quick / autosave + load mid-run; `trigger_autosave` live | 11.9 (= 9.5) |
| **PP6 — complete the tutorial** | stealth, disciplines, firearms — every retail beat to the exit, proven headlessly | 13.1, 13.2, 13.3 → P9's slice acceptance as `test.bat Play` |

**After PP6 (the thaw):** 9.10 economy/barter, 8.8, 8.10, the P3/P7 look lanes, 10.1–10.5 and
asset enhancement — re-sequenced then.

*(Pre-path housekeeping, now done: **0.9** put the bake architecture on the record and confirmed
`spike/uasset-bake` — the branch all of this sits on — is `main`. Docs + git only, zero rendering
work.)*

## Now — the unblocked front

The open tasks whose dependencies are met, in the order they pay off. Regenerable from the
deps below — refresh it whenever a task flips:

1. **11.5 → 11.6 → 11.8 → 11.10** — close PP0: scopes, commands + user command, the view seam,
   the play harness. Every dep is met. 11.6 also inherits the three verbs 11.4 parked on
   `AElysiumPlayerController` (+use, sign dismissal, the skybox toggle) and the gait latch.
2. **9.4 / 9.8 / 9.9 / 9.10 / 9.5** — the five systems the hinge unblocked. They now land *on*
   `FElysiumCombatCharacter` and `FElysiumPlayerRecord`: 9.4 turns the sheet's dynamic bag into
   registered fields and gives health a real derivation, 9.10 finishes the economy over the
   `money` field that already exists, 9.8 fills the record's inventory half, and 9.5 (= 11.9)
   walks the chain.
3. **9.7d** — land `OneOfSet` and the shadowed-name split: 589 dialogue gates currently fail
   closed, the fix needs no backing system, and PP3's dialogue depends on those gates.

**P12 is fully sourced** — everything left on it is runtime work. RE19 [x] and RE20 [x] closed
the format half; **PL9 [x]** mirrored the 5,444 `.vcd` + 7,136 `.lip`, and **PL10 [x]** the
morph targets, the flex rigs and the 249 `expressions/` tables. One finding reshapes the
ladder: **no shipped model carries eyeball data**, so 12.4 has no eye pose to decode and its
look-at half needs an owner call.

The lighting/look lane (3.1–3.13, 7.x remainder) is **frozen** under playable-path rule 2.

## The first-beat path (B*) — landing → the second warp point

The cross-phase priority ladder for the first *playable* game beat: the player lands on
`sp_tutorial_1` and the tutorial's opening runs unassisted up to the second warp. The tutorial's
"warp points" are its teleport stations. **Warp #1** is where the player already lands: the porch
at `teleport_very_beginning` (the `tutorial` `info_landmark` — 8.6a seats the player there).
**Warp #2** is the patch's relocation to the downtown alley: `teleport_fade` (an `env_fade`) fires
`OnBeginFade -> teleport_player.Teleport` + `teleport_jack.Teleport`.

**The data flow** (traced from `sp_tutorial_1.ents` + `tutorial.py`, patch flow):

1. **Map load** — `logic_auto.OnMapLoad -> unhidePlus()` → `ccmd.patchtype` → `setPlus()` arms
   `trig_popup_move` (`StartDisabled 1`). Live end to end (B5): the console bridge runs the whole
   chain unassisted on a fresh New Game.
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

- [x] **B1 `env_fade` fires `OnBeginFade`** — `Fade` starts the screen fade then fires
  `OnBeginFade` (activator through, no delay) — the whole class per the decompiled `CEnvFade`
  datamap; `OnEndFade`/`ReverseFade` do not exist in VtMB, and the inherited fade curve was
  inverted (`SF_FADE_STAYOUT` is what brings the screen *back* — see `decisions.md`). *Verified
  frame by frame:* `elysium.ent_fire teleport_fade Fade` runs all six wires with their authored
  delays and lands the player in the downtown alley. *Deps:* none.
- [x] **B2 Real entity objects in CPython** *(= 9.3's first slice)* — the `vampire` module
  carries a real **`Entity`** type (`ElysiumPythonEntity.cpp`) + **`Player`** + all 11 module
  globals: generation-checked handles (stale → retail's `AttributeError`),
  `__getattr__`/`__setattr__` over the P1 class-chain tables (an input name manufactures a callable
  firing through `EnqueueInput`; a field name marshals the live value), `G`'s mapping protocol, and
  the level-script merge into `__main__`. The native surface moved to `ElysiumScriptNatives.{h,cpp}`,
  shared by both hosts. *Verified:* `elysium.py.firstbeat` runs `DialogPostProcess()` through the
  installed host → B1's warp, script-driven. *Deps:* B1, 9.3a.
- [x] **B3 Minimal NPC presence** *(the 8.5 carve-out this beat needs)* — `FElysiumNpc` (14
  living-NPC classnames) + `FElysiumNpcMaker` (`ElysiumNpcClasses.cpp`): every NPC stands its real
  `out/npc/<stem>.glb` skeletal body at its origin (per-stem cache, `elysium.NpcBodies` A/B),
  latches `WillTalk`, opens a dialog session on `StartPlayerDialogRemote` (manual `EndDialog` until
  B4), and `npc_maker.Spawn` synthesizes a child via the new
  `FElysiumEntityWorld::SpawnRuntimeEntity`. *Verified:* Jack + the Sabbat stand their models;
  `ent_fire Jack … EndDialog` warps the player; `blueblood_maker.Spawn` produces the blueblood.
  Feeds but does not close 8.5. *Deps:* 8.2, PL4 [x].
- [x] **B4 `.dlg` parser + dialogue runner** *(9.1's core made playable; UI is interim)* — an NPC's
  `dialogname` opens a `FElysiumDlgConversation` on `StartPlayerDialogRemote`; field-4 evals / field-5
  execs route through the installed host (`FElysiumEntityWorld::EvalCondition`), and the world's open-
  dialogue seam (`OpenDialog`/`PlayerDialogChoose`/`PlayerDialogAdvance`) fires the owner's `OnDialogEnd`
  (→ `DialogPostProcess()`) on close. The interim UI is a **purpose-built native-Slate visual-novel box**
  (`SElysiumDialogueBox`, HUD-driven, `FInputModeUIOnly`, number-key/click) — not the sign path; 9.2
  replaces it. `elysium.dlg` / `.choose` / `.advance` are the scriptable echo. *Acceptance met (built
  game):* `Jack.StartPlayerDialogRemote` → box renders line 11 + the live-gated "Who are you?" → 11→21→id-22
  → `G.Tut_Jack==1` → `OnDialogEnd`/`DialogPostProcess`. As-built: `roadmap-archive.md` B4. *Deps:* B2, B3.
- [x] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — `unhidePlus()` resolves, `setPlus()`
  arms `trig_popup_move` and the Plus gates. *Verified in the built game (fresh New Game):* the
  map-load `logic_auto` fires `unhidePlus()`, whose `ScheduleTask`'d `c.patchtype=""` → alias
  `patchtype` → `setPlus()` (real vamputil) → `trig_popup_move.Enable()`, all in the I/O history
  with no manual injection. As-built: `roadmap-archive.md` B5. *Deps:* 9.3.
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
- [x] `.env` sky/fog + `M_Sky` (the `.cube` colour-grade LUT was a test, never wired in —
  stripped 2026-07-26; grading re-enters only through 3.7); light rig + lightstyles (fully
  dynamic HWRT Lumen + MegaLights + VSM config); `.hulls`/`.dispcol` brush collision
  (walkable); static props (originally runtime `UStaticMesh` + ISM — now baked `SM_*`
  assets, see 0.9).
- [x] Designs adopted: `engine-core.md` (entity object model), `debug-tooling.md`
  (the layered debug architecture), `map-architecture.md` (map lifecycle; async travel is
  design-only → R10.4).

---

## P0 — Ground truth & de-risk

Cheap tasks that unblock or de-risk everything downstream. Do these before/alongside P1.

- [x] **0.1 Profiling baseline** *(was L0.1)* — automated + headless: `profile.bat` drives the
  `-ElysiumProfile` harness (`ElysiumProfiler.cpp`) over fixed vantages; `tools/profile_report.py`
  emits the table. Baselines for `sp_tutorial_1` / `sm_hub_1` / `sm_pawnshop_1` + the 0.2 verdict:
  **`rendering-perf.md` → "Profiling baseline"**. `PCD3D_SM6` confirmed; add a vantage with
  `elysium.campos`. *Deps:* none.
- [x] **0.2 MegaLights engagement check** *(was L0.2)* — answered by the 0.1 harness (it logs
  MegaLights vs ShadowDepths every run): MegaLights dominates, the many-light cost is ~flat (687 vs
  161 vs 395 lights ≈ the same ~1 ms), no silent VSM fallback → **3.1 is not the immediate next
  task.** Verdict: `rendering-perf.md` → "Profiling baseline". *Deps:* 0.1.
- [x] **0.3 Export a second map** *(was X1)* — `sm_hub_1` (687 lights) and `sm_pawnshop_1` (161,
  the travel target) exported via `export_all.py`; both load, walk, and profile headlessly with
  fixed vantages. Unblocks 3.4, 4.6, 10.1, 7.8; kills the tutorial-only bias. *Deps:* none.
- [x] **0.4 Sidecar space audit** *(was X2)* — every downstream-consumed sidecar (`.ents`,
  `.sprites`, `.spawn`, `.water`, `_decals.obj`) verified already emitted in Unreal cm through
  `source_to_unreal`/`INCH_TO_CM`; only `rebuild-strategy.md`'s contract table lagged (corrected).
  **PL7 is empty**; the runtime `.spawn` read is verbatim. *Deps:* none.
- [x] **0.5 Cog 5.8 compile spike** — de-risked: the main [Cog](https://github.com/arnaud-jamin/Cog)
  plugin (upstream `cb1b435`) vendored into `Plugins/Cog/` builds + runs clean on UE 5.8 (no ImPlot
  `INFINITY` patch needed); `UElysiumCogSubsystem` registers 15 stock CogEngine windows; the F1 menu
  + Inspector verified in PIE and standalone. Full integration is 2.1. *Deps:* none.
- [x] **0.6 `ent_survey` count reconciliation** *(RE, small)* — the survey deterministically
  yields **16,125 outputs / 1,591 Python** on retail `Vampire/maps` (16,214/1,621 was a stale
  snapshot); pinned across the docs, patch-set counts recorded (108 maps → 24,081 outputs).
  *Deps:* none.
- [x] **0.7 Repo hygiene** — the `tools/ghidra*/` + `tools/re/` trees are local-only RE
  references: the 15 previously-committed files untracked (kept on disk), two blanket gitignore
  globs, docs re-based. *Deps:* none.
- [x] **0.8 Re-base `entity_io.md` on the patch (engine-loaded) map set** *(RE, small)* — every
  table in `entity_io.md` is now the 108-map patch set (**71,096 entities / 326 classnames /
  24,081 outputs / 6,956 Python**), retail kept as a labelled comparison; the patch `maps/` is a
  strict superset of retail. `ent_survey.py` keeps the retail default and gains `--patch`.
  *Deps:* none.
- [x] **0.9 The uasset-bake architecture — decide, document, land** — the architecture running in
  practice since 2026-07-25 is now on the record: **the world's *look* is baked offline into
  `.uasset`s; everything else stays runtime-built** (`decisions.md` 2026-07-26, cont. 6).
  `bake.bat` → `tools/bake_map.py` bakes each exported map into the **gitignored** `/ElysiumBaked`
  mount (only the `.uplugin` committed) and `AElysiumMapActor` adopts the baked level by tag, while
  collision, ropes, the sky cube, the entity substrate, NPCs, audio and scripting stay
  runtime-built. `CLAUDE.md` and `rebuild-strategy.md` lost their "no `.uasset` baking / no editor
  in the content loop / build everything at map-load / Nanite is not applicable" lines, `bake.bat`
  joined the script table, and `uasset-bake-spike.md` became the adopted pipeline reference. The
  merge was already landed — `spike/uasset-bake` and `main` are the same commit. Residue found and
  handed on, not fixed here: the superseded Lumen-card path still has two dead ends (**PL11**).
  Full record: `roadmap-archive.md` → 0.9. *Deps:* none — and everything since 2026-07-25 informally depended
  on it.

## P1 — Entity substrate *(design: `engine-core.md` — read it; steps here are the tracker)*

- [x] **1.1 Currency types + persistent state** — `FElysiumVariant` (7 categories, total
  coercions), `FElysiumEntityHandle` (stable index + epoch), `FElysiumGameClock`, and
  `UElysiumGameStateSubsystem` (GI) with the case-sensitive default-0 `G` store, the quest map, and
  `elysium.g`. *Deps:* none.
- [x] **1.2 `.ents` defs parser** — `FElysiumEntityDefs::Parse` reads `<map>.ents` into immutable
  `FElysiumEntityDef` records (classname / targetname / verbatim Unreal-space origin / raw keys /
  brush hulls / `bStartHidden` / 7-field outputs); `elysium.ents [map]` verifies the round-trip off
  disk. *Deps:* 0.4.
- [x] **1.3 Class registry + base entity** — `FElysiumClassRegistry` (per-classname factory +
  base-chain + input/field tables, case-folded chain-walking lookup) and `FElysiumEntity` (plain
  C++) carrying the 22 CBaseEntity keyfields, the three base inputs
  (`Kill`/`ScriptHide`/`ScriptUnhide`), and one-switch dormancy; unregistered classnames become
  inert records. `elysium.classes` lists/probes. *Deps:* 1.1.
- [x] **1.4 Entity world + event queue + chokepoints** — `FElysiumEntityWorld` (one entity per
  def, name/classname indices, generation-checked `Resolve`, epoch teardown) with the two
  chokepoints (`AcceptInput`, `FElysiumEventQueue::Add`), **think-first** `Tick` (retail order,
  RE2), the `FElysiumIOSink` taps (always-on ring buffer + log + VLOG), and `FElysiumNullScriptHost`
  behind `IElysiumScriptHost`. Verbs: `elysium.world[.io|.fireinput]`. *Verified* on
  `sp_tutorial_1`: 1868 entities, `OnMapLoad` chains deliver through the real queue at their delays.
  *Deps:* 1.2, 1.3.
- [x] **1.5 Brush bodies** — `UElysiumBrushComponent` per brush entity (convex `UBodySetup` off
  the def's entity-local hulls; per-classname solidity: `trigger_*` overlap / `func_illusionary`
  none / else BlockAll), overlap routed to `OnTouchStart`/`OnTouchEnd`, dormancy-gated through the
  one `SetDormant` switch; `elysium.BrushBodies` A/Bs. Tutorial: 185 bodies, no cook stall.
  *Deps:* 1.4.
- [x] **1.6 Starter classes** — the first leaves (`ElysiumStarterClasses.cpp`): `logic_auto`
  (map-load ignition via a one-shot think), `logic_relay`, and `trigger_multiple`/`trigger_once`
  over the registry-only `CBaseTrigger` chain node (ALLOW_CLIENTS filter, `wait` debounce,
  `trigger_once` self-kill). *Verified headless:* relay cascades and trigger touches fire through
  the real queue. *Deps:* 1.4, 1.5.
- [x] **1.7 Labels & debug strings** — editor-only World Outliner labels on every runtime spawn
  path (`ElysiumEditorLabels.h`, compiled out of Shipping); the canonical `#<idx> <name>(<class>)`
  string (`FElysiumEntity::DebugString`) threads every I/O log line. *Deps:* 1.5.

**Slice acceptance:** loading `sp_tutorial_1` fires the `logic_auto` chains through real
queue entries; walking through a trigger logs timestamped I/O lines; Python payloads appear
as script-host log lines; ring buffer holds the session history — observable with logs only.

## P2 — Debug layer *(design: `debug-tooling.md` Layers 1–2)*

- [x] **2.1 Cog integration** — `FElysiumCogWindow` (the base handing derived windows the map
  actor / entity world / game state Cog's UObject-reflection inspector can't reach) + the
  `Elysium.Status` summary window; an **Elysium** category in the F1 menu; everything
  `#if ENABLE_COG`. *Deps:* 0.5.
- [x] **2.2 Entity windows** — **Entities** (filterable clipper-paged browser + histogram),
  **Entity Inspector** (identity, chain-walked live fields, raw keyvalues, 7-field outputs,
  per-input fire buttons), and **Event Queue** (pending deliveries, I/O ring, Pause/Step) windows
  with a shared selection; the chokepoint-respecting `FElysiumEntityWorld::EnqueueInput` seam lands
  here. *Deps:* 2.1, 1.4.
- [x] **2.3 `ent_*` verbs** — `UElysiumEntityDebugSubsystem` registers the Source-style set:
  `ent_fire` (through the real queue; crosshair picker; input discovery), `ent_dump`/`ent_info`,
  `ent_pause`/`ent_step`, `ent_break` (queue breakpoint), and the `ent_text`/`ent_bbox`/
  `ent_messages` overlays — riding a `FElysiumDebugTapSink` through the new `AddSink` seam. The Cog
  Entity Inspector is the primary live surface. *Deps:* 1.4–1.6.
- [x] **2.4 World visualization** — the `Elysium.World Viz` window + three layers on the debug
  subsystem: retained GPU-instanced **entity gizmos** (`FElysiumGizmoLayer`, class-colour palette,
  off/visible/all cycle), **trigger AABB wireframes**, and fading caller→target **I/O beams**
  tapped at the delivery chokepoint; picking rides the 2.3 picker. *Deps:* 1.5.
- [x] **2.5 Maps/Lights windows + `elysium.reload` + cheat manager** — **Maps** (travel/reload,
  per-phase load timings) and **Lights** (source breakdown + live calibration sliders via
  `UElysiumLightRig::ApplyLiveTuning`) windows; `elysium.reload` (the export→reload hot loop);
  `UElysiumCheatManager` (`Noclip`, `ElysiumTeleport` + the stock execs) on
  `AElysiumPlayerController`; the Canvas HUD trimmed to the FPS/position overlay. *Deps:* 2.1.
- [x] **2.7 Agent-facing MCP surface** *(design: `debug-tooling.md` Layer 3)* —
  `UElysiumMcpSubsystem` registers **20 `elysium_*` tools** (map / player / entity / queue / io /
  script / audio / screenshot + the `console_exec`/`log_tail` escape hatches) through the engine
  `ModelContextProtocol` plugin's direct `AddTool()` path, so they work in `-game`/PIE. On by
  default in dev builds (`-NoElysiumMcp` opts out), loopback no-auth, `.mcp.json` at repo root.
  Verified end to end. *Deps:* 2.3.
- [x] **2.8 Automation tests (both tiers)** — the suite in `Private/Tests/`: a content-free
  substrate tier (`-nullrhi`: variant / expr / KeyValues / queue / registry + one end-to-end I/O
  chain) and a content-gated tier that parses the real exports and **self-skips** when unexported;
  `test.bat` drives headless with a JSON+HTML report. All pass. *Deps:* 1.4, 5.2.
- [x] **2.9 Screenshot-regression harness** — `FElysiumShotRun` (`-ElysiumShots`) captures the
  profiler's exact vantages (shared `ElysiumVantages.h`) to `tools/out/_shots/<map>/` + a manifest
  (game-derived → gitignored); `shots.bat` mirrors `profile.bat`; the capture path is shared with
  the MCP screenshot tool. **B6** (SKY) grew it: sky-framing vantages (`t1sky`/`h1sky`) +
  `tools/shots_diff.py` (baseline promote/diff — per-vantage mean/p99/%-moved, heat maps, a
  gating exit code), with the noise floor measured: a baseline is only valid against a fixed
  bake *and* fixed content assets (`roadmap-archive.md` → SKY/B6). *Deps:* 0.1, 2.7.

**Slice acceptance:** in standalone — browse entities, pick the elevator call button through
the crosshair, hand-`ent_fire` its chain, watch beams + queue window, pause/single-step,
`elysium.reload` after a re-export without restarting. The P4 test harness exists before P4.

Deferred (tracked, not scheduled): dynamic console autocomplete of targetnames
(`UConsole::BuildRuntimeAutoCompleteList` via custom viewport client); Gameplay Debugger
category; Remote Control channel; NetImgui remote — see Options.

## P3 — Lighting correct & engaged *(parallel lane; detail: `rendering-perf.md`, `lighting.md`)* — **FROZEN** *(playable-path rule 2; gameplay-blocking rendering bugs excepted)*

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
- [x] **3.5 Sky IBL onto the SkyLight** *(was L2.1)* — landed inside sky-ambience **C1/C2**
  (D2): the built cube is assigned (`SLS_SpecifiedCubemap` + `RecaptureSky()`), intensity =
  the map's own type-5 magnitude ÷ the cube's measured upper-hemisphere mean
  (`bLowerHemisphereIsBlack`), and **zero on the 83 maps with no `light_environment`** — the
  "floor ambient" this task once named is superseded by that owner call; a night-sky lift
  goes through the D3 knobs, never here. Unblocked 7.5. As-built: `roadmap-archive.md` → SKY.
  *Deps:* none.
- [ ] **3.6 Pinned exposure** *(was L2.2)* — fixed EV/bias on the per-map post-process;
  deterministic LDR framing. C3's per-map PPV (tagged `elysium.ppv`, adopted unbound) is the
  natural home, and auto-exposure is already off in config
  (`r.DefaultFeature.AutoExposure=False`) — this task pins the *value* per map. *Deps:* none.
- [ ] **3.7 Grade/tonemapper fidelity** *(was L2.3 + M1 polish)* — stop the filmic curve
  crushing the look (neutralize the tone curve or re-fit; the test `.cube` LUT was stripped
  2026-07-26, so any grade re-enters only through this task + `elysium.GradeIntensity`); add
  `elysium.*` A/B toggles for sky/LUT/fog. **Sky orientation is done** — `sky-ambience.md`
  Phase B landed it (B3 + B1). What remains here is the tone curve, and B4 sized it: measured
  end to end, the filmic toe crushes a night sky by up to ×9 and unity crosses parity only at
  source ≈ 55, so with VtMB's skies sitting almost entirely below that, **the backdrop's
  remaining gap to parity is this task's**, not the sky's. *Deps:* 3.6.
- [ ] **3.8 Texture prewarm off the game thread** *(was M1.3)* — worker-thread batch in
  `FElysiumTextureCache` (load is texture-bound; Godot `Prewarm` shape). *Deps:* none.
- [ ] **3.9 Lightstyle clock pin + freeze** *(was L4.4)* — cvar-pinned phase + freeze toggle
  for reproducible A/B captures. *Deps:* none.
- [ ] **3.10 Volumetric fog layer calibration** — B8b moved Source's distance fog into the
  per-primitive material term, leaving `ExponentialHeightFog` owning only the volumetric
  haze/light-shaft layer D4 sanctioned — which at its current density (`3/end`, and the
  engine divides by 1000 again) is near-invisible: a mechanism, not yet a look. Calibrate it
  for its own sake; it no longer rides on the distance-fog numbers. Presentation layer —
  adjudicated by the direction test, A/B via `elysium.Fog` + the fog cvars. *Deps:* none.
- [ ] **3.11 Verify `elysium.LumenDiffuseBoost`** — C3's third knob drives
  `LumenDiffuseColorBoost` but produced no measurable change, live or across a map load; the
  suspected cause (consumed where the surface cache is *written*, so a live set cannot
  re-cache) is a hypothesis, not a finding. Test at bake/boot time (config var before first
  capture); wire it or retire it. D3's sanctioned bounce knob depends on the answer.
  *Deps:* none.
- [ ] **3.12 `sm_hub_1` fill adjudication** — judge the 15 shortlist disagreements
  (`light-attribution.md` → "Where to pick up") by in-engine A/B, now that C2 zeroed the
  map's SkyLight (no pair — its "sky glow" is all sprayed fill, *more* load-bearing than
  before) and C3's Skylight Leaking is the landed, measured replacement to gate against.
  Kill a fill only where GI demonstrably replaces it; one dated `decisions.md` entry per
  map. *Deps:* none (C3 [x]).
- [ ] **3.13 Decal fog: accept or extend** — a `UDecalComponent` carries no custom primitive
  data, so its fog set is bound into the baked material instance: correct per map, but
  `elysium.Fog` does not reach it and a fog change needs a re-bake, not a reload. Either
  record that as the accepted contract or extend the live path (re-derive decal MID
  parameters in `ApplySceneFog`). Small. *Deps:* none.

## P4 — Interaction *(design: `engine-core.md` class ladder + `animation_and_movers.md` Part B)*

- [x] **4.1 Mover base** — `FElysiumMoverBase` (constant-velocity, swept
  `LinearMove`/`AngularMove` on the substrate clock — the kinematic body pushes the pawn and
  reports blockers) + `FElysiumDoorBase` (the CBaseDoor 4-state machine:
  Open/Close/Toggle/Lock/Unlock/Use, `wait` autoclose, the locked path, blocked-while-closing →
  damage + reverse) + the prototype `func_door_rotating` leaf. *Chaos push/block feel still needs
  an in-game play test.* *Deps:* 1.5, 1.6.
- [x] **4.2 `func_button`** — `FElysiumButton` over the mover primitive (press → `OnPressed` →
  spring-back / latch / toggle); spawnflags reconciled against the decompiled `CBaseButton::Spawn`
  (`0x100`=TOUCH, `0x400`=USE — the `entity_io.md` label swap fixed). The minimal +use look-cursor
  (`UpdateUseCursor`/`PlayerUse`, pawn `E` key) lands with it, firing `OnIn`/`OnOut` on aim
  enter/leave. *Deps:* 4.1.
- [x] **4.3 `func_door` / `func_door_rotating`** — the sliding `FElysiumFuncDoor` leaf (movedir
  translation by its own depth minus `lip`, per the decompiled `CBaseDoor::Spawn`); the full
  spawnflag table honoured (`START_OPEN`/`REVERSE`/`LOCKED`/`NO_AUTO_RETURN`/**`PUSE`**); the
  doorknob `DoorUse()` path with **`linked_door`** partner mirroring; mover runtime state in the
  Cog Inspector via the new `GetDebugState` hook. *Verified:* the tutorial elevator pair slides
  open together. *Deps:* 4.1.
- [x] **4.4 `+use` verb + use-icon HUD** — a dedicated use-only trace channel (`ElysiumUse`,
  `ECC_GameTraceChannel1`) + the context-icon HUD: `use_icon`/`locked_icon` base fields,
  `GetUseIcon()` locked resolution, ring + icon cell drawn from the PL3 atlas
  (`out/hud/use_icons.*`); the 72-entry enum in `ElysiumUseIcons.h`; a `+use` section in the Cog
  Inspector. PL3 (use-icon atlas export) done. *Deps:* 4.2, PL3.
- [x] **4.5 Tutorial logic classes** — the tutorial's logic/point/brush + trigger classes as
  decompile-grounded leaves (`ElysiumLogicClasses.cpp`): `math_counter`, `logic_timer`,
  `logic_case` + the VtMB-divergent **`logic_case_toggle`** (InValue is a *delta* advancing a
  configured-case pointer), `env_fade`, `func_brush`, `point_teleport`,
  `trigger_hurt`/`trigger_look`/`trigger_autosave`; the Source `COutput<T>` value seam
  (`FireOutput` fills an empty map-param); the `Elysium.Logic` Cog window.
  `trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` stay inert (their
  backing systems don't exist yet). *Deps:* 1.6.
- [x] **4.6 `trigger_changelevel` + landmark travel** — cross-map travel through a shared
  `info_landmark`, translation-only, grounded in the decompiled `CChangeLevel`: a touch or a
  scripted `ChangeLevel` fires `OnChangeLevel`, captures the player's source-landmark offset +
  view yaw, and `UElysiumMapSubsystem::RequestLandmarkTravel` runs the deferred travel next tick,
  seating the player at `dest_landmark + offset`. The scripted `ChangeMap()` is real;
  `elysium.map <map> [landmark]`; a Transitions section in the Maps window. *Verified headless:*
  tutorial → `sm_pawnshop_1` at `dest_newgame + offset`. *Deps:* 1.6, 0.3.
- [ ] **4.7 Source movement component** *(was M1.1; parallel-capable)* — port `CGameMovement`
  friction/accel/airaccel/StepMove into a `UCharacterMovementComponent` override
  (`source_movement.md`, Godot `SourceMovement.cs`). **Faithful first** — this is the feel
  layer's known-good baseline and the thing every later tuning delta is measured against, so it
  lands line-by-line from the decompile and stays A/B-able (`remaster-direction.md` axis 3).
  Frame-rate independence, high-polling-rate mouse input and FOV control ride along (identical
  behaviour, modern plumbing); any *behavioural* delta — accel curves, air control, step feel —
  is a separate, owner-approved decision after this runs. **The body must be a box, so this is not a
  `UCharacterMovementComponent` override**: `ACharacter` creates a capsule root that cannot be
  substituted, and a capsule's rounded bottom reports ~0.65 against `StepMove`'s `0.7` standable test,
  rejecting every climb (`source_movement.md`). **11.6** re-bases the pawn to `APawn` + box +
  `UElysiumMovementComponent` and supplies the `FElysiumUserCmd` this consumes. Sequenced **in
  the playable path (PP4)** by owner call (`decisions.md` 2026-07-26 cont. 5) — the tutorial is
  played with VtMB feel, not UE feel. Open RE it needs: **RE22** (the ducked hull's dimensions —
  `IN_DUCK` is in the user command with nothing sizing it) and **RE21** (where usercmd
  processing sits in `GameFrame` relative to the think pass). *Deps:* 11.6.
- [ ] **4.8 Rotating/linear/elevator family** — `func_rotating` (spin-up/down, hurt-touch),
  `func_movelinear`, `func_elevator` (`GotoFloor`, floor Z table), keyframed movers if the
  tutorial needs them. *Deps:* 4.1.
- [x] **4.9 Event-bus classes (`events_player` / `events_world`)** — both singletons land with
  their complete faithful input/field surfaces (datamaps recovered via `DumpDatamap.java`:
  `CPlayerEvents` 12 inputs / 23 outputs, `CWorldEvents` 10 / 21). The outputs await their driver
  systems (disciplines, cop/masquerade AI, music) — each input latches state + logs rather than
  silently no-opping. *Verified:* `pc_0.MakePlayerUnkillable()` + `world.SetNoFrenzyArea(1)`
  deliver instead of `[no input]`. *Next:* P6.3's music state machine drives `events_world`'s six
  music outputs. *Deps:* 1.6.
- [~] **4.10 `game_sign` / `prop_sign` — sign windows** — **`game_sign` + PL5c landed:**
  `UE_extract_signs.py` (278 definitions + 57 background materials → `out/signs/`), the shared
  `ElysiumKeyValues.h` character-stream reader, `FElysiumSignData` (`SignData` + the first-true
  `Sign { dependency; filename }` redirect via `EvalCondition`), and the `game_sign` leaf
  (`OpenWindow`/`CloseWindow`/`ChangeFile`, `OnUseBegin`/`OnUseEnd`) drawn by `AElysiumHUD` on the
  RE-verified 1024×768 canvas model (`decisions.md` 2026-07-23; panel within 1–2 px of prediction).
  **Still open:** `prop_sign` + its `+use` path (its `use_icon`s already resolve),
  `NewspaperData`/multi-column, `ClientCommand`, `fade_out` linger, `pause` semantics +
  `spawnflags 5` undecoded; real `.fnt`-role type is 8.8. *Acceptance (rest):* `+use` on
  `sign_chopshop_upstairs` reads "password: chopshop"; a dispatch-wrapper sign picks its variant
  from `G`. *Deps:* 4.4, 1.6, PL5c; 5.2 for the redirect.

**Slice acceptance** *(M3 criterion)*: the tutorial elevator chain works — button →
`Unlock`/`Trigger` → doors open → `thug_2` `ScriptUnhide` — and walking out of the tutorial
loads `sm_pawnshop_1` at the landmark.

## P5 — Scripting foundation *(design: `python_bridge.md`, `rebuild-strategy.md` B6)*

- [x] **5.1 PL2: copy scripts + dialogue** — `UE_extract_scripts.py` mirrors the 41 loose `.py`
  → `out/scripts/` and the 147 `.dlg` → `out/dlg/` verbatim, patch-first, whole-game (runs once at
  the end of `export_all.py`); the read-only `Elysium.Scripting` Cog window pre-flights the mirror.
  *Deps:* none.
- [x] **5.2 Expression evaluator** — `ElysiumExpr`: an exception-free lexer + recursive-descent
  parser + tree-walk over the restricted Python-2 expression subset — every failure collapses to
  Void = **error-to-false** (RE3). One namespace: `G.<flag>` read/write, a bare name resolves a
  targetname, `<ent>.<input>` fires through the real chokepoints, `<ent>.<field>` reads the chain
  field table. `FElysiumExprScriptHost` implements the B6 seam (opt-in at this stage); the
  Scripting window grows the live `G` table + eval box. *Deps:* 1.3, 1.4.
- [x] **5.3 Native bindings** — the engine `vampire`-module surface: the 11 module globals + 24
  Character methods; `FindPlayer`/`FindEntityByName`/`SetQuest`/`GetQuestState` real, the rest
  log-a-stub with plausible defaults, and any unlisted attribute still binds (retail's forgiving
  dispatch). One static `GNativeBindings` table — now in `ElysiumScriptNatives.{h,cpp}`, shared by
  both hosts — plus a Native-bindings table + call log in the Scripting window. *Deps:* 5.2.
- [x] **5.4 Field-6 + `logic_pythoncheck` + `ScheduleTask` live** — the expr host becomes the
  map-load default (`elysium.script.live 0` A/Bs the whole surface off together);
  `logic_pythoncheck` lands (`Test` → `OnTrue`/`OnFalse` via `EvalCondition`; Void reads false);
  `ScheduleTask` is real — `EnqueuePython` posts python-only events onto the one queue, steppable
  in the Event Queue window and serializable (R8). *Deps:* 5.2, 5.3.
- [x] **5.5 Level-script decision — embed CPython 2.x (option c) + PoC** — the 36 loose scripts
  are full Python 2.1 (16,473 lines; the 2.1→2.7 delta is ~0), so a maintained 2.7.18 fork
  (`qnox/python-2.7`) is vendored and embedded: `FElysiumPythonVM` + `FElysiumCPythonScriptHost` +
  `elysium.py.*` verbs + a CPython panel in the Scripting window; offline-validated against the
  real `tutorial.py`. Full rationale: `decisions.md` (2026-07-22). *Deps:* 5.1, 5.2. → **9.3.**

**Slice acceptance:** the tutorial's field-6 calls and `logic_pythoncheck` gates actually
execute (e.g. `FindPlayer().ClearActiveDisciplines()` runs, `OnTrue`/`OnFalse` fire);
`G` flags flip visibly in the debug layer.

## P6 — Audio foundation *(design: `audio_pipeline.md`; parallel with P5/P7)*

- [x] **6.1 MS-ADPCM decode** — runtime WAV decode via the vendored single-header `dr_wav`
  (MS-ADPCM / IMA / PCM16 → int16); `FElysiumSoundCache` (decoded-PCM cache,
  `USoundWaveProcedural` per play — the reliable 5.8 route) + `UElysiumAudioSubsystem`
  (`PreviewSound2D`, `elysium.playsound`/`sound_info`) + the `Elysium.Audio` Cog window;
  `UE_extract_sounds.py` mirrors each map's referenced WAVs into `out/sound/`. *Deps:* none.
- [x] **6.2 MP3 decode** — the vendored `dr_mp3` (public domain, patents expired) through the
  same self-decode-to-PCM route (Unreal has no runtime loose-MP3 path): `FElysiumSoundCache`
  dispatches on extension, so the whole subsystem above it is codec-agnostic;
  `UE_extract_sounds.py` mirrors `.mp3` refs (+ `--radio` test loops). *Deps:* none.
- [x] **6.3 `ambient_generic` + SoundSchemes** — the voice pool (per-voice `UAudioComponent`s:
  3D sphere attenuation, attach-to-mover, fades, looping via underflow re-queue); the
  `ambient_generic` leaf (spawnflags + `PlaySound`/`StopSound`/`ToggleSound`/`Volume`/`FadeIn`/
  `FadeOut`); SoundSchemes (`FElysiumSoundSchemeManager`: looping ambient bed, the
  explore/combat/alert music state machine, the polar RandomSound scheduler) +
  `ambient_soundscheme`; the `Elysium.Sound Schemes` window and the global `elysium.Mute` gate
  (default muted). PL5a (scheme file copies) done. *Deps:* 1.6, 6.1.
- [x] **6.4 Mover sounds** — RE: a `soundgroup` token resolves *by directory convention* to
  `sound/usable/<category>/<token>/<subkey>.wav` (doors `open`/`close`/`swing`/`locked`, buttons
  `on`/`off`, the SILENT gate `0x1000`); `UE_extract_sounds.py` mirrors the WAVs + writes
  `soundgroups.json`; the door/button state machines play through the voice pool at the body; a
  Mover-soundgroups browser in the Audio window. *Deps:* 4.1–4.3, 6.1.

## P7 — Dressing & parity *(Track A completion; parallel lane)* — **open tasks FROZEN** *(playable-path rule 2)*

- [ ] **7.1 Coronas** *(was L3.3 / M2)* — `.sprites` consumer: additive depth-tested
  billboards (Godot `CoronaField.cs`), StartOff spawnflag filtering. *Deps:* 0.4.
- [x] **7.2 Decals** *(M2)* — `infodecal`s as **deferred `UDecalComponent`s** (owner call — the
  PMC-parity stage skipped; a deferred decal is lit exactly like its host wall, Lumen bounce
  included): the exporter writes a `<map>.decals` projector sidecar (Unreal cm), and the new
  `M_Decal` master (`tools/make_decal_material.py`) + `BuildDecals` spawn one component per line —
  the decal UV frame is U→local Z, V→local Y (see `decisions.md`).
  `elysium.Decals`/`DecalDepth`/`DecalFlipU`; substrate + content tests. Decals now land
  through the **bake** (one `ADecalActor` per `infodecal`; their world-fog set is bound into
  the baked material instance — the liveness gap is 3.13). *Deps:* 0.4.
- [ ] **7.3 Water** *(M2)* — `.water` → `M_Water` Single Layer Water + Lumen reflections
  (no mirror cameras). Known engine facts: Lumen reflections on Single Layer Water are
  **forced mirror** (roughness only scales brightness — acceptable for VtMB's mirror-like
  water), and **MegaLights does not light water surfaces** — verify the water direct-lighting
  path during this task. *Deps:* 0.4.
- [x] **7.4 Master-material set (rest)** *(was M1.2)* — the four world masters
  (`M_World_Opaque`/`_Masked`/`_Translucent`/`M_Additive`) from one generator
  (`tools/make_world_materials.py`), the full feature set as named params: `Albedo`,
  `Emissive`+scale, `BumpMap`, `EnvMask`+`EnvStrength` ($envmap → **Lumen roughness**, see
  `decisions.md`), `BaseTex2`+`BlendAmount` (WorldVertexTransition via the `.blend` sidecar →
  vertex colour; `.emc` bumped to EMC2). The runtime picks the master per blend flag and binds
  per-channel. Substrate + content tests (439 tutorial materials). *(7.5 supersedes the reflection
  half of this entry, and the `elysium.*` material knobs now reach the baked instances through
  `ApplyMaterialOverrides` rather than the factory.)* *Deps:* none.
- [x] **7.5 Real reflections** *(was L3.1)* — the `$envmap` channel, RE'd, tuned, and extended to
  props. VtMB's composite is `(base + cube·mask·tint) · lightmap · 2` — an **albedo** term the
  light multiplies, not an additive overlay — read out of its own shipped DX8 assembly; the
  non-reflective world becomes **Lambert** and `$envmaptint`'s chromatic half drives `Metallic`
  off the mask (VtMB's own metal mask). Props gained the channel — the *larger* half of the
  reflective set, 1,419 of 2,610 VMTs, previously none. RE + whole-game survey:
  **`docs/reflections.md`**; decision: `decisions.md` 2026-07-26. *Verified:* build + `test.bat`
  green, 10 maps re-exported/re-baked, shots re-baselined, Lumen reflections 0.15–0.22 ms against
  the committed 0.18–0.25. *Deps:* 3.5, 7.4.
- [ ] **7.6 Bloom/glow tuning** *(was L3.2)* — VtMB's overbright neon/selfillum vs pinned
  exposure. *Deps:* 3.6.
- [ ] **7.7 Shadow quality** *(was L3.4)* — contact shadows on hero lights, penumbra softness,
  within 0.1 budget. *Deps:* 0.1, 3.1, 3.2.
- [ ] **7.8 A/B capture harness** *(was L5.1, re-based)* — scripted fixed-camera captures vs
  **the original game** at the shared vantages (RE17's protocol; the Godot viewer is retired
  as the reference — the original outranks a port of a port). The local half exists (2.9 +
  `shots_diff.py`); respect its measured noise floor — re-baseline after any bake or content
  rebuild. *Deps:* 0.3, 3.9, RE17.

**Slice acceptance** *(Track A criterion, re-based)*: side-by-side A/B match with the
original game's reference captures (RE17) on `sp_tutorial_1` + hub maps.

## P8 — Characters & UI *(design: `rebuild-strategy.md` B5, `remaster-direction.md` axis 1; `m0_menu_build.md` = structural reference, not a port target)*

- [x] **8.1 PL1: entity-model export** — `UE_bsp_to_scene.py` decodes every static-`.mdl` entity
  model (the prop family; skeletal `npc_*` excluded → 8.2/8.5) into the shared `props/` dir and
  annotates each entity with `model_mesh`. Tutorial: 160 props / 64 models. Runtime consumption is
  8.3/8.4. *Deps:* none.
- [x] **8.2 glTFRuntime adoption spike** — implemented (compiles + links on 5.8; **the
  visual check landed with B3** — Jack + the Sabbat stand their real `.glb` bodies in the
  built game): `rdeioris/glTFRuntime` (MIT) vendored under `Plugins/glTFRuntime`;
  `mdl_gltf.py` emits standard glTF 2.0, so the plugin reorients at load (the standing `UE_`
  exemption); `UElysiumNpcSubsystem` + `elysium.npc.*` verbs + the `Elysium.NPC` Cog window load
  mesh + skeleton + clip from `out/npc/*.glb`. **Decision confirmed:** glTFRuntime is adopted
  for the NPC track; remaining risk lives in 8.5/PL4 (banks, multi-sequence merge), not the
  plugin. *Deps:* none.
- [x] **8.3 Dynamic props** — `prop_dynamic`(+`_ornament`) now stand their decoded static `.mdl`
  through `AElysiumMapActor::BuildPropVisual` (per-stem `UStaticMesh` cache, one build per model like
  `LoadProps`; per-entity `UStaticMeshComponent`, not shared ISMs — the follow-hooks want per-entity
  addressability), closing the 9.3 "bodiless prop" gap. Per-instance addressable: base
  ScriptHide/ScriptUnhide + the 9.3 SetOrigin/SetAngles/SetModel body-follow + `Break` (hide +
  `OnBreak`); `SetAnimation` logs a stub (LOD0 static geometry, no skeleton). **Skin families landed
  2026-07-26** — `mdl.py` decodes the `.mdl` skin table (`StudioMesh.Material` is a *skinref*, not a
  texture index; `docs/mdl_v2531.md` corrected) and emits `props/<stem>.skins`; the bake resolves it
  into `DA_<map>_PropSkins` (`UElysiumPropSkinSet`); `ApplyPropSkin` repaints the body's material
  slots for the `skin` keyfield, the `Skin`/`SetSkin` inputs and a script `.skin =` write. Skins
  **snap** — VtMB's `skin` is a keyfield-input with a null `inputFunc`, and the crossfading
  `FadeToSkin` is wired zero times in the 16 exported maps (RE + decision: `decisions.md` 2026-07-26,
  `entity_io.md` → "Skin families"). GAME_LUMP static props take `DStaticPropV4.skin` (a 10th `.props`
  field) applied offline by the bake. The `+use` static-mesh family (`prop_button`/`prop_switch`/
  `prop_sign`/`prop_hacking`/`prop_doorknob(_electronic)`/`item_container(_animated/_lock)`) now stands
  bodies + skins too, with no invented I/O (their interaction surface stays 4.10/8.8).
  `elysium.PropSkins` A/Bs. Non-solid (prop_physics collision is 8.4). Placement
  rotation reads the exporter's new `model_quat` verbatim (no runtime angle math). `elysium.PropBodies`
  A/Bs. *Verified:* tutorial loads all 78 `prop_dynamic` without crash, `test.bat` Content+Substrate
  green (a new prop content-assertion included), headless shots show them standing. *Deps:* 8.1, 1.3.
- [x] **8.4 Physics props** — `prop_physics` ×54 / `phys_hinge` ×12 as Chaos rigid bodies +
  hinge constraints. Physics props stand a simulating per-entity `UStaticMeshComponent`
  (`FElysiumPhysProp` → `BuildPhysPropVisual`, `PhysicsActor` profile) on the **baked** `SM_<stem>`,
  the same asset every other prop stands. **Collision is VtMB's own** (2026-07-26): `tools/phy.py`
  decodes the model's sibling `.phy` — the VPhysics convex hulls the original game simulates against —
  into `props/<stem>.phys`, and the bake reproduces each ledge exactly through Geometry Script's hull
  builder under `CTF_UseSimpleAndComplex`. Nothing is decomposed or approximated; CoACD and
  `prop_collision.py` are retired. **Mass** is the `.phy`'s authored value (boulder 2000 kg, crate
  100 kg, wine glass 1.46 kg) unless the entity's `override_mass` > 0 — which is −1 on every
  `prop_physics` in the exported maps, so the authored mass is what they all weigh. A model with no
  collision model stands visible but inert, reproducing `CPhysicsProp::CreateVPhysics`
  (`docs/phy_vphysics.md`); with the Unofficial Patch installed no prop reaches it (27/27 covered).
  `FElysiumStaticMeshBuilder` is deleted — nothing builds a `UStaticMesh` at runtime any more.
  `phys_hinge` (`FElysiumPhysHinge`) builds a `UPhysicsConstraintComponent` (twist on the exporter's
  pre-converted `hinge_axis`, one rotational DOF) in a new `PostSpawn()`/Activate pass, wiring
  `attach1`↔`attach2`/world. The RE'd VtMB I/O surface (Ghidra: `Wake`, **not**
  EnableMotion/DisableMotion/Sleep; `TurnOn`/`TurnOff`/`Break`; `OnBreak`) is in `decisions.md`
  (2026-07-24). **Skins landed 2026-07-26** with 8.3: `Skin`/`SetSkin` repaint the body for real, and
  `FadeToSkin`/`SetSkinFadeTime` are registered against the RE'd behaviour — they snap, because no
  exported map fires them (`decisions.md` 2026-07-26).
  `elysium.PhysicsProps` A/Bs simulation. *Verified:* `build.bat` + `test.bat` Content/Substrate green
  (54 prop_physics / 12 phys_hinge assertions, plus baked-collision/mass assertions on the asset);
  `phy.py` decodes all 2,854 retail `.phy` files → 7,889 hulls, every one convex (`F = 2V − 4`, zero
  exceptions); the bake reports 18 physics meshes / 19 convex shapes / 18 with authored mass, read back
  off the assets. In-game: bodies simulate with the authored masses exact (trashgarage 3.00, barrela
  7.00, break_crate 100.00 kg) and `showflag.Collision` shows the hulls hugging each barrel.
  **Chaos settle/push feel + hinge swing await an owner in-game play test** (like 4.1). *Deps:* 8.1.
- [x] **8.5 NPC presence + `scripted_sequence` minimal** — presence landed in **B3**, the animation
  vocabulary in **PL4**, and this closes the rest: NPCs idle on the stance their disposition selects
  instead of T-posing, and `scripted_sequence` (×104 + 4 `aiscripted_sequence`) runs as a real class.
  Selection is on the engine's own keys — `mdl_skel.local_sequences` now reads `szactivitynameindex`
  and `actweight`, so an idle is picked by **activity** (`default_disposition` →
  `dispositiontable.txt` → `Stance_<Name>_Idle_*`, then `ACT_IDLE` by weight), never by label
  substring. A native `UElysiumNpcAnimInstance` (two sequence players + a 0.25 s crossfade) plays it,
  banks are retargeted by bone name and cached per session, and the script seam
  (`SetAnimation`/`SetGesture`/`SetDisposition`) reaches it through two `FElysiumEntity` virtuals.
  `scripted_sequence` reproduces the beat's outputs — `OnBeginSequence`/`OnEndSequence`, the
  `m_iszNextScript` chain, `m_iszIdle` at spawn — which is the half the maps depend on: 67 of its 88
  wires land on inputs that already exist. RE'd on the way: the class is **`CCineNPC`**, HL1
  lineage, so **no exported sequence starts on spawn** (`entity_io.md`). **Not reproduced:**
  locomotion — the NPC is placed on the mark rather than walked to it, and `OnScriptEvent01..08`
  needs decoded animation events (`decisions.md` 2026-07-26). *Verified:* `Elysium.Substrate.ScriptedSequence`
  + `Elysium.Content.ScriptedSequenceClips` (106 sequences, 88/88 animation refs resolve); in-game,
  `script_7b.BeginSequence` places Jack on his mark 187 m away and its `OnEndSequence` opens his
  dialogue. As-built: `roadmap-archive.md` 8.5. *Deps:* 8.2, PL4 [x].
- [x] **8.6a New Game context + story entry** *(carve-out of 8.6)* — the player sheet +
  `UElysiumGameStateSubsystem::BeginNewGame` seed the fresh-story state (`Story_State=-4`,
  `Tut_Jack=0`, `Tut_Patch=0`, `Linux_Wine=1`) and travel to **`sp_tutorial_1` @ the `tutorial`
  landmark** through the 4.6 path. `-ElysiumMap` keeps the bare dev path; `-ElysiumNewGame=0` A/Bs.
  `elysium.newgame [clan] [m|f]` is the seam the menu calls. Verified headless. *(The New Game
  entry point moved to `UElysiumGameFlowSubsystem::NewGame` with 11.3 — `BeginNewGame` is unchanged
  and still the seeder.)* *Deps:* 4.6, 4.9, 1.1.
- [~] **8.6 UI foundation — design system + shell** *(replaces the VGUI port)* — the modern UI
  stack every other screen sits on. **Not** a `.res`-driven VGUI renderer: a **CommonUI**
  component set with **vector type**, resolution-independent layout (real widescreen/ultrawide,
  DPI scaling, no 640×480 canvas, no `//ws-fix` pairs), and a design-token layer (palette, type
  ramp, spacing, panel treatments). Structure and content come from the original — screen
  inventory, panel anatomy, reading order, iconography, strings — read off `.res`/the schemes as
  **intent** (PL8), not executed as layout. Ships with it: the main menu + pause menu on the new
  stack, and the New Game flow calling 8.6a's New Game seam (now `UElysiumGameFlowSubsystem::NewGame`,
  11.3).

  **Landed — the main menu runs.** Verified in the built game by screenshot: the title lockup from
  the user's own install over **`sm_hub_1` (the Asylum frontage) as a live backdrop — NPCs idling,
  streetlights cycling** — with five small-caps items laid out by the RE'd law. The backdrop is a
  full map build minus the player (the idling NPCs are entities, so a look-only build is an empty
  street — `decisions.md` 2026-07-26 cont. 3 corrects the original call); the HUD stands down while
  a menu is up, since `sm_hub_1`'s `havenbum` opens a conversation unprompted. Design: `docs/ui-architecture.md`; the RE it is checked against: `docs/vtmb-ui.md`
  (the two UI stacks, both schemes, the **1024×768** canvas law, the HUD class inventory, and four
  corrections to `m0_menu_build.md`). **PL8** [x]. The **Nocturne** type set (Spectral SC /
  Spectral / Inter, SIL OFL, no RFN) ships as committed `UFontFace` assets
  (`fetch_ui_fonts.py` → `make_ui_fonts.py`). **CommonUI + CommonInput** adopted with widget trees
  in C++ Slate, so **no Widget Blueprint assets**. `UElysiumUISubsystem` +
  `UElysiumMainMenu` + `ElysiumUIStyle`/`Strings`/`Texture`; `elysium.menu [pause]`,
  `elysium.menu.close`, `elysium.MenuVantage`, `elysium.BootMenu`. Six owner calls:
  `decisions.md` 2026-07-26.

  Three findings the build forced, all recorded in `ui-architecture.md`: `make_ui_fonts.py`
  **cannot** run in the `content.bat` umbrella (a `UFontFace` import flushes Slate's font cache and
  `FSlateApplication::Get()` asserts in a commandlet); a `UCommonActivatableWidget` added straight
  to the viewport is **collapsed until `ActivateWidget()`**; and `ElysiumScreenshot::Request` grew a
  `bShowUI` flag because the harness's UI-free capture silently omits every Slate widget — the MCP
  tool now passes true, the regression harness keeps false so baselines hold.

  **Remaining:** the `CommonUIInputData` config asset + gamepad/keyboard nav pass (11.3 routes Esc
  through the player controller and the menu's own `NativeOnKeyDown` precisely because CommonUI's
  Back action needs that asset); New Game click path untested end to end (the seam is wired, the
  console equivalent works); chargen ahead of New Game (9.4). **Open risk:** `shots.bat` cannot see
  the UI layer, so 8.9's HUD needs UI-inclusive vantages or its regressions go unwatched.

  **Acceptance:** main menu and pause menu are legible and correctly proportioned at 1080p,
  1440p, 4K and 21:9 with no letterboxing or bitmap-font blur; New Game enters `sp_tutorial_1`
  through the 8.6a seam. *Deps:* PL8 [x]; 8.6a [x] for the seam.
- [x] **8.7 Ropes** — VtMB's overhead cables. The exporter's `write_ropes` resolves each
  `move_rope`/`keyframe_rope` chain (topologically — `move_rope` is the start, not `keyframe_rope`
  as `entity_visuals.md` had it; `NextKey` binds **first-match by entity order** — the engine's
  `FindEntityByName`, RE-confirmed against the stock `CRopeKeyframe` in `vampire.dll` — which keeps
  each of `sp_tutorial_1`'s two reused `tele4..tele9` installations local) into per-segment
  `<map>.ropes` lines + decodes the `RopeMaterial`
  texture; the runtime's `AElysiumMapActor::BuildRopes` stands one Verlet `UCableComponent` per
  segment (`CableLength` = the sidecar's RE'd rest length, MID off `M_World_Opaque`), start pinned
  and end pinned unless `Dangling`. Rest length is *not* `span + Slack`: half the computation lives
  in `client.dll`, where `RecomputeSprings` applies `Slack` a second time, subtracts a flat 100
  units and integer-divides — so most ropes sit at or below their straight span and hang taut
  (`decisions.md` 2026-07-25 cont. 2). Node count is VtMB's `m_nSegments`, which `CRopeKeyframe::KeyValue`
  derives from **`Type`** (0 → 10, 1 → 4, else → 2, clamped `[2, 10]`) — **not** `Subdiv`, which is
  client-side render tessellation; a `Type 2` rope is one span and cannot sag, which is 25% of the
  game's rope nodes (`decisions.md` 2026-07-25).
  Stock `CableComponent` plugin enabled (`decisions.md` 2026-07-24). `elysium.Ropes` A/Bs.
  *Verified:* build + `test.bat` green (full suite); export audited against the entity lump on all
  seven exported maps — endpoints match the BSP 1:1, first-wins `NextKey` binding agrees with a
  nearest-position heuristic on every segment, and a 108-map sweep finds no rope node inside any
  `sky_camera` room. The tutorial loads **70 cables** (72 chain links − 2 coincident-node artifacts
  dropped), placement verified in-game by MCP screenshot after fixing `UCableComponent`'s
  unset-`AttachEndTo` root-component fallback (`decisions.md` 2026-07-25 cont.); sag depth verified
  in-game after RE'ing the client half of the rest-length computation — chophouse chains hang
  vertical with a short loop at the hook, street wires droop gently pole-to-pole. *Open:* the shape
  reproduces VtMB's own arithmetic exactly but has **not** been put side by side with the running
  original; `Subdiv` (client-side render tessellation) has nowhere to go on `UCableComponent`, so a
  short high-slack link is drawn as a `nodes − 1` polyline where VtMB draws a Catmull-Rom spline.
  As-built: `roadmap-archive.md` 8.7. *Deps:* none.
- [ ] **8.8 Sign / popup panels on the UI foundation** — 4.10's Canvas panel re-drawn on 8.6's
  stack. The **authored layout is honoured as proportion and grouping** (block rects, ordering,
  emphasis) and re-set with vector type on the resolution-independent layout — the `CSignUI`
  1024×768 uniform-scale canvas model (`decisions.md` 2026-07-23) stays the *reference* for what
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
  comes from the PL3 atlas, upscaled under the presentation test. It reads **`FElysiumViewState`**
  (**11.8**), not the substrate — the blood/health/frenzy/masquerade meters come off the player
  entity's sheet (**11.4**). *Deps:* 8.6, 4.4, 4.10, 11.8.
- [ ] **8.10 Accessibility & options backing** *(`remaster-direction.md` axis 4 — additive only;
  changes what the player can configure and perceive, never what the game does)* — full
  key/button remapping + gamepad navigation across the 8.6 component set; UI text scaling;
  subtitle size/background controls; colourblind-safe status colours + a high-contrast option;
  FOV control; real graphics/audio options screens backing settings the engine already exposes.
  Difficulty and balance are **not** in scope here — those are the logic layer. The remapping
  screen is the **10.6g** carve-out: it drives `UElysiumInputUserSettings` —
  `QueryMapKeyInActiveContextSet` for conflicts, `MapPlayerKey` per slot, `ResetAllPlayerKeysInRow`
  for Use Defaults — over three columns (Key/Button, Alternate, Gamepad) and filters its key
  selector against `FElysiumReservedKeys` (`input-architecture.md`).
  *Deps:* 8.6, 10.6 (input path built).

**Slice acceptance** *(M5 criterion)*: New Game starts from a real, modern menu that is legible
and correctly proportioned from 1080p to 4K and at 21:9; the HUD and the tutorial's popup signs
draw on the same stack; NPCs stand in the world at their entity origins.

## P9 — Dialogue & persistence *(design: `game_runtime.md`, `rebuild-strategy.md` B7/B9)*

- [x] **9.1 `.dlg` parser + dlgexpr** — `ElysiumDlg.{h,cpp}`: the 13-field CRLF/Latin-1 parser
  (`FElysiumDlgFile`, 14-field `kiki.dlg` tolerance), the `dlgexpr` front-normalizer
  (`ElysiumDlgExpr` — skill-checks → `CalcFeat(...) >=`, condition `&`/`|` → `and`/`or`, action `&` →
  `;`) over the 5.2 evaluator/installed host, and the host-agnostic branch machine
  (`FElysiumDlgConversation`). **error-to-false** rides the host (RE3). NPC col-4 = action, PC col-4 =
  gate (resolved by data — `decisions.md` 2026-07-24). *Verified:* unit tests (parse / normalize /
  branch) + a Content sweep over **all 25 NPC dialogues of the test-bench maps** (10,949 rows, every
  one parsed + walked, 0 dangling links) + the jack_tutorial beat to `G.Tut_Jack=1`/END. As-built:
  `roadmap-archive.md` 9.1. *Deps:* 5.2.
- [ ] **9.2 Conversation UI + audio-by-path** — dialogue screen on the 8.6 UI foundation, line
  audio via 6.2. Content is **reproduced verbatim** (lines, conditions, branch structure,
  ordering); presentation modernizes — vector type, reflowing line lists, speaker/emotion cues,
  the 8.10 subtitle path. *Deps:* 9.1, 6.2, 8.6.
- [x] **9.3a Level-script wiring — CPython is the default host + auto-load at map load** —
  `MakePreferredScriptHost` installs the CPython host when the VM actually starts (else it falls
  back to expr with a warning — a dead VM is indistinguishable from error-to-false); the map's
  `worldspawn.levelscript` imports **before the spawn pass** (VtMB's own order), host swaps
  re-import, and `EvalScript`/the verbs route through the installed host. *Verified in the built
  game:* `tutorial` imports, `elysium.eval cCelerity` = 8, `G.Tutorial_Discflags |= cCelerity`
  flips `G` to 8, travel re-imports per map. *Deps:* 5.5.
- [ ] **9.3 Level-script execution** — the remainder of the 5.5 decision. **B2 landed the core**
  (`Entity.__getattr__`/`__setattr__` over the P1 class-chain tables, the `Player` object, all 11
  module globals as real C bindings, `G`'s mapping protocol, and the `__main__` namespace). The
  **unblocked entity-manipulation surface now landed** too; the remainder is delegated to its
  owning systems (below).
  - [x] **`Entity`'s four writers are real** — runtime `Origin`/`Angles` + body-follow hooks
    move/re-face/re-skin the NPC body; `SetModel` rebuilds the visual; `SetName` re-keys the name
    index (`FElysiumEntityWorld::RenameEntity`). Full record: `decisions.md` 2026-07-24.
  - [x] **`CreateEntityNoSpawn` + `CallEntitySpawn` are real** — `SpawnRuntimeEntity` split into a
    guarded two-phase pair so scripts can set model/name/origin before `Spawn()`. **Caveat:** a
    leafless class (`item_*`, `prop_*`) yields a logic-valid but **bodiless** entity until a
    per-entity prop/item render path lands; NPCs get visible bodies.
  - [x] **Field-table audit** *(surfaced by B2)* — one genuine script-read gap: **`times_talked`**,
    registered read-only on the NPC leaf (resolves to 0 until **B4**'s runner drives it); the other
    script writes are `ccmd.*` (→ **9.3b**), `.skin` on bodiless props (harmless), and
    `pc.generation` (→ **9.4**).
  - **Character-method fill — delegated to backing systems.** The 22 unbacked methods stay
    fail-closed stubs because their backing does not exist yet, and inventing it is not 9.3's remit:
    **inventory** (`HasItem`/`GiveItem`/`RemoveItem`/`HasWeaponEquipped`/`GiveAmmo`/`AmmoCount`, ~280
    corpus calls — the single largest demand) is a tracked follow-up; **feats/stats** (`CalcFeat`/
    `BumpStat`/`GetMasqueradeLevel`) are **9.4**; **disposition/camera/barter** are B-track / later.
    `SquadSeesPlayer` stays a stub (called by no shipped script). **`OneOfSet` is not a `vamputil`
    helper** — it is a real module-table global, defined nowhere in the corpus and called 589 times
    exclusively from dialogue, so its hardcoded-false stub silently fails 589 gates closed; **9.7**
    RE'd it and owns the fix.
  - [x] **`vamputil` for real** *(9.3b)* — with `ccmd`/`cvar` bound (its top-level `c = __main__.ccmd`
    no longer throws), the real `vamputil.py` imports: its `zvtool` DAG, `fileutil`, and the 46 helpers
    (`IsClan`/`IsIdling`/`RandomLine`/`unhidePlus`/`setPlus`/…) load, and `tutorial`'s `from vamputil
    import *` merges them into `__main__`. One shim was needed: `vampire` binds a mutable **`Character`**
    class the patch monkeypatches (`Character.Near = _Near`); ours is a compatibility stub (the 24
    Character methods still dispatch off the Entity/Player getattro), see `decisions.md`. The four
    `from vamputil import RandomLine` maps (`santamonica`, `chinatown`, `gallery`, `fusyndicate`) now
    resolve that name. *Verified live via MCP.*
  *Deps:* 9.3a, B2. *Remaining blocked on:* the inventory follow-up, 9.4.
- [x] **9.3b Console bridge — `ccmd` + the `cfg` alias table** *(the fifth scripting surface)* —
  `vampire.ccmd` (attribute-set = execute) and `vampire.cvar`, backed by a host-agnostic
  `FElysiumConsole` alias/cvar store seeded from a **PL5d** `out/cfg` mirror, with the
  console→Python fallthrough (`Python → alias → Python`). Binding `ccmd`/`cvar` lets the **real
  `vamputil.py` import** (its top-level `c = __main__.ccmd`); the VM's file layer (**9.3c**) serves
  `cfg/config.cfg` out of the mirror so `FixKeyBindings` resolves and `setPlus` reaches its Tutorial
  branch. This is the Unofficial Patch's Basic/Plus switch —
  `user.cfg`'s `alias patchtype "setPlus()"`, named nowhere in the `.py`/`.ents`/`.dlg`/`.bsp`
  trees. *Verified:* fresh New Game runs `unhidePlus()` → `c.patchtype=""` → `setPlus()` →
  `trig_popup_move.Enable()`. Full record + the `Character`-shim divergence: `roadmap-archive.md`
  9.3b, `decisions.md` 2026-07-24. *Deps:* 9.3.
- [x] **9.3c The script filesystem** — `FElysiumScriptFS`: the VM gets a filesystem namespace of its
  own instead of a redirected `getcwd`, because VtMB's scripts spell paths three ways and two of them
  hand a relative path to the *process* cwd — which belongs to the engine (`FPaths::EngineDir()` is
  the literal `"../../../Engine/"`; UE sets the cwd to BaseDir at startup so it resolves, and guards
  the setter with `DISABLE_CWD_CHANGES`). The `FS_SHIM` bootstrap wraps `__builtin__.open` + the `nt`
  surface to rewrite every path through `vampire._fs_resolve`; **reads** union the `Saved/` overlay
  over the `out/` mirror (`cfg/`, `vdata/`, `vdata/signs/`→`signs`, `python/`→`scripts`, `dlg/`,
  `sound/`), **writes** always land in the overlay with `a`/`r+` copy-up, and escaping the sandbox is
  the one denial. `sys.moddir` returns to the shipped `"Vampire"`, so `fileutil`'s write guard passes
  as authored. *Verified live (built game, `sp_tutorial_1`):* all three path styles resolve, the
  `haven_pc.txt` read-modify-write reads back its own edit **with the mirror untouched**, copy-up
  preserves 423 B of `autoexec.cfg` under an append, `nt.listdir` returns the mirror's `cfg/`, an
  escape raises `IOError` from `open` and `nt.error` from `nt.stat`, and the `.lip` dialogue probes
  log as a named divergence. Unit tier: `Elysium.Substrate.ScriptFS`. Full record + limits:
  `decisions.md` 2026-07-26; VtMB facts: `docs/python_bridge.md` → "The script file layer".
  *Deps:* 9.3b.
- [~] **9.7 The script→engine action surface — survey, RE, spec** *(reference: `docs/script_api.md`)* —
  the demand-side ledger `python_bridge.md` never had: which names the shipped content actually
  calls, what each does in `vampire.dll`, and which system owes it. Orders every task below it.
  - [x] **a. The survey** — `tools/script_api_survey.py` over `out/scripts` (36 `.py`),
    `out/dlg` (147 files / 50,393 rows / cols 4+5) and the exported field-6 payloads, folding
    module-level aliases (1,911 calls hide behind `Find = __main__.FindEntityByName`) and
    cross-checking `ElysiumScriptNatives.cpp`, the CPython host's `PyMethodDef` tables and every
    `D.Input(TEXT("…"))`. **16,438 call sites / 1,287 names; 47 natives bound (24 real / 23 stub);
    124 names / 1,212 calls resolved to nothing.**
  - [x] **b. Ghidra recovery** — new `DumpPyMethods.java` (walks a `PyMethodDef` table: names,
    `ml_doc`, thunk→body, decompiled bodies) + `parse_datamap_builder.py` (replays a builder's
    assignments, because an image read misses every builder-written record). All **six** method
    tables dumped, and the three datamaps behind the unresolved names recovered —
    `CBaseCombatCharacter` (25 inputs), `CAI_BaseNPC` (`SetRelationship` + 16 outputs incl.
    **`OnFedUponBegin`/`OnFedUponEnd`**, which **B6** needs), the player class (11 inputs).
  - [x] **c. The spec** — `docs/script_api.md`: per-name signature, arg types off `fieldType`,
    owning table/datamap, handler address, call count, owning task, and the demand-ranked build
    order. `python_bridge.md` keeps the mechanism and links to it.
  - [ ] **d. Land the zero-dependency wins** — **`OneOfSet`** is
    `(<engine roll> % count) == which - 1`, a 1-based one-of-N selector; its hardcoded-`false`
    stub fails **589 dialogue gates** closed today. Also the `Whisper`/`FrenzyTrigger`
    receiver split (a `vamputil` helper shadows an engine input name; bare vs `pc.`-qualified
    must not collapse to one implementation). *Acceptance:* a `OneOfSet` gate selects one of N in
    the built game; the shadowed names resolve per call site. *Deps:* none.
  **Findings that correct existing docs:** `OneOfSet` is a real module global, not a `vamputil`
  helper (9.3's note fixed); six `ml_doc` strings are copy-paste errors; `GiveItem` exists both as
  a Character method and as a player datamap input; **`AwardExperience` takes a STRING**, not an
  amount (constrains 9.4); `HungerCheck` and `FrenzyCheck` share one handler; the file-like table
  is the `IRestore` adapter, closing `python_bridge.md`'s only open item. *Deps:* 9.3, B2.
- [ ] **9.8 Inventory & items** — **853 corpus calls**, the largest gap with no owning task:
  `HasItem` 327 / `RemoveItem` 182 / `GiveItem` 126 / `StartBarter` 108 / `AmmoCount` /
  `GiveAmmo` / `HasWeaponEquipped`, plus the `Inventory_Remove` input and
  `SpawnItemInContainer`/`AddEntityToContainer`. String-keyed against `vdata/items/` (244 files,
  on disk since PL5b); the receiver is the combat character at `+0x9c` (`script_api.md`).
  `StartBarter` lags the rest — it needs the barter UI (8.6). *Deps:* 9.7c, 9.4.
- [ ] **9.9 NPC disposition & reactions** — the single largest engine demand in the game,
  **2,862 calls**: `SetDisposition(name, level)` alone is 2,510, 2,467 of them in `.dlg` column 4
  (an NPC line's *action*), plus `SetRelationship` 334 on `CAI_BaseNPC` and
  `React`/`SetExpression`/`SetGesture`. Needs `vdata/dispositiontable` + `reaction*` and an NPC
  emotional-state model; the dialogue runner (B4) is the caller. *Deps:* 9.7c, B4.
- [ ] **9.10 Economy** — **250 calls**: `MoneyAdd`/`MoneyRemove` (INTEGER inputs on the combat
  character) + `CurrentMoney`/`SetMoney`. The smallest self-contained system on the ledger; one
  integer on the sheet plus vendor `worth` when 9.8 lands. *Deps:* 9.7c.
- [ ] **9.4 Quests/XP + RPG sheet data** — quest map is live since 1.1; the `vdata/` rulebook is
  on disk (**PL5b [x]**, `out/vdata/`); load `system/stats/feats/traiteffects/rules` into the
  sheet, `quests_*` + `experience_table` for XP. Table→system map: `docs/vdata-catalog.md`.
  9.7 sized and constrained this lane: the sheet-counter demand is **290 calls**
  (`AwardExperience` 77 / `HumanityAdd` 69 / `CalcFeat` 53 / `ChangeMasqueradeLevel` 44 /
  `Bloodloss` / `BumpStat` / `GetMasqueradeLevel`), the counters are INTEGER datamap inputs on the
  combat character, and **`AwardExperience` takes a STRING** — it names an experience-table entry,
  so it cannot be modelled as an integer add (`script_api.md`). The sheet's home already exists
  (11.4): `FElysiumSheet` on `FElysiumCombatCharacter` (live) + `FElysiumPlayerRecord` (durable).
  What 9.4 adds is the loaded data — and it turns the names VtMB's own datamap carries into
  registered fields, shrinking `GetDynamicField`'s bag as it goes. Health is part of that: its
  ceiling is the stated interim `ElysiumInterimPlayerMaxHealth` until Stamina derives it.
  Chargen lands here too — clan, **name**, sex, history, attribute/ability/discipline spends —
  as the screen behind `ccmd.createplayer` (a registered 11.6 command) that VtMB's own
  `sp_genesisdevice_1` already fires (`game_runtime.md` §4); `sp_genesisdevice_1` joins the
  export/bake set so genesis is **played, not skipped** (PP1). *Acceptance (PP1):* New Game
  walks genesis from real input, the created character lands on the player entity, and
  `pc.clan`/`pc.strength` read back from Python. *Deps:* 1.1, 9.7c, 11.4, 11.6.
- [ ] **9.5 Save/load** *(design: `save-architecture.md`; built as **11.9**)* — the four blocks
  (Session / Player / Maps / World) over the R2 field walk, the per-map snapshot lifecycle with the
  absent-entity set, the event queue incl. deferred script strings, think times, `G` + morgue, and
  owned RNG streams, inside a `UElysiumSaveGame` shell over a versioned compressed payload.
  *Deps:* 1.4, 5.4, **11.4** (the player and its inventory are entities — persistence is entity
  persistence, `savegame_format.md`).
- [ ] **9.6 Dice resolver** *(RE5 [x])* — **mechanic verified** by decompiling the full roll
  cluster (ctor `FUN_101d88b0`, roller `FUN_101d8b40`, `vroll` handler `0x100d7040`, RNG/table
  path, loader `FUN_101d92b0`) **plus reading `vdata/system/DiceRolls.txt`** — no running game
  needed (the golden-test premise was void: the face distribution is data-driven by that file).
  `recovered/dice-system.md` is now canonical. `DiceRolls.txt` is on disk (**PL5b [x]**,
  `out/vdata/system/`). **Remaining build work:** the C++ resolver loading its
  `TableWeightings`/`HealthModifiers` (shipped tables are uniform d10, so `rng(0..9)` matches
  today). *Deps:* none.

**Slice acceptance** *(M6 criterion)*: `sp_tutorial_1` is completable as in retail —
dialogue, scripted flow, quests, save/load included.

## P10 — Scale & ship-shape *(ongoing after P4)*

- [ ] **10.1 Horizontal scale-out** — export all ~100 maps; per-map light calibration
  (`probe_light_calibration.py` / `.lightfit`; was L5.2); fix decoder edge cases as maps
  surface them. Calibration is now in **absolute units** (RE15/C4:
  `stored luxel = 255·intensity/falloff`) and only meaningful on Troika's 81 retail bakes
  (27 of 108 are the patch compiler's — provenance-gate first); extending C4's two-map
  absolute sweep across the other ~15 retail sky-pair maps rides along here.
  *Deps:* P4 done (travel), P3 done (calibration meaningful).
- [ ] **10.2 Perf deepening** — Lumen tuning ladder (was L4.1), light culling/max-influence
  cap (was L4.2), scale-up scalability tier for 4070+ (was L4.3). *Deps:* 0.1, P3.
- [ ] **10.3 Floor validation `[needs 3060]`** *(was L5.3)* — 1080p/60 on a real RTX
  3060-class card; the one gate look can't judge. *Deps:* P3, 10.2.
- [ ] **10.4 Async travel state machine** — `map-architecture.md` design (fade → unload →
  task-thread parse → spawn → fade in), built on the 10.8 OpenLevel foundation (the heavy
  build runs in the shell world's `BeginPlay` behind a loading screen); **trigger: when
  synchronous hitches start to matter, not before.** *Deps:* 4.6, 10.8.
- [ ] **10.5 Packaged-build content path** — `content/` next to the exe, packaging story,
  Shipping config sweep (debug layer compiled out), and the **`GameInputRedist.msi`** prerequisite
  10.6e introduces (Windows 10 19H1 floor). The gitignored `/ElysiumBaked` mount joins this
  story (0.9): a package must either ship a bake-on-first-run path or the user-side bake
  tooling. *Deps:* none until first package.
- [ ] **10.6 Input path — Enhanced Input, remapping, first-party gamepad** *(design:
  `input-architecture.md`; VtMB facts: `controls.md`)* — retire the legacy `DefaultInput.ini`
  axis/action block for the four-plane model: **Enhanced Input is the driver, the VtMB console
  command string stays the action's identity.** One `UInputAction` per bindable command from the
  `kb_act.lst` inventory, `UPlayerMappableKeySettings.Name` = a stable id, the command string
  carried beside it and executed through `FElysiumConsole` on `Started`/`Completed` (so the
  patch's *aliases* bind exactly like compiled verbs, and `-ExecCmds`/MCP/level scripts can fire
  any player action by name). Movement/look stay first-class analog actions with per-device
  modifier stacks. `EPlayerMappableKeySlot` First/Second/Third = VtMB's Key/Alternate + Gamepad.
  Sub-steps, in build order:
  - **a. Action table + generator** — hand-authored `Config/ElysiumInputActions.csv` (the
    committed spec; **not** generated from the user's `kb_act.lst`, which is game-derived) →
    `IA_*`/`IMC_*` assets emitted by `tools/build_content.py`, so `content.bat` keeps them in
    lockstep.
  - **b. `IMC_Player_KBM` + analog actions + `UElysiumInputRouter`** — retires the legacy
    mappings and `bEnableLegacyInputScales`; contexts replace VtMB's `CClientMode*` split
    (`IMC_Dialogue`/`_Menu`/`_Cinematic`), with `bIgnoreAllPressedKeysUntilRelease` settling the
    held-input-into-conversation question on our side of the port.
  - **c. Reserved keys** — console back to `` ` `` (VtMB's own `toggleconsole` key; frees F10 for
    `snapshot`), Cog's shell shortcuts to `Ctrl+F1`–`Ctrl+F4`, all other dev keys on
    `BindDebugKey`. Enforced by a **Substrate-tier test** over every generated IMC, not by
    convention; `elysium.input.ReserveDebugKeys 0` A/Bs it in dev builds.
  - **d. `UElysiumMouseSensitivity` modifier** — reads `sensitivity`/`m_pitch`/`m_yaw`/`m_filter`
    off `FElysiumConsole` for VtMB's 0.066°/count; the options slider writes the cvar.
  - **e. `GameInputWindows` + PS device configs + `IMC_Player_Gamepad`** — Xbox needs no config;
    DS4/DualSense get `FGameInputDeviceConfiguration` entries (VID `054C`) mapping onto standard
    `Gamepad_*` keys plus an overridden hardware-device id for glyph swapping. `GameInputRedist.msi`
    joins 10.5's packaging story. Adaptive triggers/haptics deferred.
  - **f. `UElysiumInputUserSettings` + `config.cfg` projection** — the key profile is
    authoritative; `FElysiumConfigWriter` emits Valve-format text into `out/cfg/config.cfg` so
    `vamputil.py`'s `FixKeyBindings` reads a faithful view (one-way; imported once on first run).
    Rebinding works headlessly before any UI exists.
  - **g. Remapping screen** — lands with **8.10** on the 8.6 stack, not here.

  **Acceptance:** the tutorial is playable start to finish on keyboard+mouse and on an Xbox *and*
  a DualSense pad with no third-party driver; every action rebindable to primary/alternate/gamepad
  and surviving a restart; the reserved-key test green. Defaults are the **Patch 11.5** set
  (`decisions.md` 2026-07-25). *Deps:* 9.3b (the console bus); **11.5** (the input scope stack the
  contexts are pushed through) and **11.6** (the command registry every action's string resolves
  against, and the `FElysiumUserCmd` the analog actions fill); 8.6/8.10 for the screen only.
- [P] **10.7 Long tail** *(post-tutorial; promote to tasks when reached — **promoted 2026-07-26:**
  stealth → **13.1**, disciplines → **13.2**, weapons/combat basics → **13.3**, chargen → **9.4**,
  choreography → **P12**)* — full combat AI (beyond 13.3's basics); real NPC AI (runtime NavMesh + BT/StateTree
  replacing `info_node`); ragdoll/IK/anim blends; MetaSounds; `.emc`-style cache for `.ents` if
  parse time bites; lump-8 lighting bake as a low-end contingency (parked with the dynamic-path
  commitment); retail `.sav` import (needs RE7 wire format — currently a non-goal). For the
  low-end contingency, **Lumen Lite** (5.8's medium-quality irradiance-field GI, ~2× faster,
  runs on PC) is noted as a cheaper alternative to a lump-8 bake path — see Options.
  **vdata-driven gameplay systems** — data already on disk (PL5b, `out/vdata/`); each table's
  consumer + schema is mapped in `docs/vdata-catalog.md`, and these are the systems that read
  them: **disciplines/vampire powers** (`disciplinetgt_*`, ~300 KB — the largest; → **13.2**),
  **stealth** (`stealth`/`stealthkillrules`; → **13.1**), the **hacking minigame** (`hackterminals/`),
  **economy/vendors** (`vendors`, item `worth`), **NPC disposition + reactions**
  (`dispositiontable`/`reaction*`), **data-driven conversation camera** (`camerashots/`),
  **radio + TV-news ambient content** (`radio_data`/`newscaster_*` — only 6.2's audio decode
  exists), **impact FX** (`particleimpacttable`), **per-category entity sound schemes + volume**
  (`sndscheme_*`/`sound_volume_table`, distinct from PL5a's map SoundSchemes), and the **minor UI
  content tables** (`loadingtips`/`infobartypes`/`mapnames_localized`/`keynames`/
  `interestingplacetypelist`). Promote any to its own task when reached.
- [x] **10.8 OpenLevel map-lifecycle migration** — map change is UE5 hard travel: `Travel` stows
  the target map + landmark in the GI-scoped `PendingMapLoad` and `OpenLevel`s the one reused shell
  `.umap` (on cold boot, already in the shell, it spawns the map directly — no redundant re-open);
  the fresh world's game mode calls `SpawnPendingMap`, whose `AElysiumMapActor` reads the target from
  GI state and builds in code on `BeginPlay`. Retired the per-travel `ForceGarbageCollection(true)`,
  `FlushAll`-on-travel, the `RequestLandmarkTravel` next-tick defer (`OpenLevel` self-defers teardown
  to end of frame, so it's safe mid entity-tick), and the `IsPlayerSeated` gate; kept the
  `Travel`/`RequestLandmarkTravel` seam + `NextLandmarkSpawn` carry-over. The texture cache was also
  re-scoped from a process-wide strong-ref table to a **per-map instance owned by the map actor**, so
  GC frees it with the world — the whole point of handing teardown to the engine. Cross-map state
  verified GI-scoped (map/game-state/audio subsystems, CPython VM — the script host resolves the live
  entity world each call, so the persistent VM tracks the fresh world). *Verified:* clean build,
  Substrate tests green. Owner call + as-built: `decisions.md` 2026-07-24. As-built detail:
  `roadmap-archive.md`. *Deps:* 4.6.

## P11 — Runtime spine *(design: `runtime-architecture.md` + `save-architecture.md` — read them; steps here are the tracker)*

The structure *between* the systems P1–P10 design: lifetimes, the frame, the player object, the
session, and the seams. It exists because the slice ladder now reaches "boot a New Game and play it",
and that is the one thing no current doc owns. Its rules are **S1–S10** (`runtime-architecture.md`
§13), orthogonal to `engine-core.md`'s R1–R8.

Steps are ordered so each compiles, ships and is observable alone. **11.4 was the hinge** — 9.4, 9.5,
9.8, 9.9 and 9.10 all sit on it, and it landed before any of them.

- [x] **11.0 Adopt the spine** — all seven `runtime-architecture.md` §16 owner calls recorded as
  one dated entry (`decisions.md` 2026-07-26 cont. 4), with the amendments from a four-way
  verification sweep (source state, VtMB-facts docs, design docs, tracker) folded in: the
  four-map chain named in full, health as an entity field, the dilation single-application rule,
  the input scope stack over CommonUI's action router. Both design docs flipped to adopted; new
  RE opened (RE21 usercmd order, RE22 crouch hull). As-built: `roadmap-archive.md` 11.0.
  *Deps:* none.
- [x] **11.1 Frame + clock ownership** *(S1, S2)* — the tick table pinned with tick groups and
  prerequisites (`dumpticks` shows PC → map gameplay tick → movement → physics → map post-move tick);
  `AElysiumMapActor` split into a `TG_PrePhysics` gameplay tick and a `TG_PostPhysics` post-move tick
  that owns the `+use` cursor; `FElysiumTimeControl` as the one pause/scale facade, with
  `FElysiumGameClock`'s writers made private to it so the single advance site is a compile-time
  property. `elysium.timescale` / `.pause` / `.step`. As-built: `roadmap-archive.md` 11.1.
  *Deps:* none.
- [x] **11.2 World services** *(the substrate's outbound seam)* — `FElysiumWorldServices`
  (`IElysiumEmbodiment`/`IElysiumAudio`/`IElysiumTravel`/`IElysiumPresenter`) injected into
  `FElysiumEntityWorld` at construction; `AElysiumMapActor` implements the first three, and every
  `Cast<AElysiumMapActor>` and `GetWorld()->GetFirstPlayerController()` under the substrate is gone.
  `IElysiumPresenter` has **no production implementation until 11.8** — the fade/sign/dialogue state
  stays on the world for `AElysiumHUD` to poll and the seam is announce-only, non-null in a test.
  A recording stub (`Private/Tests/ElysiumTestServices.h`) implements all four.
  *Acceptance met:* `Elysium.Substrate.WorldServices` runs a tutorial-shaped `logic_auto` chain end to
  end against the stub with no RHI, no actors and no `tools/out`, then re-runs the same defs with a
  null bundle and reaches the same counter — the seam's actual claim. As-built:
  `roadmap-archive.md` 11.2. *Deps:* 1.4.
- [x] **11.3 App state machine + pause + loading + game over** — `UElysiumGameFlowSubsystem` owning
  `EElysiumAppState` (Boot/FrontEnd/Loading/Playing/Paused/GameOver) with its transition table as
  plain C++ (`ElysiumAppState.h`), plus `BootFromCommandLine`/`NotifyWorldReady`/`NewGame`/`LoadGame`/
  `SaveGame`/`QuitToMenu`/`ReloadMap`/`SetPaused`/`TriggerGameOver` and `OnAppStateChanged`.
  `AElysiumGameMode::BeginPlay` is one `NotifyWorldReady(this)`; the boot decision, the backdrop
  camera and New Game moved off the game mode and the map subsystem. **The screen is a pure function
  of the state** (`ApplyMenuForState`), which is also what makes the raw `elysium.map`/`.reload`
  verbs correct — the Loading transition their OpenLevel raises takes a stale menu off the dying
  world. The loading screen is the engine movie player hooked at `OnPrepareLoadingScreen` (not
  `PreLoadMap` — the movie player binds that itself at engine init, ahead of any GI subsystem). Esc
  on the player controller (`bExecuteWhenPaused`) opens the pause menu; the menu's own
  `NativeOnKeyDown` closes it. `FElysiumNewGameRequest` carries the retail chain as data with
  `elysium.SkipIntro` as the entry-point switch. New verbs: `elysium.appstate`, `.pausemenu`,
  `.quittomenu`, `.gameover`, `.newgame` (moved), `.SkipIntro`, `.LoadingScreen[MinTime]`; MCP
  reports `app_state`. **Remaining:** `LoadGame`/`SaveGame` are logged seams until 11.9; the
  game-over screen's copy is invented (VtMB's own death screen is un-RE'd); the loading screen
  covers the level-load flush only — the map actor's build pass after it is 10.4's. *Acceptance met:*
  Esc pauses and holds the world (clock frozen; `FrontEnd` deliberately does not — verified a no-op),
  quit-to-menu returns to the backdrop with `G`/quests/sheet/clock cleared, travel plays the loading
  screen (`LogMoviePlayer` PlayMovie→PostLoadMap), boot is decided once at GI init.
  `Elysium.Substrate.AppState` asserts the whole transition table. As-built:
  `roadmap-archive.md`. *Deps:* 11.1, 8.6.
- [x] **11.4 The player entity** *(S3 — the hinge)* — `FElysiumAnimating` + `FElysiumCombatCharacter`
  as real registry chain nodes matching VtMB's datamap chain, `FElysiumPlayer` under them (classname
  `player`, targetname **`!player`** — which is what the maps themselves write, so 48 of the 49
  `point_teleport.target` keys became ordinary name resolves), and `FElysiumPlayerRecord` (session
  lifetime, on `UElysiumGameStateSubsystem`) hydrating at map build and dehydrating at world
  teardown. `FElysiumNpc` re-based onto the same two nodes, losing its duplicated body/animation
  half. The pawn demoted to a body: `+use`, sign dismissal and the skybox toggle moved to
  `AElysiumPlayerController`, the shift-gait poll became a `+speed`/`-speed` latch, and it now
  carries the handle of the entity it embodies. `FindPlayer()` and `pc` return an ordinary `Entity`
  (re-bound per eval, since a handle is generation-checked); **`vampire.Player` is retired**; the 25
  `CBaseCombatCharacter` + the 10 recovered player inputs register on the chain, eight of them
  backed by real fields and the rest logging their owning task. A `GetDynamicField` hook carries the
  `vdata` half of the sheet (`pc.base_*`) in both hosts until 9.4 names those fields. **The death
  path is now 11.3's missing game-over driver.** *Acceptance met:* on `sp_tutorial_1`,
  `pc.MoneyAdd(50)` and `elysium.ent_fire !player MoneyAdd 50` both land on `money` (0→50→100)
  through the same R2 walk; a trigger the player walks into resolves `!activator` to a real handle;
  `elysium.ent_fire teleport_player Teleport` moves the player through `SetRuntimeOrigin`; the map's
  own `logic_auto` `GiveItem` wires at the player resolve instead of dropping; `trigger_hurt` drains
  the entity's `health` to 0 and the run ends in `GameOver`; money survives a travel and
  quit-to-menu clears the record. `Elysium.Substrate.PlayerEntity` asserts the whole shape headless.
  As-built: `roadmap-archive.md`. *Deps:* 11.0 (call A), 11.2.
- [ ] **11.5 Input scope stack** *(S6)* — `UElysiumInputSubsystem` (LocalPlayer) owning a priority
  stack of `FElysiumInputScope` (mode, cursor, mapping contexts, pauses-game); menus, dialogue, signs,
  cinematics, chargen and Cog all push/pop instead of setting `FInputMode*` — retiring the three
  independent owners that exist today (`UElysiumUISubsystem`, `AElysiumHUD`, Cog's ImGui capture).
  *Acceptance:* opening any screen over any other restores exactly the mode it found; a Substrate test
  asserts the stack is balanced across every screen transition; Cog can never eat a menu click.
  *Deps:* 11.3.
- [ ] **11.6 Command registry + user command** *(S5, S7)* — `FElysiumCommands` (every VtMB
  bindable verb registered by name, `+`/`-` pairs included) with `FElysiumConsole::Execute`'s
  precedence stated and tested (**command → alias → cvar → Python**); `FElysiumUserCmd` filled by the
  router and consumed by movement, camera and the bus, so nothing polls a key; `AElysiumPawn` re-based
  to `APawn` with a **box** collision component and a `UElysiumMovementComponent` shell
  (`source_movement.md`: `ACharacter` cannot take a box root, and the box is a recovered requirement,
  not a preference), the capsule pawn kept behind `elysium.SourceMovement 0`. *Acceptance:* every
  bindable verb fires by name from console, level script, `.dlg` action and MCP; a recorded command
  stream replays identically. *Deps:* 11.1. *Feeds:* 10.6, 4.7.
- [ ] **11.7 Camera component** — `UElysiumCameraComponent` holding VtMB's four weights with
  `AElysiumPawn::CalcCamera` as the single apply point (delegating to `GetCameraView` first), the
  cvar surface reproduced verbatim, and the scripted-shot channel (`SetCamera`, `camera_keyframe`,
  conversation and feed cameras) as one push/pop seam. Design + the recovered solve:
  `camera-view-modes.md`. Sequenced in **PP2** — the scripted-shot channel is the theatre's
  camera. *Acceptance:* its weight-driver automation test passes (0→1 in 0.5 s, time-scaled,
  symmetric resume mid-blend); `togglecamera` works from a binding; `SetCamera` has a landing
  site. *Deps:* 11.6.
- [ ] **11.8 Presentation seam** *(S8)* — `UElysiumPresentationSubsystem` publishing
  `FElysiumViewState` once per frame plus discrete delegates; `AElysiumHUD`, the dialogue box and the
  8.6 screens re-based onto it. The front-end gating (`IsMenuUp()` checks scattered through the HUD)
  becomes one rule in the publisher. *Acceptance:* no widget references `FElysiumEntityWorld`; the
  HUD renders from a hand-built view state in a test. *Deps:* 11.3. *Feeds:* 8.9, 9.2.
- [ ] **11.9 Save/load** *(= 9.5, on this spine)* — `save-architecture.md` in full: the
  `UElysiumSaveGame` shell over a versioned compressed payload, the four blocks, `EElysiumField::Save`
  on the class-chain field tables (enumeration is the R2 walk, matched by name, zero-omitted), the
  per-map snapshot lifecycle with the absent-entity set, the event queue + think times, `G`/morgue,
  owned RNG streams, and the slot/quick/autosave ring. *Acceptance:* save mid-tutorial, quit to menu,
  load, and the beat machine continues; the round-trip digest test is green. *Deps:* 11.4, 5.4.
- [ ] **11.10 Play test tier** *(S10)* — a fourth automation tier that drives a real headless world:
  the beat-script driver (`do`/`wait`/`assert`/`shot` over the command registry, injected input, `G`/
  quest/entity predicates and the 2.9 shot baseline), command-stream replay, the save round-trip, and
  the matching MCP tools (`input_inject`, `beat_run`, `save`/`load`, `time`). *Acceptance:*
  `test.bat Play` walks the tutorial opening unassisted and fails loudly when a beat regresses — P9's
  slice acceptance becomes a CI run rather than a manual play-through. *Deps:* 11.6, 2.7, 2.9.

**Slice acceptance:** from a cold launch — the menu comes up over the backdrop, New Game runs chargen
and enters the story, the tutorial's opening beats play on rebindable controls with a HUD, Esc pauses,
Save and Load round-trip the run, and `test.bat Play` asserts the whole thing headlessly.

## P12 — The theatre: choreography & faces *(the PP2 rung — everything blocks, `decisions.md` 2026-07-26 cont. 5)*

The intro cinematic (`sp_theatre` — embrace + trial) as VtMB plays it: `logic_choreographed_scene`
driving actors, scripted camera (11.7), line audio, subtitles, and facial animation. The fidelity
bar is an owner call: the scene is not done until the faces are alive — **eyes and lipsync
included**. The RE unknowns were front-loaded in "Now" because this is the highest-variance
work on the path, and both halves are now closed: **RE19** (the scene format and event
semantics — `docs/choreographed_scenes.md`), **PL9** (the corpus on disk at `out/scenes/`,
`out/lip/`) and **RE20** (the flex/eyeball chunks, the `.lip` grammar and the
phoneme→controller tables — `docs/facial_animation.md`). **PL10** then baked the faces into
the NPC export — morph targets in each glb, the flex rig in `out/npc/facial/<stem>.json`,
and `out/expressions/`. Every remaining task here is runtime work.

RE20 changes what 12.4 can be: **no model in the install carries eyeball data** — the whole
cast ships `NumEyeballs == 0`, so there is no authored eye pose, look-at cone or procedural
lid. Eyes in VtMB are *eyelids*: eight `eyelid` flex controllers driving 16 eyelid flexdescs
through four RPN rules. Blink and lid shaping are reproducible; gaze is not RE-able because
it was never authored.

- [ ] **12.1 Choreographed scenes** — `logic_choreographed_scene` as a real class + the scene-file
  parser (PL9) + an event timeline on the game clock, `Start`/`Pause`/`Resume`/`Cancel` inputs and
  the seven outputs; actors resolve **by name** and play through the 8.5 anim seam. Spec:
  `docs/choreographed_scenes.md` (RE19) — nine live event types (`speak`, `silence`, `loud`,
  `expression`, `gesture`, `sequence`, `firetrigger`, `python`, `bodysound`), absolute scene time
  offset by the audio mixahead, `position_start`/`position_end` actor placement, and
  `firetrigger "N"` → `OnTriggerN`. *Acceptance:* the theatre's first scene runs its actors and
  fires its completion wires in the built game. *Deps:* 8.5, 11.1, RE19 [x], PL9 [x] — a
  `SceneFile` resolves to `out/scenes/` + the path with its `sound/` prefix stripped.
- [ ] **12.2 Scene audio + subtitles** — per-line audio through the 6.2 decode path (the
  `PlayDialogFile` file-resolution rules) synced to scene time; a subtitle surface on the view
  state (11.8). *Acceptance:* the scene's lines are audible and subtitled in sync. *Deps:* 12.1,
  6.2, 11.8.
- [ ] **12.3 Facial flex track** — the morph targets are baked (PL10 [x]); what is left is the
  three layers above them, which are runtime evaluation: 44 flex controllers → 60 RPN flex rules
  → 65 flexdesc weights → the per-flex target ramp → the morph weight. All four inputs are in
  `out/npc/facial/<stem>.json`, index-aligned with the glb's morph targets;
  `UElysiumNpcAnimInstance` grows a morph-track player over the body animation. Two load
  contracts the bake fixes: a morph that spans two materials arrives as one same-named piece per
  primitive, so the skeletal-mesh config must set `MorphTargetsDuplicateStrategy::Merge`, and a
  morph target is one *flex record*, not one flexdesc — the eyelid pairs hinge a single flexdesc
  into two ramps. Spec: `docs/facial_animation.md`. *Acceptance:* a flex authored in the model
  moves the face in-game. *Deps:* 8.5, RE20 [x], PL10 [x].
- [ ] **12.4 Eyelids** *(was "Eyes")* — blink + lid shaping off the eight `eyelid` controllers
  and their four rules (`raiser × (1 − droop·0.8) × (1 − blink)` and its complements). **There is
  no eyeball data to consume** — RE20 found `NumEyeballs == 0` on all 4,444 models, so eye posing
  and look-at have no faithful baseline. *Acceptance:* actors blink and their lids shape through
  the theatre scene. *Open owner call:* whether to add gaze/look-at at all — it is an invention
  under `remaster-direction.md`'s Feel layer, not a reproduction, so it needs a dated
  `decisions.md` entry before it is built. *Deps:* 12.3.
- [ ] **12.5 Lipsync** — `.lip` phoneme tracks (RE20 [x]; 9.3c already logs the scripts' `.lip`
  probes as a named divergence) driving mouth flexes against 12.2's line audio; the 7,136 files
  are on disk in `out/lip/` (PL9 [x]), keyed by the line's own sound path. A **three-file join
  per line**: the `.lip` for phoneme timing, `expressions/<model stem>_phonemes.txt` for the
  phoneme→controller weights (249 tables, chosen by the actor's model basename), and
  `mstudiomouth_t` for the amplitude-driven jaw that runs alongside. Key on the phoneme
  *string* — the `.lip` numeric code is not stable across the corpus. All three inputs are on
  disk: `out/lip/`, `out/expressions/` (the 249 `.txt` tables, PL10 [x]) and `mouths` in
  `out/npc/facial/<stem>.json`. Spec: `docs/facial_animation.md`. *Acceptance:* mouths move
  with the words on every theatre line. *Deps:* 12.2, 12.3.

**Slice acceptance** *(PP2)*: New Game runs genesis, then the full theatre act plays start to
finish — choreography, camera moves, audible subtitled lines, live faces — and hands the player
to the tutorial chain, unassisted, from real input.

## P13 — Tutorial mechanics: stealth, disciplines, firearms *(the PP6 rung; promoted out of 10.7)*

- [ ] **13.1 Stealth** — `vdata/stealth` + `stealthkillrules` loaded; sneak mode (movement +
  posture + the stealth readout on 8.9's stack), NPC detection against it, `trigger_stealth_mod`
  becomes real. *Acceptance:* the tutorial's stealth lesson completes as retail. *Deps:* 9.4,
  4.7, 11.4.
- [ ] **13.2 Disciplines** — activation/deactivation over `vdata/disciplinetgt_*`, blood cost
  through the sheet, timed effects on the one queue (R4), the tutorial's discipline lesson
  (`ClearActiveDisciplines` and friends become real). *Acceptance:* the tutorial's discipline
  lesson completes as retail. *Deps:* 9.4, 11.4.
- [ ] **13.3 Firearms & melee basics** — weapons off `vdata/items/`, equip/holster, the attack
  path through the dice resolver (9.6, `CalcFeat`), damage onto `FElysiumCombatCharacter`, the
  gun-range and melee lessons. Full combat AI stays 10.7. *Acceptance:* the tutorial's range +
  melee lessons complete as retail. *Deps:* 9.8, 9.6, 11.4.

**Slice acceptance** *(PP6 = P9's criterion, mechanised)*: `sp_tutorial_1` is completable as in
retail end to end, and `test.bat Play` proves it headlessly.

## Pipeline backlog (indexed; owned by phases above)

| ID | Task | Needed by |
|---|---|---|
| PL1 | Export `.ents`-referenced models (`prop_dynamic`/`prop_physics`) — `model_mesh` in `.ents` | 8.1 [x] |
| PL2 | Copy loose `.py` → `out/scripts/`, `.dlg` → `out/dlg/` — `UE_extract_scripts.py` | 5.1 [x] |
| PL3 | Use-icon atlas export (72-entry enum) — `UE_use_icons.py` → `out/hud/use_icons.png`+`.json` | 4.4 [x] |
| PL4 | Batch NPC export + include-model resolution — `mdl_skel.resolve_tree`/`local_sequences` (includes@404/408, `StudioModelGroup` stride 116) → shared-bank glbs + `npc_manifest.json` via `npc_export.py`; 45 NPCs / 62 banks / ~410 MB (`decisions.md` 2026-07-24) | 8.5 [x] |
| PL5 | Copy sound schemes (a) [x] + the full `vdata/` rulebook (b) [x] + `vdata/Signs/*.txt` ×278 + the 57 referenced background materials (`hud/signs/*`, `interface/Pop_Ups/*`) → `out/signs/` — `UE_extract_signs.py` (c) [x] | 6.3, 9.4, 4.10 |
| PL5b | Mirror the whole `vdata/` rulebook (`system` 97 + `items` 244 + `camerashots` 66 + `hackterminals` 57 + `precache` 1 = 465) verbatim → `out/vdata/` — `UE_extract_vdata.py`, patch-first, `signs`/`.xls` excluded. Consumer map: `docs/vdata-catalog.md` | 9.4, 9.6, 10.7 [x] |
| PL5d | Copy `cfg/*.cfg` (the alias/cvar tables — `user.cfg` carries the Basic/Plus `patchtype` alias) verbatim → `out/cfg/` — `UE_extract_cfg.py`, patch-first, wired into `export_all.py` (`--no-cfg`) [x] | 9.3b [x] |
| PL6 | Texlight merge in exporter | 3.4 |
| PL11 | Remove the dead Lumen-card path the bake superseded (found by 0.9): `export_all.py`'s `bake_cards`/`--no-cards` calls a `cards.bat` that no longer exists and prints a "skipped" line every run; `ElysiumCardGen.cpp` (`ELYSIUM_WITH_CARDGEN`, `elysium.cards.probe`) still builds into editor targets. Nothing depends on either | 0.9 |
| PL7 | Sidecar space fixes surfaced by the audit — **none (0.4: all sidecars already Unreal cm)** | 0.4 [x] |
| PL9 ✅ | Mirror the choreographed-scene files + `.lip` phoneme files → `out/scenes/`, `out/lip/` — `UE_extract_scenes.py`, patch-first, verbatim, `sound/` prefix stripped so `SceneFile` reads back 1:1; **5,444 `.vcd`** (4.5 MB) + **7,136 `.lip`** (16.8 MB), wired into `export_all.py` (`--no-scenes`) with a `SceneFile` cross-check over the exported `.ents`. The `.lip` *format* stays RE20 [x] | 12.1, 12.5 |
| PL10 ✅ | Facial data in the NPC export — the flex chunks (RE20 [x]) decoded by **`mdl_skel`** (one decoder, now shared with `probe_facial.py`) into glb **morph targets** in each rigged NPC's own glb, plus `out/npc/facial/<stem>.json` carrying what a morph target cannot hold: the 44 controllers, the 60 RPN rules, each morph's four-value ramp and `mstudiomouth_t`. **78 of 101 NPCs rigged, 4,015 morph targets**, +0.33–0.53 MB per rigged glb (manifest v3; the manifest names the sidecar). The unit-vector table comes out of the user's own `StudioRender.dll` at export time (`mdl_skel.read_anorms`; `probe_facial.py --anorms` still dumps it for RE) — game-derived, so regenerated, never committed; without it the export ships meshes and skips faces rather than baking wrong deltas. Two corrections to the plan: the unit is the flex **record**, not the flexdesc (a flexdesc splits into two ramps — 53 targets from 45 flexdescs), and a morph **spans materials**, so it lands as one same-named piece per primitive and the consumer must load with `MorphTargetsDuplicateStrategy::Merge` (12.3; 8.5's loader still takes the default `Ignore`). No eyeball chunk exists to export. `UE_extract_scenes.py` also mirrors the 249 `expressions/*.txt` → `out/expressions/`. Full shape: `docs/facial_animation.md` → "The offline export" | 12.3, 12.4, 12.5 |
| PL8 ✅ | UI source inventory for the re-skin — **`tools/UE_extract_ui.py`** mirrors `out/ui/`: 25 `.res` layouts + **both** schemes byte-for-byte (`VampireScheme` skins client.dll, `TrackerScheme` skins GameUI.dll — `docs/vtmb-ui.md`), 194 localized strings, the 1024×512 title lockup, the menu particle scene (26 scripts → 22 `.tga` sprites) + the 6 `MM_Skybox` faces, and **503 decoded HUD/interface materials**; `--inventory` adds the ~350 item icons. Zero unresolved. Wired into `export_all.py` (`--no-ui`). The `.fnt` atlases are **not** extracted — vector type is `Content/Fonts` via `tools/fetch_ui_fonts.py`. | 8.6 |

## RE backlog (reverse-engineering work; each cited where consumed)

| ID | Question | Consumed by | Status |
|---|---|---|---|
| RE1 | Trigger/button spawnflag filter bits — decoded from the decompile; the maps live in `entity_io.md` (4.2 corrected a `0x100`↔`0x400` touch↔use label swap) | 4.2, 4.5 | [x] |
| RE2 | Retail queue-vs-think service order — **confirmed think-first** (thinks then `ServiceEvents`); our provisional queue-first diverges (see `engine-core.md` Tick note) | 1.4 | [x] |
| RE3 | `__setattr__` write path + error-to-false + `G` default-0 **all confirmed** | 5.2, 9.1 | [x] |
| RE4 | Ghidra datamap export (validate our input/field tables vs retail) — method confirmed, CBaseEntity base map extracted. **4.5 proved the fast path: `run.ps1 -Script DumpGrep` (str=/cls= anchors) over the persisted `vtmb` project, no re-import — recovered every P4.5 class factory/datamap and settled `logic_case_toggle`'s delta-advance divergence.** | 4.5+ (optional, valuable) | [~] |
| RE5 | Dice-system verified by decompilation + `vdata/system/DiceRolls.txt` (data-driven face weightings, shipped tables uniform d10; difficulty is human-scale; `[4]`/`[6]`/`[0xe]` + pool source all confirmed) — no running game needed; `recovered/dice-system.md` canonical | 9.6 | [x] |
| RE6 | `ent_survey` count reconciliation — retail = 16,125 outputs / 1,591 Python (16,214/1,621 was stale) | 0.6 | [x] |
| RE7 | Retail `.sav` block wire format | 10.7 (only for importing retail saves) | [P] |
| RE8 | Re-base `entity_io.md` survey on the patch (engine-loaded) map set — patch 24,081 outputs / 6,956 Python (retail 16,125 / 1,591) | 0.8 | [x] |
| RE9 | Screen-fade flag semantics — the client owns the curve, not `env_fade`; `SF_FADE_STAYOUT` uncovers again, and no `OnEndFade`/`ReverseFade` exists (detail: `decisions.md` 2026-07-23) | B1 | [x] |
| RE10 | **Sky-face orientation (K1)** — `R_DrawSkyBox`/`MakeSkyVec` + three `.rdata` tables: `rt`+X / `lf`−X / `bk`+Y / `ft`−Y, **no face rotated or mirrored**; the draw applies no colour scaling (brightness is material-side → RE16). Full: `sky-ambience.md` → K1; verbatim row: archive | 3.7, SKY B2/B3 | [x] |
| RE11 | **Labelled-sky probe in the shipped engine (RE-A2)** — the original draws all six labelled faces exactly as K1 predicts (five three-face corner agreements, two on `dn`); K1 closed on three independent legs. Full: `sky-ambience.md` → "The in-game check" | SKY B1 | [x] |
| RE12 | **Model lighting + lump 15's runtime role (K3/K5, RE-A3)** — world surfaces render from lump 8 alone; lump 15 feeds only the model light cache (162-ray ambient cube off `avgLightColor` × reflectivity + ≤ 2 direct worldlights); first-wins, never summed. Full: `sky-ambience.md` → K3/K5 | SKY C0–C2, D2/D6 | [x] |
| RE13 | **Day/night bake selection (K4, RE-A4)** — the premise was wrong: `day[8]`/`night[8]` all-zero across 108 maps, no reader in `engine.dll`, one bake keyed by `styles[8]`. Full: `sky-ambience.md` → K4 | SKY C4, D1 | [x] |
| RE14 | **Full-game sky inventory (K8, RE-A7)** — 11 sky sets; 66 maps draw sky, **25 are lit by it, 83 have no `light_environment` at all**; 43 run the miniature at `scale` 16; 1,442 worldlights sit in sky areas. Full: `sky-ambience.md` → "The full-game inventory" | SKY B7/B8, C1/C2 | [x] |
| RE15 | **VRAD's transfer + lump 8's absolute scale (K6, RE-A5)** — `intensity = (colour/255)^2.2·(B/255)·falloff(100u)`, zero exceptions on 16,378 lights; `stored luxel = 255·intensity/falloff`; the sun confirmed at ×1.01; sky ambient resolved globally first-entity-wins; 27/108 bakes are the patch compiler's (provenance-gate everything). Open residue: the skyambient's hemisphere aperture, bounded ~2× and **unidentifiable from this data** (C4). Full: `sky-ambience.md` → K6 | SKY C0–C4, D1/D6, 10.1 | [x] |
| RE16 | **The sky's brightness chain (K7, RE-A9)** — the identity: a sky pixel is the decoded texel, unscaled, `$nofog` game-wide; the one asymmetry is the world's `albedo × lightmap × 2` (overbright pinned to 2). Full: `sky-ambience.md` → K7 | SKY B4/B5/B8, D7 | [x] |
| RE17 | **Owner-run reference captures** *(was sky-ambience RE-A6)* — original-game screenshots at the shared vantages (3–4 sky maps + one sky-only view per skyname), for the **world** half of the display ratio (`albedo × lightmap × 2` beside a sky texel — the sky's own transfer is the identity, RE16) and as 7.8's reference. **Gate:** first settle whether `snapshot` grabs pre- or post-gamma-ramp (the display gamma is a device LUT a back-buffer grab omits) — quantitative use waits on that check | 3.6/3.7, 7.8 | [ ] |
| RE18 | **The script→engine action surface** — the demand ledger (16,438 call sites / 1,287 names) plus the supply side out of `vampire.dll`: all six `PyMethodDef` tables with their `ml_doc` contracts, and the `CBaseCombatCharacter` / `CAI_BaseNPC` / player datamaps behind the 124 unresolved names. Closes `python_bridge.md`'s file-like open item. Full: `docs/script_api.md` | 9.7, 9.8–9.10, 9.4, 8.5 | [x] |
| RE19 | **Choreographed-scene format + event semantics** — the `.vcd` grammar (uniform word-list/brace, 14 live tokens of a much larger parser vocabulary), the 19-type `CChoreoEvent` enum with **nine** used by content and `CAMERASHOT` unhandled by the engine, the `CSceneEntity` datamap (4 inputs / 7 outputs; `force_lod` is a dead key), actor binding **by name** (`targetN` is inert — `!targetN` has zero uses), `position_start`/`position_end`, absolute-time playback offset by `snd_mixahead`, and `Start→OnStart` / end→`OnCompletion` / `Cancel`→`OnCanceled` / `firetrigger "N"`→`OnTriggerN`. 5,444 scenes on disk, 105 named by the 122 map entities; the rest are per-line dialogue scenes on `CInstancedSceneEntity`. Full: `docs/choreographed_scenes.md`; probe: `tools/probe_scenes.py` | 12.1, PL9 | [x] |
| RE20 | **MDL v2531 facial data** — the studiohdr facial block (at **344**, eight bytes past the VAMPTools field walk), `mstudioflexdesc_t` 4B / `mstudioflexcontroller_t` 20B / `mstudioflexrule_t` 12B + 8B RPN ops / `mstudiomouth_t` 20B; `StudioFlex` 32B with its target ramp, and **both** `StudioVertAnim` encodings — the 8B compressed record stores *directions*, two byte offsets into a 5,314-entry unit-vector table in `StudioRender.dll` plus `n/255` magnitudes scaled 8.0/2.0, and a 20B raw form (`mingxiao_transformation` only). **`NumEyeballs` is 0 on all 4,444 models** — no eye pose, look-at or procedural lid was ever authored; eyes are eyelid flexes. `.lip` is plain text (7,136 files, `VERSION`/`PLAINTEXT`/`WORDS`/`EMPHASIS`(always empty)/`CLOSECAPTION`/`OPTIONS`), joined to `expressions/<model stem>_phonemes.vfe` (249 tables) for phoneme→controller weights. Corrects `mdl_v2531.md`: `StudioModel` is **224B** and carries its own de-quantization offset/scale at +0xA0. Full: `docs/facial_animation.md`; probe: `tools/probe_facial.py` | 12.3–12.5, PL10 | [x] |
| RE21 | **`GameFrame` usercmd order** — is player movement processed before or after the think pass? Settles `runtime-architecture.md` §3's inferred ordering (`DumpFuncs funcs=10571fc0`, the RE2 workflow) | 11.1, 4.7 | [ ] |
| RE22 | **The ducked hull** — Source's crouch AABB dimensions + `CategorizePosition`/`StepMove` interaction (standing `32×32×72` is recorded in `source_movement.md`; ducked is not, and `IN_DUCK` needs it) | 4.7, 11.6 | [ ] |
| SKY | **Sky + ambience rework, Phases B + C (B1–B8b, C0–C5)** — landed 2026-07-26: backdrop correct + at parity with standing tests (`Elysium.Substrate.SkyCube`/`FogPack`); the whole 3D skybox split by BSP area and placed under its transform; fog from its real owners + Source's own linear distance fog as a per-primitive material term (B8b, D4 amended); the sky light at the map's own authored level (**zero on the 83 no-pair maps**); the bake measured in absolute units — direct light explains ~0% of a median lit face, the bounce floor *is* the ambient level. Open residue promoted to **3.10–3.13 + RE17**. Facts: `sky-ambience.md`; full as-built: `roadmap-archive.md` → SKY | 3.6/3.7 | [x] |

The Ghidra extraction findings behind the closed rows (the RE1/RE2/RE3/RE4 detail:
addresses, datamap shapes, method notes) live in `roadmap-archive.md` → "Ghidra
extraction"; durable format/behaviour facts fold into the owning topic docs
(`python_bridge.md`, `entity_io.md`) as they are consumed.

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
| `GameInputWindows` is a beta plugin with a redist prerequisite | PlayStation pads regress or fail to enumerate on a player's machine | 10.6e authors device configs against the documented VID/PID set and keeps the mapping in `Config/DefaultGameInput.ini` (data, not code); Xbox/XInput remains the fallback path, so a GameInput failure degrades to "PS pads need Steam Input" rather than to no gamepad; `GameInputRedist.msi` is tracked as a 10.5 packaging prerequisite |
| Asset enhancement drifts off-style | silent look regression | `asset-enhancement.md` adjudication test + `elysium.EnhancedTextures` A/B toggle keeps the faithful set as reference; per-family review, not per-texture |
| Modern UI loses VtMB's voice (reads generic/AAA) | the remaster stops feeling like VtMB | 8.6 keeps the original's structure, palette and iconography and re-skins only the craft; presentation test applied per screen; `m0_menu_build.md` + extracted `.res`/scheme (PL8) are the intent reference every screen is checked against |
| "Polish" leaks into the logic layer | silent divergence from retail behaviour, unfindable later | `remaster-direction.md`'s governing rule: RE first, owner's call, dated decision-log entry recording faithful *and* chosen behaviour; default is reproduce, and layer assignment happens before the work, not after |
| No classic-UI mode to A/B against | a UI regression has no reference | the original's structure is captured as data (PL8) and in `m0_menu_build.md`, so screens are checked against intent rather than pixels; the *world* keeps its faithful A/B path unchanged |
| ~~The player stays a pawn + a sheet struct while 9.4/9.8/9.9/9.10/9.5 land on it~~ **(resolved)** | five systems built against a shim, then a five-way migration with saves already in the wild | **11.4 landed ahead of all five**: the sheet is on `FElysiumCombatCharacter`, the durable half is `FElysiumPlayerRecord`, and the shape is VtMB's own (`savegame_format.md`, `script_api.md`) — a port, not an invention |
| Modal screens fight over input mode (three independent owners today) | the mouse is unusable in some screen order; Cog can make the game unclickable | 11.5's single arbiter + a Substrate test asserting the scope stack balances across every transition |
| P12's facial RE was unknown-duration work that **blocks PP2 in full** (owner call: eyes + lipsync gate the cinematic) | the playable path stalls behind RE | **Retired as a risk — front-loading worked on both halves.** RE19 closed the scene format, event semantics and completion contract; PL9 mirrored the 5,444 scenes + 7,136 `.lip`; **RE20 closed the face** — the flex chunks, both vertex-animation encodings, the `.lip` grammar and the phoneme tables, validated across 3.3 M records with zero failures (`docs/facial_animation.md`). Only PL10 (a mechanical bake) is left in "Now". The residual is a *scope* question, not an RE one: no eyeball data was ever authored, so 12.4's look-at half is an owner call, tracked on the task |
| A shots baseline silently invalidates across a re-bake or content rebuild (measured: up to ~10 mean on bounce-dominated vantages from **byte-identical** inputs) | a look regression hides in toolchain noise — or toolchain noise reads as a regression | B6's measured rule: re-baseline after any bake/content change; A/B a small effect as two runs over one fixed asset set (a cvar A/B), never across a rebuild |

## Decision log

Append-only, dated, newest first: **`decisions.md`**. A decision is recorded there when it
is made — including a behavioural divergence's faithful-and-chosen record
(`remaster-direction.md`'s governing rule) and "pending" decisions with their trigger.

## Traceability (old plan IDs → this doc)

| Old | Here |
|---|---|
| M0 | Done foundation |
| M1.1 Source movement / M1.2 materials / M1.3 prewarm / M1.4 texlights | 4.7 / 7.4 / 3.8 / 3.4 |
| M1 polish (grade, sky orientation, A/B toggles, repo hygiene) | 3.7, 0.7 |
| M2 (props + decals done; water, coronas, A/B) | done / done / 7.3 / 7.1 / 7.8 |
| M3 | P1 + P2 + P4 |
| M4 | P5 + P6 |
| M5 | P8 |
| M6 | P9 |
| L0.1/L0.2 | 0.1/0.2 · L1.1–L1.4 → 3.1–3.4 · L2.1–L2.3 → 3.5–3.7 · L3.1–L3.4 → 7.5, 7.6, 7.1, 7.7 · L4.1–L4.4 → 10.2, 10.2, 10.2, 3.9 · L5.1–L5.3 → 7.8, 10.1, 10.3 |
| X1 / X2 | 0.3 / 0.4 |
| engine-core Phase 1 / Phase 2 | P1 / P2 |
