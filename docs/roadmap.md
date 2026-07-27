# Elysium-Unreal — Consolidated Roadmap (single source of truth)

**This document is the one work tracker.** It consolidates and supersedes the plan/tracking
sections of `rebuild-strategy.md` (milestones M0–M6, pipeline backlog),
`debug-tooling.md` (build order), and `engine-core.md` (Phases 1–2). Those docs remain the
**design/reference detail** behind the tasks here; this doc owns **sequencing and status**.
If a task exists anywhere, it exists here.

This file is the **only** status file. There is no as-built archive and no decision log: git
history is the as-built record, and a decision's outcome is a present-tense fact in the doc that
owns the system.

## How to use this doc

- Status marks: `[ ]` open · `[~]` in progress/partial · `[x]` done (verified) · `[P]` parked
  (deliberately deferred — revisit trigger stated).
- Every open task: **ID — name — why/where — acceptance — deps**. Detail lives in the linked
  design doc; don't duplicate it here — link it.
- **Landing a task — two writes, and a hard cap:**
  1. Durable how-it-works facts → the **owning design doc** (`docs/CLAUDE.md` maps which).
     This is the only place implementation detail is written in prose.
  2. Flip the checkbox here and replace the open task's body with **one line, ≤2 lines**:
     what landed, and the doc that owns it. Not what was verified in detail, not the deps,
     not the reasoning — those are the design doc's and git's.

  **Do not** restate the same fact in more than one doc. If a sentence would be true in
  both the design doc and here, it belongs only in the design doc.
- **Decisions are not logged anywhere.** A decision's outcome is written once, as a present-tense
  fact, in the doc that owns the system. A deliberate divergence from VtMB is written the same
  way — faithful behaviour, what we do instead, marked as a divergence with the owner call.
- Old plan IDs (M1–M6, L0–L5, X1/X2, engine-core Phase 1/2) map to new IDs in the
  **traceability table** at the bottom; other docs may still say "M3" — that table resolves it.

## North star and the slice ladder

Rebuild VtMB as a playable game **— remastered —** on UE 5.8 + C++ from this repo's own
exported intermediates; **bring-your-own-game holds** — nothing game-sourced is committed
(strategy and principles: `rebuild-strategy.md`). The world's *look* is **baked offline into a
gitignored `.uasset` plugin mount** (`/ElysiumBaked`, regenerable like `tools/out/`) and adopted at
load, while collision, entities, scripting, audio and NPCs stay runtime-built — adopted 2026-07-26. Everything is proven on `sp_tutorial_1` first (1,226 entities, 75 classnames — VtMB's own
vertical slice), then scaled across ~100 maps.

**Direction:** the presentation/feel/logic three-layer rule and its adjudication tests are owned
by `remaster-direction.md` — read it there, not restated here.

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

**Owner call, 2026-07-26.** One path to a real, played game drives all
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
| **PP0 — the core refactor** | the spine: one clock/frame, world services, app states + pause, the player entity, input scopes, commands + user command, the view seam, the play harness | 11.10 *(11.0, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.8 [x])* |
| **PP1 — New Game & genesis** | chargen for real: clan, **name**, sex, spends — onto the player entity, Python-readable; `sp_genesisdevice_1` played, not skipped — the map is already exported **and** baked, so what is left is the `elysium.SkipIntro` re-scope | 9.4 a–g *(RE24 [x], RE25 [x])*, 8.6's New Game click path |
| **PP2 — the theatre cinematic** | the intro plays start to finish: choreography, scripted camera, line audio, subtitles, **eyes and lipsync — all block** (cont. 5); the PC is on camera, so its body stands here | 12.1–12.5, 8.11a (+ `sp_theatre` export/bake) *(11.7 [x])* |
| **PP3 — land the tutorial** | the chain hands the player to Jack; the first conversation runs with sound and reactions | 9.2, 9.9 |
| **PP4 — core mechanics** | faithful movement (owner call: **in** the path), camera modes, the body's gait, feeding, items + object interaction, dice, the vitals HUD | 4.7, 8.11b, 10.6, B6, 9.8, 9.6, 8.9 |
| **PP5 — persistence** | save / quick / autosave + load mid-run; `trigger_autosave` live | **[x]** *(11.9 = 9.5)* |
| **PP6 — complete the tutorial** | stealth, disciplines, firearms — every retail beat to the exit, proven headlessly | 13.1, 13.2, 13.3 → P9's slice acceptance as `test.bat Play` |

**After PP6 (the thaw):** 9.10 economy/barter, 8.8, 8.10, the P3/P7 look lanes, 10.1–10.5 and
asset enhancement — re-sequenced then.

*(Pre-path housekeeping, now done: **0.9** put the bake architecture on the record and confirmed
`spike/uasset-bake` — the branch all of this sits on — is `main`. Docs + git only, zero rendering
work.)*

## Now — the unblocked front

The open tasks whose dependencies are met, in the order they pay off. Regenerable from the
deps below — refresh it whenever a task flips:

1. **11.10** — the last of PP0: the play harness. It now has everything it was waiting on — a
   named verb for every player action and a recordable command stream (11.6), and a published view
   state (11.8) a beat can assert what is on screen against.
2. **9.4 — the PP1 rung, in seven sub-steps.** Build order: **RE24 [x]** → **a [x]** the rulebook
   readers (the gate — 12 table families now parse and are asserted against the exported files)
   → **b** the sheet as registered fields, `Max_Health` read as the authored
   stat it is → **c** the 290-call counter surface → **d** quests for real → **e** the journal screen
   → **RE25 [x]** → **f** chargen including the quiz → **g** genesis played, not skipped.
   **b is next**, and it starts from data that already loads.
3. **9.8 / 9.9 / 9.10 / 9.5** — the rest of what the hinge unblocked. They land *on*
   `FElysiumCombatCharacter` and `FElysiumPlayerRecord`: 9.10 finishes the economy over the
   `money` field that already exists, 9.8 fills the record's inventory half, and 9.5 (= 11.9, **[x]**)
   walks the chain — so each of the others is saved the day it registers its state as fields.
4. **8.11a** — the player body. **PL13 [x]** put all 56 clan bodies on disk beside the NPCs, so
   nothing is left to export; the task itself stays PP2-gated, because the theatre is where the
   body is first on camera.

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

- [x] **B1 `env_fade` fires `OnBeginFade`** — the whole `CEnvFade` class per the decompiled datamap;
  `OnEndFade`/`ReverseFade` do not exist in VtMB. → `entity_io.md`.
- [x] **B2 Real entity objects in CPython** *(= 9.3's first slice)* — the `vampire` module's real
  `Entity`/`Player` types over the P1 class-chain tables. → `python_bridge.md`.
- [x] **B3 Minimal NPC presence** *(the 8.5 carve-out)* — `FElysiumNpc` + `FElysiumNpcMaker`: NPCs
  stand their real skeletal bodies, latch `WillTalk`, open a dialog session. Feeds but does not
  close 8.5.
- [x] **B4 `.dlg` parser + dialogue runner** *(9.1's core; UI is interim)* —
  `FElysiumDlgConversation` on `StartPlayerDialogRemote`, field-4/5 through the installed host,
  `OnDialogEnd` on close. The interim `SElysiumDialogueBox` is replaced by 9.2. → `game_runtime.md`.
- [x] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — `unhidePlus()` resolves and
  `setPlus()` arms `trig_popup_move`, unassisted from map load. → `python_bridge.md`.
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

- [x] **0.1 Profiling baseline** — `profile.bat` / `-ElysiumProfile` over fixed vantages;
  `PCD3D_SM6` confirmed. → `rendering-perf.md` → "Profiling baseline".
- [x] **0.2 MegaLights engagement check** — MegaLights dominates, many-light cost ~flat, no silent
  VSM fallback, so 3.1 is not urgent. → `rendering-perf.md` → "Profiling baseline".
- [x] **0.3 Export a second map** — `sm_hub_1` + `sm_pawnshop_1` exported, loaded and profiled;
  unblocked 3.4, 4.6, 10.1, 7.8.
- [x] **0.4 Sidecar space audit** — every consumed sidecar already emitted in Unreal cm; **PL7 is
  empty**. → `rebuild-strategy.md` contract table.
- [x] **0.5 Cog 5.8 compile spike** — Cog (upstream `cb1b435`) vendored into `Plugins/Cog/`, builds
  and runs clean on 5.8. Full integration is 2.1.
- [x] **0.6 `ent_survey` count reconciliation** — **16,125 outputs / 1,591 Python** on retail;
  pinned across the docs.
- [x] **0.7 Repo hygiene** — `tools/ghidra*/` + `tools/re/` untracked, local-only.
- [x] **0.8 Re-base `entity_io.md` on the patch map set** — 108 maps / **71,096 entities / 326
  classnames / 24,081 outputs**. → `entity_io.md`.
- [x] **0.9 The uasset-bake architecture** — the world's *look* bakes offline into the gitignored
  `/ElysiumBaked` mount, everything else stays runtime-built. → `uasset-bake-spike.md`. Residue handed on: **PL11**.

## P1 — Entity substrate *(design: `engine-core.md` — read it; steps here are the tracker)*

- [x] **1.1 Currency types + persistent state** — `FElysiumVariant`, `FElysiumEntityHandle`,
  `FElysiumGameClock`, `UElysiumGameStateSubsystem` (the `G` store, the quest map).
- [x] **1.2 `.ents` defs parser** — `FElysiumEntityDefs::Parse` into immutable `FElysiumEntityDef`
  records; `elysium.ents` verifies the round-trip off disk.
- [x] **1.3 Class registry + base entity** — `FElysiumClassRegistry` + `FElysiumEntity`;
  unregistered classnames become inert records.
- [x] **1.4 Entity world + event queue + chokepoints** — `FElysiumEntityWorld`, the two chokepoints
  (`AcceptInput`, `FElysiumEventQueue::Add`), the `FElysiumIOSink` taps.
- [x] **1.5 Brush bodies** — `UElysiumBrushComponent` per brush entity, per-classname solidity,
  overlap → `OnTouchStart`/`OnTouchEnd`.
- [x] **1.6 Starter classes** — `logic_auto`, `logic_relay`, `trigger_multiple`/`trigger_once` over
  the `CBaseTrigger` chain node.
- [x] **1.7 Labels & debug strings** — editor-only Outliner labels; `FElysiumEntity::DebugString`
  threads every I/O log line.

**Slice acceptance:** loading `sp_tutorial_1` fires the `logic_auto` chains through real
queue entries; walking through a trigger logs timestamped I/O lines; Python payloads appear
as script-host log lines; ring buffer holds the session history — observable with logs only.

## P2 — Debug layer *(design: `debug-tooling.md` Layers 1–2)*

- [x] **2.1 Cog integration** — `FElysiumCogWindow` + the `Elysium.Status` window; an **Elysium**
  category in the F1 menu, all `#if ENABLE_COG`.
- [x] **2.2 Entity windows** — **Entities**, **Entity Inspector** and **Event Queue** over a shared
  selection; the `FElysiumEntityWorld::EnqueueInput` seam lands here.
- [x] **2.3 `ent_*` verbs** — the Source-style set on `UElysiumEntityDebugSubsystem`: `ent_fire`,
  `ent_dump`/`ent_info`, `ent_pause`/`ent_step`, `ent_break`, the overlays.
- [x] **2.4 World visualization** — the `Elysium.World Viz` window: entity gizmos, trigger AABB
  wireframes, caller→target I/O beams.
- [x] **2.5 Maps/Lights windows + `elysium.reload` + cheat manager** — travel/reload timings, live
  light calibration sliders, the export→reload hot loop, `UElysiumCheatManager`.
- [x] **2.7 Agent-facing MCP surface** — `UElysiumMcpSubsystem` registers **20 `elysium_*` tools**
  over the engine `ModelContextProtocol` plugin; on by default in dev builds. → `debug-tooling.md`
  Layer 3.
- [x] **2.8 Automation tests (both tiers)** — the `Substrate` (`-nullrhi`) and `Content`
  (self-skipping) tiers in `Private/Tests/`, driven by `test.bat`.
- [x] **2.9 Screenshot-regression harness** — `FElysiumShotRun` (`-ElysiumShots`) over the
  profiler's vantages + `tools/shots_diff.py` baseline promote/diff. A baseline is only valid
  against a fixed bake *and* fixed content assets.

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
- [x] **3.5 Sky IBL onto the SkyLight** — landed inside sky-ambience C1/C2: the built cube is
  assigned, intensity from the map's own type-5 magnitude, **zero on the 83 maps with no
  `light_environment`**. A night-sky lift goes through the D3 knobs, never here. →
  `sky-ambience.md`.
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
  `FElysiumTextureCache` (load is texture-bound). *Deps:* none.
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
- [ ] **3.12 `sm_hub_1` fill adjudication** — of `sm_hub_1`'s 249 lights that touch nothing
  emissive, ~90% were kept by hand against the classifier's fill call (`light-attribution.md`)
  — the only map where the two genuinely disagree, holding 104 of 409 fill candidates
  project-wide. A 15-light shortlist of the highest-confidence disagreements sits in the map's
  eastern strip (X > 7000), numerically indistinguishable from lights killed in the west; the
  framing under test is that the classifier asks "authored as fill?" while the hand survey (live
  rig, Lumen on) answers "does the scene survive without it?" — the two diverge where fill is
  *load-bearing* (faking sky/city-glow ambient with nothing emissive nearby to bounce off).
  Judge the shortlist by in-engine A/B (MCP teleport + screenshot per light), now that C2 zeroed
  the map's SkyLight (no pair — its "sky glow" is all sprayed fill, *more* load-bearing than
  before) and C3's Skylight Leaking is the landed, measured replacement to gate against. Kill a
  fill only where GI demonstrably replaces it, recorded per map in `light-attribution.md`. Follow-on
  threads once this closes: survey `hw_609_1` to break a 3-of-4 tie (lowest protected share, 45%
  abstention); re-score with the `type`/`style` clauses (type-0 auto-protect, styled protect, spot
  prior) against all surveys; an in-engine counterfactual per authored batch
  (`probe_light_attribution.py` already reconstructs per-luxel baked luminance to check against);
  batch-level voting (83/85 unanimity on `sp_tutorial_1`, untested elsewhere). *Deps:* none (C3
  [x]).
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
  (`source_movement.md`). **Faithful first** — this is the feel
  layer's known-good baseline and the thing every later tuning delta is measured against, so it
  lands line-by-line from the decompile and stays A/B-able (`remaster-direction.md` axis 3).
  Frame-rate independence, high-polling-rate mouse input and FOV control ride along (identical
  behaviour, modern plumbing); any *behavioural* delta — accel curves, air control, step feel —
  is a separate, owner-approved decision after this runs. **The body must be a box, so this is not a
  `UCharacterMovementComponent` override**: `ACharacter` creates a capsule root that cannot be
  substituted, and a capsule's rounded bottom reports ~0.65 against `StepMove`'s `0.7` standable test,
  rejecting every climb (`source_movement.md`). **11.6 [x]** re-based the pawn to `APawn` + box +
  `UElysiumMovementComponent` and supplies the `FElysiumUserCmd` this consumes; what is left here is
  the line-by-line port — the gravity half-step split, the timestep, ducking and
  water, against 11.6's Source-shaped shell. **`surfaceFriction` is closed**: it is 1.0 on every
  world surface in retail (VtMB scales the material's friction by 1.25 and clamps to 1.0; 1 of
  11,624 VMTs carries a `$surfaceprop`, so everything is the `default` prop at 0.8), so the shell's
  hardcoded 1.0 is already faithful and there is nothing per-surface to export
  (`source_movement.md`). **The two divergences this task carried are both settled by RE**
  (`Host_FilterTime` `0x2008ba30`, decompiled): (a) **the frame-delta bound is `[0.001, 0.1]`
  seconds** — VtMB clamps `host_frametime` to a hard 10 fps floor before the game sees it, so the
  port pins that one number across `FElysiumTimeControl::AdvanceFrame` and the mover, which both
  take Unreal's raw delta today. (b) **There is no tick.** `Host_FilterTime` bounds a *variable*
  frametime and returns — no accumulator, no fixed-interval loop — which confirms
  `game_runtime.md`'s pre-tick finding from the pacing side and retires the "fixed 66.7 Hz tick"
  (that is *modern* Source's default). So retail's frame-rate dependence is real and faithful:
  `AirAccelerate`'s `addspeed` clamp stops binding above ~117 fps, and full-step gravity puts the
  jump apex at `25 − 100·dt` units instead of a flat 25. **A fixed-step accumulator is therefore a
  divergence, not the baseline** — it ships behind `elysium.move.FixedStep` (default 0 = faithful
  variable delta), recorded in `source_movement.md`. **RE22 is closed** and the ducked hull is
  `(-16,-16,0)..(16,16,36)` with the eye at 30. **Ladders are out of scope, not deferred**: VtMB's
  `PlayerMove` switch has no ladder arm and no map places a ladder entity, so there is nothing to
  reproduce. Water movement exists (`WaterMove` `0x101200c0`) and its wish-velocity build, `0.8`
  speed clamp, `40` idle sink and friction step are transcribed, but no exported map places a water
  brush, so it lands formula-faithful and unexercised; the accel tail and the `WaterJump` pair are
  located, not read. Sequenced **in
  the playable path (PP4)** by owner call — the tutorial is
  played with VtMB feel, not UE feel. (c) **An owner call this task carried, now
  discharged:** RE21 pinned retail as movement-*first*, called **reproduce** and landed by **11.11** — the mover already runs before the think pass, on the user
  command's own delta, with the player's own think ahead of it. Port onto that order; the frame is
  no longer moving under this task. *Deps:* 11.6, 11.11.
- [ ] **4.8 Rotating/linear/elevator family** — `func_rotating` (spin-up/down, hurt-touch),
  `func_movelinear`, `func_elevator` (`GotoFloor`, floor Z table), keyframed movers if the
  tutorial needs them. **Carries the mover-push remainder 11.11 left**: movers are `MOVETYPE_PUSH`
  and displace what they touch from their own side, which is what makes the move-first frame safe.
  `FElysiumMoverBase` sweeps instead, and Chaos resolving that sweep already shoves the pawn out of
  a closing door's arc (measured) — so nothing tunnels, but VtMB's authored push is not reproduced:
  `dmg` is not dealt and `OnBlockedClosing` does not fire unless the sweep is *fully* blocked, and
  the displacement is a physics artifact rather than `PhysicsPushEntity`'s recursive push list.
  *Deps:* 4.1.
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
  RE-verified 1024×768 canvas model.
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
  real `tutorial.py`. Full rationale: `python_bridge.md`. *Deps:* 5.1, 5.2. → **9.3.**

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
  billboards, StartOff spawnflag filtering. *Deps:* 0.4.
- [x] **7.2 Decals** *(M2)* — `infodecal`s as **deferred `UDecalComponent`s** (owner call — the
  PMC-parity stage skipped; a deferred decal is lit exactly like its host wall, Lumen bounce
  included): the exporter writes a `<map>.decals` projector sidecar (Unreal cm), and the new
  `M_Decal` master (`tools/make_decal_material.py`) + `BuildDecals` spawn one component per line —
  the decal UV frame is U→local Z, V→local Y.
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
  `reflections.md`), `BaseTex2`+`BlendAmount` (WorldVertexTransition via the `.blend` sidecar →
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
  **`docs/reflections.md`**; decision:. *Verified:* build + `test.bat`
  green, 10 maps re-exported/re-baked, shots re-baselined, Lumen reflections 0.15–0.22 ms against
  the committed 0.18–0.25. *Deps:* 3.5, 7.4.
- [ ] **7.6 Bloom/glow tuning** *(was L3.2)* — VtMB's overbright neon/selfillum vs pinned
  exposure. *Deps:* 3.6.
- [ ] **7.7 Shadow quality** *(was L3.4)* — contact shadows on hero lights, penumbra softness,
  within 0.1 budget. *Deps:* 0.1, 3.1, 3.2.
- [ ] **7.8 A/B capture harness** *(was L5.1, re-based)* — scripted fixed-camera captures vs
  **the original game** at the shared vantages (RE17's protocol — the original is the only
  reference). The local half exists (2.9 +
  `shots_diff.py`); respect its measured noise floor — re-baseline after any bake or content
  rebuild. *Deps:* 0.3, 3.9, RE17.
- [ ] **7.9 Weather & wetness** *(facts + design: `weather.md`)* — the rain system, never
  surveyed until now: `func_particle`/`env_particle` precipitation volumes on 6 maps, the
  `worldspawn` wetness channel the level scripts drive through `FadeGlobalWetness`, the
  `lightningrotator` rig on 5 maps, and the `Environmental/Weather` ambients. Landing order is
  independent-first: **(a)** wetness as a Material Parameter Collection scalar off the existing
  `FElysiumWorldEvents::GlobalWetness` (no particle RE needed, largest look delta); **(b)** the
  baked top-down occlusion height map in the exporter + a debug view; **(c)** Niagara rain in
  the authored volumes, occlusion-masked; **(d)** impacts + the hand-placed drip emitters;
  **(e)** volumetric mist, and lightning as real Lumen-bounced light on its authored timer
  rhythm. Appearance is Presentation (built native); the volumes, shelters, timers and script
  calls are Logic (reproduced). Re-enabling the two shipped-disabled rain layers is a
  divergence — owner call, recorded in `weather.md`. *Deps:* PL12, RE23 (c–e only; a–b are unblocked).

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
- [x] **8.3 Dynamic props** — `prop_dynamic`(+`_ornament`) stand their decoded static `.mdl`,
  per-instance addressable (ScriptHide/Unhide, body-follow, `Break`); skin families repaint them via
  a baked `PropSkinSet`. Non-solid — collision is 8.4.
- [x] **8.4 Physics props** — `prop_physics` / `phys_hinge` as Chaos rigid bodies on the baked
  `SM_<stem>`; collision and mass reproduce VtMB's own `.phy` exactly. **Chaos settle/push feel +
  hinge swing await an owner in-game play test.** → `phy_vphysics.md`.
- [x] **8.5 NPC presence + `scripted_sequence` minimal** — NPCs idle on a disposition-selected
  stance; `scripted_sequence` runs as a real class reproducing its beat outputs. **Not reproduced:**
  locomotion (placed on the mark, not walked to it) and `OnScriptEvent01..08`.
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
  street — corrects the original call); the HUD stands down while
  a menu is up, since `sm_hub_1`'s `havenbum` opens a conversation unprompted. Design: `docs/ui-architecture.md`; the RE it is checked against: `docs/vtmb-ui.md`
  (the two UI stacks, both schemes, the **1024×768** canvas law, the HUD class inventory, and four
  corrections to `m0_menu_build.md`). **PL8** [x]. The **Nocturne** type set (Spectral SC /
  Spectral / Inter, SIL OFL, no RFN) ships as committed `UFontFace` assets
  (`fetch_ui_fonts.py` → `make_ui_fonts.py`). **CommonUI + CommonInput** adopted with widget trees
  in C++ Slate, so **no Widget Blueprint assets**. `UElysiumUISubsystem` +
  `UElysiumMainMenu` + `ElysiumUIStyle`/`Strings`/`Texture`; `elysium.menu [pause]`,
  `elysium.menu.close`, `elysium.MenuVantage`, `elysium.BootMenu`. Six owner calls:.

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
- [x] **8.7 Ropes** — `move_rope`/`keyframe_rope` chains as Verlet `UCableComponent`s; rest length
  and node count reproduce VtMB's own arithmetic. **Open:** never put side by side with the running
  original, and `Subdiv` render tessellation has no analogue on `UCableComponent`. →
  `entity_visuals.md`.
- [ ] **8.8 Sign / popup panels on the UI foundation** — 4.10's Canvas panel re-drawn on 8.6's
  stack. The **authored layout is honoured as proportion and grouping** (block rects, ordering,
  emphasis) and re-set with vector type on the resolution-independent layout — the `CSignUI`
  1024×768 uniform-scale canvas model stays the *reference* for what
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
- [ ] **8.11 The player body** *(design: `camera-view-modes.md` → "Player mesh, fade and
  first-person rendering")* — the PC's own skeletal body: the thing 11.7's camera already solves for
  and nothing draws. `FElysiumPlayer::OnRuntimeModelChanged` is a deliberate no-op today ("the
  player's model is the pawn"), so the entity chain's animating half is unused on the one character
  that is always on screen. Two carve-outs, sequenced apart because they need different things:
  - **a. The body** *(PP2)* — the mesh stands, animates under choreography, and fades on the band.
    Built on 8.5's machinery — glTFRuntime, a skeletal visual, bank retarget by bone name — with the
    pawn as the body the entity places. **Model identity is data, not a constant:**
    `vdata/system/clandoc000.txt` (on disk since PL5b) carries `M_Body0..5`/`F_Body0..5` per clan —
    7 clans × 2 sexes × 6 armour slots whose top two repeat the tier-3 suit, **56 distinct `.mdl`
    over 84 slots** — plus the `M_Hands`/`F_Hands` first-person viewmodels the patch restored (PL14).
    All 56 are exported (**PL13 [x]**) as ordinary `npc_index.json` entries, so the selection is
    `clandoc` path → index `model` → stem → the 8.5 loader; **none carries a flex rig**, so the PC
    body has no face to drive.
    Chargen (9.4) supplies clan + sex; a cvar default covers the path ahead of it. Visibility is
    `CAM_IsThirdPerson` as 11.7 defines it (true from the blend's first frame, true under a scripted
    camera), and the fade band is already solved — `UElysiumCameraComponent::ModelAlpha()` — needing
    **dithered or masked** opacity on the character material, never translucency, which would take
    the body off the opaque path and out of Lumen. **Choreography is why this is PP2 and not later:**
    `logic_choreographed_scene` carries `MaleAnim`/`FemaleAnim`
    (`m_iszAnimSetForMalePlayer`/`ForFemalePlayer`, 25 uses each) and binds `Player`/`!player` as an
    actor, so the theatre's embrace + trial animate the PC on camera (`choreographed_scenes.md`).
    *Deps:* 8.2 [x], 8.5 [x], 11.7 [x], PL13 [x]; 9.4 for real identity.
  - **b. Locomotion** *(PP4, beside 4.7)* — idle/walk/run/crouch driven by movement state.
    `UElysiumNpcAnimInstance` is a two-sequence idle crossfade; a player locomotion blend is new
    work, and the states it blends between are the Source movement port's, so it lands beside 4.7
    rather than ahead of it. *Deps:* 8.11a, 4.7.
  *Acceptance (a):* on `sp_tutorial_1`, `togglecamera` shows the PC's own clan model on the boom,
  dissolving in across `cam_fadeend`→`cam_fadestart` and culled at weight 0; the theatre's scenes
  animate it. *(b):* the gait matches the mover's reported state through a walk/run/crouch pass, and
  a `test.bat Play` beat asserts it.

**Slice acceptance** *(M5 criterion)*: New Game starts from a real, modern menu that is legible
and correctly proportioned from 1080p to 4K and at 21:9; the HUD and the tutorial's popup signs
draw on the same stack; NPCs stand in the world at their entity origins.

## P9 — Dialogue & persistence *(design: `game_runtime.md`, `rebuild-strategy.md` B7/B9)*

- [x] **9.1 `.dlg` parser + dlgexpr** — `ElysiumDlg.{h,cpp}`: the 13-field parser, the `dlgexpr`
  front-normalizer, the host-agnostic branch machine. NPC col-4 = action, PC col-4 = gate. →
  `game_runtime.md`.
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
    index (`FElysiumEntityWorld::RenameEntity`). Full record:.
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
    exclusively from dialogue; **9.7** RE'd it and **9.7d** replaced its hardcoded-false stub with
    the real selector.
  - [x] **`vamputil` for real** *(9.3b)* — with `ccmd`/`cvar` bound (its top-level `c = __main__.ccmd`
    no longer throws), the real `vamputil.py` imports: its `zvtool` DAG, `fileutil`, and the 46 helpers
    (`IsClan`/`IsIdling`/`RandomLine`/`unhidePlus`/`setPlus`/…) load, and `tutorial`'s `from vamputil
    import *` merges them into `__main__`. One shim was needed: `vampire` binds a mutable **`Character`**
    class the patch monkeypatches (`Character.Near = _Near`); ours is a compatibility stub (the 24
    Character methods still dispatch off the Entity/Player getattro), see `python_bridge.md`. The four
    `from vamputil import RandomLine` maps (`santamonica`, `chinatown`, `gallery`, `fusyndicate`) now
    resolve that name. *Verified live via MCP.*
  *Deps:* 9.3a, B2. *Remaining blocked on:* the inventory follow-up, 9.4.
- [x] **9.3b Console bridge — `ccmd` + the `cfg` alias table** *(the fifth scripting surface)* —
  `vampire.ccmd`/`cvar` over a host-agnostic `FElysiumConsole` store seeded from the PL5d `out/cfg`
  mirror, with the console→Python fallthrough. Binding them lets the **real `vamputil.py` import**.
  → `python_bridge.md`; the `Character`-shim divergence:.
- [x] **9.3c The script filesystem** — `FElysiumScriptFS` gives the VM its own filesystem namespace:
  reads union the `Saved/` overlay over the `out/` mirror, writes land in the overlay with copy-up,
  escaping the sandbox is the one denial. → `python_bridge.md` → "The script file layer".
- [x] **9.7 The script→engine action surface — survey, RE, spec** — **16,438 call sites / 1,287
  names** surveyed, the `PyMethodDef` tables and datamaps recovered, and `script_api.md` written as
  the per-name inventory + demand-ranked build order. **d** landed `OneOfSet` for real (the 589
  dialogue gates now select) and guarded the `Whisper`/`FrenzyTrigger` receiver split. →
  `script_api.md`; roll model:.
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
- [ ] **9.4 Quests/XP + RPG sheet data** *(the PP1 rung)* — the RPG layer is a shell: `FElysiumSheet`
  is a `TMap<FName,int32>` bag that answers every `base_*` read with 0, quests are a bare
  `name -> int` map with no catalogue and no awards, `CalcFeat`/`BumpStat`/`GetMasqueradeLevel`/
  `DialogDiscipline` are logged stubs and `AwardExperience` drops. The whole `vdata/` rulebook is on
  disk (**PL5b [x]**, `out/vdata/`, 465 files) and **nothing parses any of it** but
  `dispositiontable.txt`. 9.7 sized and constrained the lane: the sheet-counter demand is
  **290 calls** (`AwardExperience` 77 / `HumanityAdd` 69 / `CalcFeat` 53 / `ChangeMasqueradeLevel` 44
  / `Bloodloss` / `BumpStat` / `GetMasqueradeLevel`), the counters are INTEGER datamap inputs on the
  combat character, and **`AwardExperience` takes a STRING** — it names an experience-table entry, so
  it cannot be modelled as an integer add (`script_api.md`). The sheet's home already exists (11.4):
  `FElysiumSheet` on `FElysiumCombatCharacter` (live) + `FElysiumPlayerRecord` (durable); what 9.4
  adds is the loaded data, the fields VtMB's own datamap names, and the two screens that data feeds.
  Table→system map: `docs/vdata-catalog.md`; the sheet's recovered shape: `savegame_format.md`
  (`m_iVAttributes*`, `m_QuestList`, `m_ExpList`). **Owner call:** chargen is a *full* reproduction
  including the `charcreatewizard.txt` quiz, the journal screen is in scope, and the open RE is
  closed **before** the sub-step that needs it. Sub-steps, in build order:
  - [x] **a. The rulebook readers** *(the gate)* — `ElysiumRulebook.{h,cpp}` +
    `UElysiumRulebookSubsystem`, **12** table families over `ElysiumKeyValues.h`, lazy per table,
    verb `elysium.rules`. **Reads only — no consumer is wired; b/c/d/f do that.** Acceptance:
    **161/161 `AwardXP` keys resolve in `experience_table`**. Corrections it forced (container slot
    counts, where the priority-tier tables live, the template count) landed in `vdata-catalog.md`,
    `game_runtime.md` and `savegame_format.md` in the same pass.
  - **b. The sheet becomes real fields** *(**RE24** closed it — the slot counts below are the
    recovered ones, not the earlier estimate)* — the bag becomes VtMB's own shape:
    `m_iVAttributesBase`/`Current` (**35** slots, the `Attrib_Order` block occupying index 0 and
    every derived/bookkeeping stat through `Experience` at 34), `m_iVAbilitiesBase`/`Current`
    (**13**, `Ability_Order` at 0), `m_iVDisciplinesBase`/`Current` and
    `m_iVActiveDisciplinesBase`/`Current` (**13** each, no order slot; `-1` = a discipline the clan
    cannot take). **The base/current split is the whole buff system** and both halves persist. Each
    name registers through `AddSheetIntField`, so one R2 walk serves script reads (`pc.strength`),
    keyvalues, the save enumeration and the inspector, and `GetDynamicField`'s bag shrinks to what
    has no static name. Three datamap names diverge from the `stats.txt` `InternalName`
    (`intimidate`/`Intimidation`, `computers`/`Computer`, `base_gender_`), so the field table needs
    both spellings. **Health is not Stamina-derived** — `Max_Health` is an authored stat slot
    (`Default 100`, no formula anywhere in `vdata`) and **`Health` counts damage taken**, so
    `ElysiumInterimPlayerMaxHealth` retires by being read out of `stats.txt` rather than replaced
    by a formula, and `npctemplate*`'s literal `Max_Health` gives every NPC a track (default 100
    when the key is absent) so `TakeDamage` kills one instead of only recording damage.
    `pc.generation` (9.3's field-table audit gap) lands here. Costs a `FElysiumSaveVersion` bump plus
    matching `ElysiumSave::Describe` rows, or the sheet goes invisible to `elysium.save.diff`.
  - **c. The 290-call counter surface** *(**RE24** closed it)* — the four Character-method stubs go
    real: **`CalcFeat`** returns the clamped rating `Feats::FeatValue` computes over a
    *variable-length* `Base%d` list (each entry the *current* trait value through its own `/`-or-`*`
    modifier, the nine attributes floored at 1, then a feat-level trait-effect pass, then
    `[0, MaxValue]`), **`BumpStat(stat, times)`** loops its recovered third argument and increments
    the **base** under a hardcoded `< 5` ceiling and cannot decrement, plus `GetMasqueradeLevel` and
    `DialogDiscipline`. `AwardExperience` looks its key up, refuses a repeat against the `m_ExpList`
    ledger (give-once is the ledger, *not* the trailing `01`), adds `Experience_Modifier` above 2 XP,
    and accumulates `floor(value/100)` **keeping the sub-100 remainder** into an
    `FElysiumXpEntry`. The counters that already have fields gain their `rules.txt` clamps and the
    clan trait-effect doubling. `ChangeMasqueradeLevel` reaching 5 is **the second game-over
    condition** — the branch 11.3 shipped with no driver.
  - **d. Quests for real** — the `name -> int` map stays authoritative (732 call sites; default-0 on
    miss is VtMB's own contract). What lands is what happens *around* a state change: resolve the
    `CompletionState`, fire `AwardXP` (an experience-table key, not a number — the shipped file's own
    header comment is wrong), `AwardMoney`, and `Event` (script data handed to the installed host — a
    dispatch surface `python_bridge.md` does not list), then keep the journal as
    `ASSIGNED_QUEST { szTitle, idxQuestTable, idxState, iOrder }` rows on the player record.
  - **e. The journal screen** — assigned quests on the 8.6 CommonUI/Slate stack, grouped by hub,
    `DisplayName` as the heading and the current state's `Description` beneath it, coloured by `Type`
    (`success`/`failure`/`incomplete`). Adds one `ElysiumInput::Priority` row, which the pairwise
    `Elysium.Substrate.InputScopes` test picks up on its own.
  - **f. Chargen** *(**RE25** closed it)* — **`createplayer`** declared in `FElysiumCommands` (it is not
    in `controls.md`'s bindable inventory, so it is a new declaration) and implemented by the UI
    subsystem, on the **`Chargen` scope 11.5 reserved and nothing has pushed since**. Full flow:
    name + sex → the `charcreatewizard.txt` `Popup` quiz (the 8 abstract Traits, `Trait_Prereq`
    gating, and the same-`InternalName` random pick drawn from an owned `ElysiumRng` stream so the
    Play tier replays deterministically) → `ConnectionScores` clan suggestion with override →
    history (`histories000`) → priority-tier point-buy → confirm onto the **player entity**, with
    `m_tEffectList` in VtMB's own spelling. The quiz's `Bkg_Image`/`Region`/`TextRegion` keys are
    read as intent, not as a runtime coordinate system (`remaster-direction.md` axis 1). The math
    RE25 recovered is not a second design: build the pools as *clan `Subpool_*` + tier table*, the
    baseline by running the clan's `*_CharGen` leveling template, and price a dot off the
    **pre-purchase base** rating, `New` only at the 0→1 step. `game_runtime.md` §3 has the whole
    model, incl. the `-1` row filter that hides a non-clan discipline and the trait-effect layer
    the clan banes and histories ride on.
  - **g. Genesis played, not skipped** — `sp_genesisdevice_1` is **already exported and baked**, and
    its `newplayer` `trigger_once` already fires `ccmd.createplayer` + `G.Story_State = -5`, so with
    **f** registered the map itself needs no change. What is left is routing New Game's `story` entry
    to it and **re-scoping `elysium.SkipIntro`**: it skips from genesis's `boogieout` exit to the
    tutorial landmark rather than skipping genesis, because `sp_theatre` is unexported and **P12**
    owns it. A reversible divergence, recorded in `level_transitions.md`.

  *Acceptance (PP1):* New Game walks genesis from real input, the quiz-and-spend character lands on
  the player entity, `pc.clan`/`pc.strength` read back from Python, a skill-gated `.dlg` choice that
  was hidden becomes visible, and the journal shows `pc.SetQuest("Tutorial", 1)`. *Deps:* 1.1 [x],
  9.7c [x], 11.4 [x], 11.6 [x]; **RE24 [x]** (b/c are unblocked), **RE25 [x]** (f is unblocked).
- [x] **9.5 Save/load** — **built as 11.9**; see that entry. The four blocks (Session / Player /
  Maps / World) over the R2 field walk, the per-map snapshot lifecycle with the absent-entity set,
  the event queue incl. deferred script strings, think times, `G`, and owned RNG streams, inside a
  `UElysiumSaveGame` shell over a versioned compressed payload. The inventory half arrives with 9.8:
  an item that overrides `TravelsWithPlayer()` joins the absent set with no change here.
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
  - **c. Reserved keys** — console on `` ` `` (VtMB's own `toggleconsole` key; frees F10 for
    `snapshot`) **plus `F7`** for layouts with no `` ` `` left of `1`, Cog's shell shortcuts to
    `Ctrl+F1`–`Ctrl+F4`, all other dev keys on
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
  and surviving a restart; the reserved-key test green. Defaults are the **Patch 11.5** set. *Deps:* 9.3b (the console bus); **11.5** (the input scope stack the
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
- [x] **10.8 OpenLevel map-lifecycle migration** — map change is UE5 hard travel through the
  GI-scoped `PendingMapLoad` and one reused shell `.umap`; the texture cache is a per-map instance
  so GC frees it with the world. → `map-architecture.md`.

## P11 — Runtime spine *(design: `runtime-architecture.md` + `save-architecture.md` — read them; steps here are the tracker)*

The structure *between* the systems P1–P10 design: lifetimes, the frame, the player object, the
session, and the seams. It exists because the slice ladder now reaches "boot a New Game and play it",
and that is the one thing no current doc owns. Its rules are **S1–S10** (`runtime-architecture.md`
§13), orthogonal to `engine-core.md`'s R1–R8.

Steps are ordered so each compiles, ships and is observable alone. **11.4 was the hinge** — 9.4, 9.5,
9.8, 9.9 and 9.10 all sit on it, and it landed before any of them.

- [x] **11.0 Adopt the spine** — all seven `runtime-architecture.md` §16 owner calls recorded in; both design docs flipped to adopted. Opened RE21, RE22.
- [x] **11.1 Frame + clock ownership** *(S1, S2)* — the tick table pinned by tick group and
  prerequisite; `FElysiumTimeControl` as the one pause/scale facade over a private-writer
  `FElysiumGameClock`. **Step order superseded by 11.11**; the clock and facade survived unchanged.
- [x] **11.2 World services** — `FElysiumWorldServices` (`IElysiumEmbodiment`/`IElysiumAudio`/
  `IElysiumTravel`/`IElysiumPresenter`) injected into `FElysiumEntityWorld`; no `Cast<AElysiumMapActor>`
  or `GetFirstPlayerController()` left under the substrate.
- [x] **11.3 App state machine + pause + loading + game over** — `UElysiumGameFlowSubsystem` owning
  `EElysiumAppState` and its transition table; **the screen is a pure function of the state**.
  **Remaining:** the game-over copy is invented (VtMB's death screen is un-RE'd); the loading screen
  covers the level-load flush only — the build pass after it is 10.4's.
- [x] **11.4 The player entity** *(S3 — the hinge)* — `FElysiumAnimating`/`FElysiumCombatCharacter`/
  `FElysiumPlayer` join VtMB's own datamap chain; `FElysiumPlayerRecord` carries the durable half
  across maps. `vampire.Player` retired — `pc` is an ordinary `Entity` handle.
- [x] **11.5 Input scope stack** *(S6)* — `UElysiumInputSubsystem` owning a handle-based priority
  stack; **nothing else in the module calls `SetInputMode`**. **Remaining:** cinematics and chargen
  have priorities reserved but no pusher until P12 / 9.4.
- [x] **11.6 Command registry + user command** *(S5, S7)* — `FElysiumCommands`' **92 declared verbs**
  with stacking handlers, the asserted **command → alias → cvar → Python** precedence, and
  `FElysiumUserCmd` built from button latches — **nothing polls a key**. `AElysiumPawn` re-based onto
  `UElysiumMovementComponent`, with `AElysiumCapsulePawn` behind `elysium.SourceMovement 0`.
- [x] **11.7 Camera component** — `UElysiumCameraComponent` (VtMB has **one** camera and a **weight**,
  not two), applied once in `AElysiumPawn::CalcCamera`, plus the scripted-shot channel (`SetCamera`,
  `vdata/camerashots/`). → `camera-view-modes.md`.
- [x] **11.8 Presentation seam** *(S8)* — `UElysiumPresentationSubsystem` rebuilds `FElysiumViewState`
  once per frame and is the production `IElysiumPresenter`; no widget/HUD path references
  `FElysiumEntityWorld`.
- [x] **11.9 Save/load** *(= 9.5)* — `save-architecture.md` built in full: the versioned compressed
  payload, the four blocks, `EElysiumField::Save` field walk, per-map snapshots against a post-Load
  baseline, and the slot/quick/autosave ring. Verbs: `elysium.save.slots`, `.cansave`, `.delete`,
  `.diff`.
- [ ] **11.10 Play test tier** *(S10)* — a fourth automation tier that drives a real headless world:
  the beat-script driver (`do`/`wait`/`assert`/`shot` over the command registry, injected input, `G`/
  quest/entity predicates and the 2.9 shot baseline), command-stream replay, the save round-trip, and
  the matching MCP tools (`input_inject`, `beat_run`, `save`/`load`, `time`). *Acceptance:*
  `test.bat Play` walks the tutorial opening unassisted and fails loudly when a beat regresses — P9's
  slice acceptance becomes a CI run rather than a manual play-through. *Deps:* 11.6, 2.7, 2.9.

- [x] **11.11 Rework the frame to retail order — move first, then think** *(reversed 11.1's tick
  table)* — **RE21**'s move-first order is the shipped order: sample input → pre-move pass → move →
  gameplay pass → physics → post-move pass → camera → publish, with `FElysiumEntityWorld` driven
  twice a frame. **What the inversion gave up, measured:** a closing door displaces the pawn rather
  than dealing `dmg` or firing `OnBlockedClosing`, so VtMB's authored `MOVETYPE_PUSH` is **4.8's
  remainder**. → `runtime-architecture.md` §3. Discharged 4.7's owner call (c).

**Slice acceptance:** from a cold launch — the menu comes up over the backdrop, New Game runs chargen
and enters the story, the tutorial's opening beats play on rebindable controls with a HUD, Esc pauses,
Save and Load round-trip the run, and `test.bat Play` asserts the whole thing headlessly.

## P12 — The theatre: choreography & faces *(the PP2 rung — everything blocks)*

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
  under `remaster-direction.md`'s Feel layer, not a reproduction, so it needs an owner call
  recorded in `facial_animation.md` before it is built. *Deps:* 12.3.
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
| PL4 | Batch NPC export + include-model resolution — `mdl_skel.resolve_tree`/`local_sequences` (includes@404/408, `StudioModelGroup` stride 116) → shared-bank glbs + `npc_manifest.json` via `npc_export.py`; 45 NPCs / 62 banks / ~410 MB | 8.5 [x] |
| PL5 | Copy sound schemes (a) [x] + the full `vdata/` rulebook (b) [x] + `vdata/Signs/*.txt` ×278 + the 57 referenced background materials (`hud/signs/*`, `interface/Pop_Ups/*`) → `out/signs/` — `UE_extract_signs.py` (c) [x] | 6.3, 9.4, 4.10 |
| PL5b | Mirror the whole `vdata/` rulebook (`system` 97 + `items` 244 + `camerashots` 66 + `hackterminals` 57 + `precache` 1 = 465) verbatim → `out/vdata/` — `UE_extract_vdata.py`, patch-first, `signs`/`.xls` excluded. Consumer map: `docs/vdata-catalog.md` | 9.4, 9.6, 10.7 [x] |
| PL5d | Copy `cfg/*.cfg` (the alias/cvar tables — `user.cfg` carries the Basic/Plus `patchtype` alias) verbatim → `out/cfg/` — `UE_extract_cfg.py`, patch-first, wired into `export_all.py` (`--no-cfg`) [x] | 9.3b [x] |
| PL6 | Texlight merge in exporter | 3.4 |
| PL11 | Remove the dead Lumen-card path the bake superseded (found by 0.9): `export_all.py`'s `bake_cards`/`--no-cards` calls a `cards.bat` that no longer exists and prints a "skipped" line every run; `ElysiumCardGen.cpp` (`ELYSIUM_WITH_CARDGEN`, `elysium.cards.probe`) still builds into editor targets. Nothing depends on either | 0.9 |
| PL12 | Mirror `particles/*.txt` (**1,594**) + the `particles/*.tga` sprite set (**309**) verbatim → `out/particles/` — patch-first, wired into `export_all.py`. Weather is the immediate consumer (33 rain definitions) but the set is engine-wide: fire, muzzle flashes, disciplines, the menu background. Also bake the top-down occlusion height map per map from `<map>.obj` + `worldspawn`'s `world_mins`/`world_maxs` (1024², ~11 cm/texel on `sm_hub_1`). Format: `weather.md` | 7.9 |
| PL13 ✅ | **Export the PC models.** A second seed, not a second path: `pc_models_from_clandoc` reads `out/vdata/system/clandoc000.txt`'s indexed `M_Body0..5`/`F_Body0..5` — 7 `Player_*` clans × 2 sexes × 6 armour slots = **84 slots → 56 distinct `.mdl`** (each clan's top two repeat its tier-3 suit) — and `main()` unions it with `npc_models_from_ents`, so the exported set cannot drift from the table 8.11a selects through. Only the *indexed* keys count: the un-indexed `M_Body`/`F_Body` of the human/Society-of-Leopold templates name NPC models, and the `mp-*`/`unused*` templates repeat the playable paths. All 56 resolve; the install's other 3 `models/character/pc/**.mdl` are named nowhere. Result: **157 characters (101 NPCs + 56 PC bodies), 67 banks, 721 MB** — the bodies are ordinary index entries (63–106 bones, 3–5 own clips, ~1,440 resolved, ~786 KB each; 45 MB total), matched back to a clan slot through each entry's own `model` path, and the banks confirm the prediction: only **3** are new, the per-clan run-cycle aggregators `run{brujah,malknos,otherspc}_pcidles_allsequences`. **Finding: no PC body carries a flex rig** — 0 of all 59, and 0 of the 21 `models/hands/` viewmodels — so the player has no morph targets and no `facial/` sidecar, which is 12.3/12.5's problem to answer for the PC (`facial_animation.md`). Two parser defects fell out and are fixed: `kv.py` and `ElysiumKeyValues.h` both mis-read a quoted value carrying `\"` (`clandoc000.txt`'s Malkavian description), and `kv.py` also mis-read one spanning lines — either shifts every following key/value pair by one, so the next `{` is taken as a value and the block nesting collapses. Mechanised as **`Elysium.Content.PlayerBodies`** | 8.11 |
| PL14 | **Export the first-person hand viewmodels.** `clandoc000.txt` also names `M_Hands`/`F_Hands` per clan — the patch-restored per-clan viewmodels under `models/hands/**` (21 in the merged install) — and PL13 deliberately left them out: they are the first-person half of the body and 8.11a's acceptance is the third-person boom. Same seed function, one more key pair; none carries a flex rig | 8.11a |
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
| RE9 | Screen-fade flag semantics — the client owns the curve, not `env_fade`; `SF_FADE_STAYOUT` uncovers again, and no `OnEndFade`/`ReverseFade` exists (detail:) | B1 | [x] |
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
| RE21 | **`GameFrame` usercmd order** — **movement runs *before* the think pass**, and not in `GameFrame` at all: the engine runs it while draining the client's `clc_move` message (`_Host_RunFrame` → `SV_Frame` `0x200f62b0` → `SV_ReadPackets` → `SV_ExecuteClientMessage` → clc_move `0x200f9990` → `serverGameClients->ProcessUsercmds` → `CPlayerMove::RunCommand` `0x101874a0`), then `SV_Frame` calls `serverGameDLL->GameFrame` `0x1011abc0` (the old `0x10571fc0` is that function's profile *string*) whose body is thirteen calls with `Physics_RunThinkFunctions` third and `ServiceEvents` sixth. Also pins: the player's own think runs inside `RunCommand`, and `frametime`/`curtime` are rebound to the command's timing for the move. **`runtime-architecture.md` §3's tick table now reproduces this order — called as reproduce, landed by 11.11.** Full: `game_runtime.md` §1 | 11.1, **11.11**, 4.7 | [x] |
| RE22 | **The ducked hull** — all six hull/view vectors are literals in the `CGameMovement` ctor (`0x1011e0d0`): ducked is `(-16,-16,0)..(16,16,36)` with the eye at **30** (not stock Source's `VEC_DUCK_VIEW` 28 — a Troika value), standing `(-16,-16,0)..(16,16,72)` eye 64, observer `±10`. `GAMEMOVEMENT_DUCK_TIME` is 1000 ms; `FinishUnDuck` refuses a stand-up that fails a standing-size `TracePlayerBBox`. The same ctor seeds `surfaceFriction` to 1.0. **Also closed alongside it:** VtMB's `PlayerMove` switch has **no ladder arm** — ladder movement is not in this game, and the `"ladder"` string is a footstep material. Full: `source_movement.md` → "The hulls and the view offsets" / "Ladders: VtMB has none" | 4.7, 11.6 | [x] |
| RE23 | **The particle format + the wetness channel** — VtMB's weather is Troika-custom, not Source: no `func_precipitation` anywhere in the install, and the parser lives in a forked `Bin/engine.dll` (gate cvar `particles_enable_precipitation`). The `particles/*.txt` grammar is partly reconstructed (envelope, emitter-vs-particle roles, the `a~b` / `a,b,…` / `v(n)` value forms, the `collide { spawn / decal }` block) — `weather.md` marks what is inferred. Eight open questions, the load-bearing ones being **what `FadeGlobalWetness` actually scales** (`GlobalWetness` crosses into `client.dll`, so it reaches the render side), **who calls it** (survey `out/scripts/`), and whether `func_particle`/`env_particle` take the standard I/O + `start_hidden` surface. No public RE exists — the community FGD defines neither classname and annotates all three wetness keys "Not tested yet...". Full: `docs/weather.md` | 7.9, PL12 | [ ] |
| RE24 | **The sheet math** — all four closed, and two premises were wrong. The substrate first: a trait is `(container, index)` over `CVStatList_t`, **index counting the container's leading `*_Order` block as 0**, so `m_iVAttributes*` is **35** slots (not 21) and `m_iVAbilities*` **13** (not 12) — proven three ways off the datamap and three hardcoded indices. **`AwardExperience`**: `floor(value/100)` confirmed, but `AddExperience` **keeps the sub-100 remainder**, and **give-once is the `m_ExpList` ledger, not the trailing `01`** — every key is give-once; a `> 299` award additionally adds `Experience_Modifier`. **`CalcFeat`** returns a plain int — the *rating*, not a roll — from `Feats::FeatValue`: the sum of a **variable-length** `Base%d` list (`Soak_vs_Bashing` has three, `Damage` none, `"Armor_Rating / 2"` is a per-base `÷`), each entry the *current* value, the nine attributes floored at 1, plus per-feat code terms, a feat-level trait-effect pass, and a clamp to `MaxValue`; `PCWeighting` resolves at load to a `dicerolls.txt` index (all 23 feats → `Normal`). **`BumpStat`**'s third argument is a **repeat count**; it writes the **base** via `IncBase`, under a hardcoded `GetBase < 5` ceiling, and cannot decrement. **There is no Stamina→Health derivation** — `Max_Health` is an authored stat (`Default 100`, no formula in any `vdata` file, no trait effect targeting it) and **`Health` counts damage taken**; NPC tracks are `npctemplate*`'s literal `Max_Health`. Residue: five per-feat override object pointers, null in the image with no writer found. Full: `game_runtime.md` §3; as-built: archive | 9.4b, 9.4c | [x] |
| RE25 | **Chargen math** — all four closed, and the whole chargen surface turned out to live in **`client.dll`**, not `vampire.dll`. **Pools:** seven per-category counters = clan-keyed `rules_tables.txt` `Subpool_*` (zero on every shipped clan bar `Subpool_Disciplines` = 1) **+** the tier table routed through `Attribute_Order_Lookups`/`Ability_Order_Lookups` — so a playable PC spends **2/1/0** attribute dots, **3/2/1** ability dots, **1** discipline dot, over a baseline the wizard *buys* with `giftxp 9000` + `vautolvl <clan>_CharGen`. **Cost:** `Current_Rating` is **pre-purchase** and is the stat's **base**; `Sell(r) ≡ Buy(r−1)`; `New` only for the 0→1 step and never for attributes; `30000` = cannot buy. **The `-1` sentinel** gates the sheet's **row filter** (`0 ≤ v < 6`), not the price — and the `Raise_Clan_Discipline`/`Raise_Other_Discipline` dual formula was **never implemented** (neither string exists in either DLL). **Banes/histories** are the generic trait-effect layer, with its operator enum shipped as data (`traiteffect.txt` `ModifierNames`). Residue: trait-effect stacking order. Full: `game_runtime.md` → "Chargen" / "Buying a dot" / "Trait effects"; as-built: archive | 9.4f | [x] |
| SKY | **Sky + ambience rework, Phases B + C (B1–B8b, C0–C5)** — landed 2026-07-26: backdrop correct + at parity with standing tests (`Elysium.Substrate.SkyCube`/`FogPack`); the whole 3D skybox split by BSP area and placed under its transform; fog from its real owners + Source's own linear distance fog as a per-primitive material term (B8b, D4 amended); the sky light at the map's own authored level (**zero on the 83 no-pair maps**); the bake measured in absolute units — direct light explains ~0% of a median lit face, the bounce floor *is* the ambient level. Open residue promoted to **3.10–3.13 + RE17**. Facts: `sky-ambience.md` | 3.6/3.7 | [x] |

The Ghidra extraction findings behind the closed rows (the RE1/RE2/RE3/RE4 detail: addresses,
datamap shapes, method notes) live in the owning topic docs — `python_bridge.md` and
`entity_io.md`. The extractor workflow is `tools/ghidra/README.md`.

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
| Sidecar space drift (legacy non-`UE_` exporter leftovers) | subtle geometry/logic bugs | 0.4 audit before any new consumer |
| Save determinism erodes | broken saves late | standing rule since P1: no engine timers, own serializable structs |
| Legal posture | project-ending | bring-your-own-game holds; nothing game-sourced committed — standing constraint on every task |
| `GameInputWindows` is a beta plugin with a redist prerequisite | PlayStation pads regress or fail to enumerate on a player's machine | 10.6e authors device configs against the documented VID/PID set and keeps the mapping in `Config/DefaultGameInput.ini` (data, not code); Xbox/XInput remains the fallback path, so a GameInput failure degrades to "PS pads need Steam Input" rather than to no gamepad; `GameInputRedist.msi` is tracked as a 10.5 packaging prerequisite |
| Asset enhancement drifts off-style | silent look regression | `asset-enhancement.md` adjudication test + `elysium.EnhancedTextures` A/B toggle keeps the faithful set as reference; per-family review, not per-texture |
| Modern UI loses VtMB's voice (reads generic/AAA) | the remaster stops feeling like VtMB | 8.6 keeps the original's structure, palette and iconography and re-skins only the craft; presentation test applied per screen; `m0_menu_build.md` + extracted `.res`/scheme (PL8) are the intent reference every screen is checked against |
| "Polish" leaks into the logic layer | silent divergence from retail behaviour, unfindable later | `remaster-direction.md`'s governing rule: RE first, owner's call, dated decision-log entry recording faithful *and* chosen behaviour; default is reproduce, and layer assignment happens before the work, not after |
| No classic-UI mode to A/B against | a UI regression has no reference | the original's structure is captured as data (PL8) and in `m0_menu_build.md`, so screens are checked against intent rather than pixels; the *world* keeps its faithful A/B path unchanged |
| ~~The player stays a pawn + a sheet struct while 9.4/9.8/9.9/9.10/9.5 land on it~~ **(resolved)** | five systems built against a shim, then a five-way migration with saves already in the wild | **11.4 landed ahead of all five**: the sheet is on `FElysiumCombatCharacter`, the durable half is `FElysiumPlayerRecord`, and the shape is VtMB's own (`savegame_format.md`, `script_api.md`) — a port, not an invention |
| ~~Modal screens fight over input mode (three independent owners)~~ **(resolved)** | the mouse is unusable in some screen order; Cog can make the game unclickable | closed by **11.5**: one arbiter (`UElysiumInputSubsystem`) is the module's only `SetInputMode` caller, CommonUI's router is declined explicitly, and `Elysium.Substrate.InputScopes` asserts every ordered screen pair restores and balances |
| P12's facial RE was unknown-duration work that **blocks PP2 in full** (owner call: eyes + lipsync gate the cinematic) | the playable path stalls behind RE | **Retired as a risk — front-loading worked on both halves.** RE19 closed the scene format, event semantics and completion contract; PL9 mirrored the 5,444 scenes + 7,136 `.lip`; **RE20 closed the face** — the flex chunks, both vertex-animation encodings, the `.lip` grammar and the phoneme tables, validated across 3.3 M records with zero failures (`docs/facial_animation.md`). Only PL10 (a mechanical bake) is left in "Now". The residual is a *scope* question, not an RE one: no eyeball data was ever authored, so 12.4's look-at half is an owner call, tracked on the task |
| A shots baseline silently invalidates across a re-bake or content rebuild (measured: up to ~10 mean on bounce-dominated vantages from **byte-identical** inputs) | a look regression hides in toolchain noise — or toolchain noise reads as a regression | B6's measured rule: re-baseline after any bake/content change; A/B a small effect as two runs over one fixed asset set (a cvar A/B), never across a rebuild |

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
