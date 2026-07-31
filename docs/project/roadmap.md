# Elysium-Unreal — Consolidated Master Roadmap

**This document is the master work tracker.** It consolidates and supersedes the plan/tracking
sections of `docs/project/rebuild-strategy.md` (milestones M0–M6, pipeline backlog),
`docs/architecture/debug-tooling.md` (build order), and `docs/architecture/engine-core.md` (Phases 1–2). Those docs remain the
**design/reference detail** behind the tasks here; this doc owns project-wide sequencing,
playable-path priority, and roll-up status.

The private, exact-build retail animation instrument and its resource-to-render
investigation delegate detailed task status to
`docs/project/retail-capture-roadmap.md`; this file
retains the parent rows `0.10`, `RE32`, and `RE33`. That is the only scoped
subtracker. There is no as-built archive and no decision log: git history is the
as-built record, and a decision's outcome is a present-tense fact in the doc
that owns the system.

## How to use this doc

- Status marks: `[ ]` open · `[~]` in progress/partial · `[x]` done (verified) · `[P]` parked
  (deliberately deferred — revisit trigger stated).
- The detailed capture tracker uses the same marks. Any child change that changes a parent
  roll-up updates `0.10`, `RE32`, or `RE33` here in the same change.
- Every open task: **ID — name — why/where — acceptance — deps**. Detail lives in the linked
  design doc or scoped tracker; don't duplicate it here — link it.
- **Landing a task — two writes, and a hard cap:**
  1. Durable how-it-works facts → the **owning design doc** (`docs/CLAUDE.md` maps which).
     This is the only place implementation detail is written in prose.
  2. Flip the checkbox here and replace the open task's body with **one line, ≤2 lines**:
     what landed, and the doc that owns it. Not what was verified in detail, not the deps,
     not the reasoning — those are the design doc's and git's. If a sentence would be true in
     both docs, it belongs only in the design doc.
- **Decisions are not logged anywhere** — a deliberate divergence from VtMB follows the same
  rule: faithful behaviour, what we do instead, marked as a divergence with the owner call,
  written once in the doc that owns the system.
- Old plan IDs (M1–M6, L0–L5, X1/X2, engine-core Phase 1/2) map to new IDs in the
  **traceability table** at the bottom; other docs may still say "M3" — that table resolves it.

## North star and the slice ladder

Rebuild VtMB as a playable game **— remastered —** on UE 5.8 + C++ from this repo's own
exported intermediates; **bring-your-own-game holds** — nothing game-sourced is committed
(strategy and principles: `docs/project/rebuild-strategy.md`). The world's *look* is **baked offline into a
gitignored `.uasset` plugin mount** (`/ElysiumBaked`, regenerable like `$ELYSIUM_EXPORT_ROOT/`) and adopted at
load, while collision, entities, scripting, audio and NPCs stay runtime-built. Everything is proven
on `sp_tutorial_1` first (1,226 entities, 75 classnames — VtMB's own
vertical slice), then scaled across ~100 maps.

**Direction:** the presentation/feel/logic three-layer rule and its adjudication tests are owned
by `docs/project/remaster-direction.md` — read it there, not restated here.

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

**Owner call.** One path to a real, played game drives all
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
| **PP1 — New Game & genesis [x]** | chargen for real: clan, **name**, sex, spends — onto the player entity, Python-readable; `sp_genesisdevice_1` played, not skipped | **9.4 a–g [x]** *(RE24 [x], RE25 [x], RE27 [x], RE28 [x], RE29 [x])*, 8.6's New Game click path |
| **PP2 — the theatre cinematic** | the intro plays start to finish: choreography, scripted camera, line audio, subtitles, **eyes and lipsync — all block** (cont. 5); the PC is on camera, so its body stands here | 12.1–12.5, 8.11a (+ `sp_theatre` export/bake) *(11.7 [x])* |
| **PP3 — land the tutorial** | the chain hands the player to Jack; the first conversation runs with sound and reactions | 9.2, 9.9 |
| **PP4 — core mechanics** | faithful movement (owner call: **in** the path), camera modes, the body's gait, feeding, items + object interaction, dice, the vitals HUD | 4.7, 8.11b, 10.6, B6, 9.8, 9.6, 8.9 |
| **PP5 — persistence** | save / quick / autosave + load mid-run; `trigger_autosave` live | **[x]** *(11.9 = 9.5)* |
| **PP6 — complete the tutorial** | stealth, disciplines, firearms — every retail beat to the exit, proven headlessly | 13.1, 13.2, 13.3 → P9's slice acceptance as `uv run elysium test Play` |

**After PP6 (the thaw):** 9.10 economy/barter, 8.8, 8.10, the P3/P7 look lanes, 10.1–10.5 and
asset enhancement — re-sequenced then.

## Now — the unblocked front

Open tasks whose dependencies are met, ordered by playable-path payoff:

1. **0.10 / RE32 / RE33 / 12.1** — launch the exact retail build with the probe
   active before `sp_theatre` loads and retain one queryable database that joins
   encountered animation/model resources, actors, models, skeletons, every fired
   skeletal contribution, pose-build stages, and final draw matrices. Match the
   observed source identities to the patch-first decoder/export stack, then use
   the first mismatching stage to choose focused skeletal, scene, facial/lip, or
   secondary-motion work. Detailed order and retail evidence gates:
   `docs/project/retail-capture-roadmap.md`.
2. **11.10** — finish PP0 with the played-input harness.
3. **9.8 / 9.9 / 9.10** — inventory, NPC reactions, and economy on the durable player/entity spine.

P12's remaining content and behavior gaps are tracked on its task rows. The lighting/look lane
(3.1–3.13 and the P7 remainder) stays frozen under playable-path rule 2.

## The first-beat path (B*) — landing → the second warp point

The cross-phase priority ladder for the first *playable* game beat: the player lands on
`sp_tutorial_1` and the tutorial's opening runs unassisted up to the second warp. The tutorial's
"warp points" are its teleport stations. **Warp #1** is where the player already lands: the porch
at `teleport_very_beginning` (the `tutorial` `info_landmark` — 8.6a seats the player there).
**Warp #2** is the patch's relocation to the downtown alley: `teleport_fade` (an `env_fade`) fires
`OnBeginFade -> teleport_player.Teleport` + `teleport_jack.Teleport`.

**Data-flow facts:** cross-map placement is `docs/vtmb/level_transitions.md`; entity/output semantics are
`docs/vtmb/entity_io.md`; the dialogue and tutorial beat machine are `docs/vtmb/game_runtime.md`. This ladder keeps
only priority and acceptance status.

Ordered so each step is independently observable with the existing debug layer — after B1 the
warp is console-fireable, after B2 the script warps the player, after B3 Jack stands there and
takes his inputs, after B4 walking off the porch runs the whole beat unassisted, after B5 the
first popup arms itself:

- [x] **B1 `env_fade` fires `OnBeginFade`** — the whole `CEnvFade` class per the decompiled datamap;
  `OnEndFade`/`ReverseFade` do not exist in VtMB. → `docs/vtmb/entity_io.md`.
- [x] **B2 Real entity objects in CPython** *(= 9.3's first slice)* — the `vampire` module's real
  `Entity`/`Player` types over the P1 class-chain tables. → `docs/vtmb/python_bridge.md`.
- [x] **B3 Minimal NPC presence** *(the 8.5 carve-out)* — `FElysiumNpc` + `FElysiumNpcMaker`: NPCs
  stand their real skeletal bodies, latch `WillTalk`, open a dialog session. Feeds but does not
  close 8.5.
- [x] **B4 `.dlg` parser + dialogue runner** *(9.1's core; UI is interim)* —
  `FElysiumDlgConversation` on `StartPlayerDialogRemote`, field-4/5 through the installed host,
  `OnDialogEnd` on close. The interim `SElysiumDialogueBox` is replaced by 9.2. → `docs/vtmb/game_runtime.md`.
- [x] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — `unhidePlus()` resolves and
  `setPlus()` arms `trig_popup_move`, unassisted from map load. → `docs/vtmb/python_bridge.md`.
- [ ] **B6 Feed interaction (post-warp-2 continuation)** — `+use` feed on the blueblood fires
  `OnFedUponBegin`/`OnFedUponEnd`; the maker's `OnFedUponEnd` wires set `G.Tutorial_Blueblood=1`
  and enable `trig_dialog_outside_chopshop`, opening the `Tut_Jack=2` chopshop beat.
  *Acceptance:* feeding on the blueblood enables the chopshop dialogue trigger. *Deps:* B3.

Parallel, non-blocking: 4.7 Source movement (the current pawn walks the beat fine),
`PlayDialogFile` is part of the 6.5/9.2 shared line-service gate (codec decode alone does not make
the script call audible); PL4 batch NPC export
for Jack's real model. B-tasks that are slices of phase tasks (B2/B4/B5) flip here **and** feed
their parent task's status in the same change.

## Foundation baseline

- [x] **M0 — first pixels.** Milestone meaning: `docs/project/rebuild-strategy.md`; task-level completion is
  represented by the phase rows below and the traceability table.

## P0 — Ground truth & de-risk

Cheap tasks that unblock or de-risk everything downstream. Do these before/alongside P1.

- [x] **0.1 Profiling baseline** — `uv run elysium debug profile` / `-ElysiumProfile` over fixed vantages;
  `PCD3D_SM6` confirmed. → `docs/architecture/rendering-perf.md` → "Profiling baseline".
- [x] **0.2 MegaLights engagement check** — MegaLights dominates, many-light cost ~flat, no silent
  VSM fallback, so 3.1 is not urgent. → `docs/architecture/rendering-perf.md` → "Profiling baseline".
- [x] **0.3 Export a second map** — `sm_hub_1` + `sm_pawnshop_1` exported, loaded and profiled;
  unblocked 3.4, 4.6, 10.1, 7.8.
- [x] **0.4 Sidecar space audit** — every consumed sidecar already emitted in Unreal cm; **PL7 is
  empty**. → `docs/project/rebuild-strategy.md` contract table.
- [x] **0.5 Cog 5.8 compile spike** — Cog (upstream `cb1b435`) is restored under
  `Plugins/External/Cog/` by the pinned bootstrap, builds
  and runs clean on 5.8. Full integration is 2.1.
- [x] **0.6 `ent_survey` count reconciliation** — **16,125 outputs / 1,591 Python** on retail;
  pinned across the docs.
- [x] **0.7 Repo hygiene** — `$ELYSIUM_WORK_ROOT/research/ghidra/` + `$ELYSIUM_WORK_ROOT/research/reference-source/` untracked, local-only.
- [x] **0.8 Re-base `docs/vtmb/entity_io.md` on the patch map set** — 108 maps / **71,096 entities / 326
  classnames / 24,081 outputs**. → `docs/vtmb/entity_io.md`.
- [x] **0.9 The uasset-bake architecture** — the world's *look* bakes offline into the gitignored
  `/ElysiumBaked` mount, everything else stays runtime-built. → `docs/architecture/uasset-bake-spike.md`. Residue handed on: **PL11**.
- [~] **0.10 Retail animation RE instrument** — a private one-build launcher/probe
  whose first intermediate goal is one hook-active `sp_theatre` run consolidated
  into a queryable capture database: relevant resource loads, actor/model/skeleton
  identity, every fired source animation contribution, BASE/FINL state, and final
  matrices. The observed source identities join to the patch-first export/decoder
  stack. Editable raw byte-span recipes are added only when the first mismatching
  stage needs them; there is no public compatibility surface or migration system.
  Detailed tasks and acceptance:
  `docs/project/retail-capture-roadmap.md`.

## P1 — Entity substrate *(design: `docs/architecture/engine-core.md` — read it; steps here are the tracker)*

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

## P2 — Debug layer *(design: `docs/architecture/debug-tooling.md` Layers 1–2)*

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
  over the engine `ModelContextProtocol` plugin; on by default in dev builds. → `docs/architecture/debug-tooling.md`
  Layer 3.
- [x] **2.8 Automation tests (both tiers)** — the `Substrate` (`-nullrhi`) and `Content`
  (self-skipping) tiers in `Private/Tests/`, driven by `uv run elysium test`.
- [x] **2.9 Screenshot-regression harness** — `FElysiumShotRun` (`-ElysiumShots`) over the
  profiler's vantages + `pipeline/src/elysium_pipeline/validation/shots_diff.py` baseline promote/diff. A baseline is only valid
  against a fixed bake *and* fixed content assets.

**Slice acceptance:** in standalone — browse entities, pick the elevator call button through
the crosshair, hand-`ent_fire` its chain, watch beams + queue window, pause/single-step,
`elysium.reload` after a re-export without restarting. The P4 test harness exists before P4.

Deferred (tracked, not scheduled): dynamic console autocomplete of targetnames
(`UConsole::BuildRuntimeAutoCompleteList` via custom viewport client); Gameplay Debugger
category; Remote Control channel; NetImgui remote — see Options.

## P3 — Lighting correct & engaged *(parallel lane; detail: `docs/architecture/rendering-perf.md`, `docs/vtmb/lighting.md`)* — **FROZEN** *(playable-path rule 2; gameplay-blocking rendering bugs excepted)*

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
  `docs/vtmb/sky-ambience.md`.
- [ ] **3.6 Pinned exposure** *(was L2.2)* — fixed EV/bias on the per-map post-process;
  deterministic LDR framing. C3's per-map PPV (tagged `elysium.ppv`, adopted unbound) is the
  natural home, and auto-exposure is already off in config
  (`r.DefaultFeature.AutoExposure=False`) — this task pins the *value* per map. *Deps:* none.
- [ ] **3.7 Grade/tonemapper fidelity** *(was L2.3 + M1 polish)* — stop the filmic curve
  crushing the look (neutralize the tone curve or re-fit; the test `.cube` LUT was stripped
  2026-07-26, so any grade re-enters only through this task + `elysium.GradeIntensity`); add
  `elysium.*` A/B toggles for sky/LUT/fog. **Sky orientation is done** — `docs/vtmb/sky-ambience.md`
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
  emissive, ~90% were kept by hand against the classifier's fill call (`docs/vtmb/light-attribution.md`)
  — the only map where the two genuinely disagree, holding 104 of 409 fill candidates
  project-wide. A 15-light shortlist of the highest-confidence disagreements sits in the map's
  eastern strip (X > 7000), numerically indistinguishable from lights killed in the west; the
  framing under test is that the classifier asks "authored as fill?" while the hand survey (live
  rig, Lumen on) answers "does the scene survive without it?" — the two diverge where fill is
  *load-bearing* (faking sky/city-glow ambient with nothing emissive nearby to bounce off).
  Judge the shortlist by in-engine A/B (MCP teleport + screenshot per light), now that C2 zeroed
  the map's SkyLight (no pair — its "sky glow" is all sprayed fill, *more* load-bearing than
  before) and C3's Skylight Leaking is the landed, measured replacement to gate against. Kill a
  fill only where GI demonstrably replaces it, recorded per map in `docs/vtmb/light-attribution.md`. Follow-on
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

## P4 — Interaction *(design: `docs/architecture/engine-core.md` class ladder + `docs/vtmb/animation_and_movers.md` Part B)*

- [x] **4.1 Mover base** — `FElysiumMoverBase` (constant-velocity, swept
  `LinearMove`/`AngularMove` on the substrate clock — the kinematic body pushes the pawn and
  reports blockers) + `FElysiumDoorBase` (the CBaseDoor 4-state machine:
  Open/Close/Toggle/Lock/Unlock/Use, `wait` autoclose, the locked path, blocked-while-closing →
  damage + reverse) + the prototype `func_door_rotating` leaf. *Chaos push/block feel still needs
  an in-game play test.* *Deps:* 1.5, 1.6.
- [x] **4.2 `func_button`** — `FElysiumButton` over the mover primitive (press → `OnPressed` →
  spring-back / latch / toggle); spawnflags reconciled against the decompiled `CBaseButton::Spawn`
  (`0x100`=TOUCH, `0x400`=USE — the `docs/vtmb/entity_io.md` label swap fixed). The minimal +use look-cursor
  (`UpdateUseCursor`/`PlayerUse`, pawn `E` key) lands with it, firing `OnIn`/`OnOut` on aim
  enter/leave. *Deps:* 4.1.
- [x] **4.3 `func_door` / `func_door_rotating`** — the sliding `FElysiumFuncDoor` leaf (movedir
  translation by its own depth minus `lip`, per the decompiled `CBaseDoor::Spawn`); the full
  spawnflag table honoured (`START_OPEN`/`REVERSE`/`LOCKED`/`NO_AUTO_RETURN`/**`PUSE`**); the
  doorknob `DoorUse()` path with **`linked_door`** partner mirroring; mover runtime state in the
  Cog Inspector via the new `GetDebugState` hook. Renderable BSP submodels are split from the
  static world, baked as unplaced local meshes, and attached to their runtime hulls, so translation,
  rotation, hiding and teardown carry collision and visuals together. `use_override` delegates
  once through the target's normal Use path and fails closed; PASSABLE keeps use/debug traces while
  dropping pawn/physics collision. *Verified:* the tutorial front doors and elevator door have
  annotated brush meshes; substrate coverage exercises attachment, override, PASSABLE and dormancy.
  *Deps:* 4.1.
- [x] **4.4 `+use` verb + use-icon HUD** — a dedicated use-only trace channel (`ElysiumUse`,
  `ECC_GameTraceChannel1`) + the context-icon HUD: `use_icon`/`locked_icon` base fields,
  `GetUseIcon()` locked resolution, ring + icon cell drawn from the PL3 atlas
  (`$ELYSIUM_EXPORT_ROOT/hud/use_icons.*`); the 72-entry enum in `ElysiumUseIcons.h`; a `+use` section in the Cog
  Inspector. PL3 (use-icon atlas export) done. *Deps:* 4.2, PL3.
- [x] **4.5 Tutorial logic classes** — the tutorial's logic/point/brush + trigger classes as
  decompile-grounded leaves (`ElysiumLogicClasses.cpp`): `math_counter`, `logic_timer`,
  `logic_case` + the VtMB-divergent **`logic_case_toggle`** (`InValue` matches case strings while
  the added `InValueDelta` advances a configured-case pointer), `env_fade`, `func_brush`, `point_teleport`,
  `trigger_hurt`/`trigger_look`/`trigger_autosave`; the Source `COutput<T>` value seam
  (`FireOutput` fills an empty map-param); the `Elysium.Logic` Cog window.
  `trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` stay inert (their
  backing systems don't exist yet; environmental audio is owned by 6.7). *Deps:* 1.6.
- [x] **4.6 `trigger_changelevel` + landmark travel** — cross-map travel through a shared
  `info_landmark`, translation-only, grounded in the decompiled `CChangeLevel`: a touch or a
  scripted `ChangeLevel` fires `OnChangeLevel`, captures the player's source-landmark offset +
  view yaw, and `UElysiumMapSubsystem::RequestLandmarkTravel` runs the deferred travel next tick,
  seating the player at `dest_landmark + offset`. The scripted `ChangeMap()` is real;
  `elysium.map <map> [landmark]`; a Transitions section in the Maps window. *Verified headless:*
  tutorial → `sm_pawnshop_1` at `dest_newgame + offset`. *Deps:* 1.6, 0.3.
- [x] **4.7 Source movement component** *(was M1.1; parallel-capable)* — port `CGameMovement`
  friction/accel/airaccel/StepMove into a `UCharacterMovementComponent` override
  (`docs/vtmb/source_movement.md`). **Faithful first** — this is the feel
  layer's known-good baseline and the thing every later tuning delta is measured against, so it
  lands line-by-line from the decompile and stays A/B-able (`docs/project/remaster-direction.md` axis 3).
  Frame-rate independence, high-polling-rate mouse input and FOV control ride along (identical
  behaviour, modern plumbing); any *behavioural* delta — accel curves, air control, step feel —
  is a separate, owner-approved decision after this runs. **The body must be a box, so this is not a
  `UCharacterMovementComponent` override**: `ACharacter` creates a capsule root that cannot be
  substituted, and a capsule's rounded bottom reports ~0.65 against `StepMove`'s `0.7` standable test,
  rejecting every climb (`docs/vtmb/source_movement.md`). **11.6 [x]** re-based the pawn to `APawn` + box +
  `UElysiumMovementComponent` and supplies the `FElysiumUserCmd` this consumes; what is left here is
  the line-by-line port — the gravity half-step split, the timestep, ducking and
  water, against 11.6's Source-shaped shell. **`surfaceFriction` is closed**: it is 1.0 on every
  world surface in retail (VtMB scales the material's friction by 1.25 and clamps to 1.0; 1 of
  11,624 VMTs carries a `$surfaceprop`, so everything is the `default` prop at 0.8), so the shell's
  hardcoded 1.0 is already faithful and there is nothing per-surface to export
  (`docs/vtmb/source_movement.md`). **The two divergences this task carried are both settled by RE**
  (`Host_FilterTime` `0x2008ba30`, decompiled): (a) **the frame-delta bound is `[0.001, 0.1]`
  seconds** — VtMB clamps `host_frametime` to a hard 10 fps floor before the game sees it, so the
  port pins that one number across `FElysiumTimeControl::AdvanceFrame` and the mover, which both
  take Unreal's raw delta today. (b) **There is no tick.** `Host_FilterTime` bounds a *variable*
  frametime and returns — no accumulator, no fixed-interval loop — which confirms
  `docs/vtmb/game_runtime.md`'s pre-tick finding from the pacing side and retires the "fixed 66.7 Hz tick"
  (that is *modern* Source's default). **A fixed-step accumulator is therefore a
  divergence, not the baseline** — it ships behind `elysium.move.FixedStep` (default 0 = faithful
  variable delta), recorded in `docs/vtmb/source_movement.md`. **And the residual frame-rate dependence is
  smaller than the shape of the code suggested**, measured over `uv run elysium debug move` at 60/120/240 Hz: the
  jump apex is a flat **25.00 units at every rate** — the half-step split plus VtMB's *additive*
  `CheckJumpButton` (`0x101226b0`, `v.z += impulse`, not an overwrite) is exact velocity-Verlet, and
  the old `25 − 100·dt` prediction was what a *full*-step gravity would give. What does drift is
  air control alone: a strafe-jump exits at 259.5 / 259.8 / 261.0 u/s across the three rates, ~0.6%.
  **RE22 is closed** and the ducked hull is
  `(-16,-16,0)..(16,16,36)` with the eye at 30 (not stock Source's 28), over `TIME_TO_DUCK` 0.4 /
  `TIME_TO_UNDUCK` 0.2. **Ladders are out of scope, not deferred**: VtMB's
  `PlayerMove` switch has no ladder arm and no map places a ladder entity, so there is nothing to
  reproduce. Water movement exists (`WaterMove` `0x101200c0`) and its wish-velocity build, `0.8`
  speed clamp, `40` idle sink and friction step are transcribed, but no exported map places a water
  brush, so it lands formula-faithful and unexercised; the accel tail and the `WaterJump` pair are
  located, not read. Sequenced **in
  the playable path (PP4)** by owner call — the tutorial is
  played with VtMB feel, not UE feel. (c) **An owner call this task carried, now
  discharged:** RE21 pinned retail as movement-*first*, called **reproduce** and landed by **11.11** — the mover already runs before the think pass, on the user
  command's own delta, with the player's own think ahead of it. Port onto that order; the frame is
  no longer moving under this task.
  **As built:** the math is `Public/ElysiumMoveSolve.h` (constants + the `CGameMovement` formulas as
  free functions + `FElysiumMoveTuning`'s `sv_*` console surface + `FElysiumMoveStepper`), the state
  machine is `UElysiumMovementComponent` in `PlayerMove`/`FullWalkMove` order; the frame bound is
  `ElysiumFrame::ClampFrameDelta` in `ElysiumGameClock.h`, read by the clock, the router and the
  mover. *Verified:* `Elysium.Substrate.Movement` (content-free, the formulas and the hulls) plus
  **`uv run elysium debug move`** — a new `-ElysiumMove` harness replaying fixed command streams over eight courses
  against real geometry, with `pipeline/src/elysium_pipeline/validation/move_diff.py` as the comparator (`--save` promotes a baseline,
  `--hz` does the cross-rate check). It caught two real defects the unit tests could not: the jump
  overwriting `v.z` instead of adding to it, and courses inheriting the previous course's velocity.
  *Remaining:* the `stairs`/`slope`/`doorway` courses are sited on placeholder coordinates and need
  surveyed vantages (`elysium.campos`) before their baselines mean anything; ducked speed uses
  Source's `/3` rather than a read-out VtMB value, since retail's is animation-driven.
  *Deps:* 11.6, 11.11.
- [ ] **4.8 Rotating/linear/elevator family** — `func_elevator` is implemented from its recovered
  datamap/handlers: one-based `GotoFloor`, constant-speed vertical travel, lock/current/target
  state, start/pass/arrival outputs and sounds, same-floor completion, ignored mid-move retargets,
  and destination-rest persistence. `prop_button` supplies the tutorial car controls with recovered
  lock/use/state/output/skin behavior. Still open: `func_rotating` (spin-up/down, hurt-touch),
  `func_movelinear`, keyframed movers, and **the mover-push remainder 11.11**: movers are `MOVETYPE_PUSH`
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
  deliver instead of `[no input]`. *Next:* 6.7's completed music state machine drives `events_world`'s six
  music outputs. *Deps:* 1.6.
- [~] **4.10 `game_sign` / `prop_sign` — sign windows** — **`game_sign` + PL5c landed:**
  `UE_extract_signs.py` (278 definitions + 57 background materials → `$ELYSIUM_EXPORT_ROOT/signs/`), the shared
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

## P5 — Scripting foundation *(design: `docs/vtmb/python_bridge.md`, `docs/project/rebuild-strategy.md` B6)*

- [x] **5.1 PL2: copy scripts + dialogue** — `UE_extract_scripts.py` mirrors the 41 loose `.py`
  → `$ELYSIUM_EXPORT_ROOT/scripts/` and the 147 `.dlg` → `$ELYSIUM_EXPORT_ROOT/dlg/` verbatim, patch-first, whole-game (runs once at
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
  real `tutorial.py`. Full rationale: `docs/vtmb/python_bridge.md`. *Deps:* 5.1, 5.2. → **9.3.**

**Slice acceptance:** the tutorial's field-6 calls and `logic_pythoncheck` gates actually
execute (e.g. `FindPlayer().ClearActiveDisciplines()` runs, `OnTrue`/`OnFalse` fire);
`G` flags flip visibly in the debug layer.

## P6 — Audio foundation *(facts: `docs/vtmb/audio_pipeline.md`; design: `docs/architecture/audio-architecture.md`; parallel with P5/P7)*

- [x] **6.1 MS-ADPCM decode** — runtime WAV decode via the vendored single-header `dr_wav`
  (MS-ADPCM / IMA / PCM16 → int16); `FElysiumSoundCache` (decoded-PCM cache,
  `USoundWaveProcedural` per play — the reliable 5.8 route) + `UElysiumAudioSubsystem`
  (`PreviewSound2D`, `elysium.playsound`/`sound_info`) + the `Elysium.Audio` Cog window;
  `UE_extract_sounds.py` mirrors each map's referenced WAVs into `$ELYSIUM_EXPORT_ROOT/sound/`. *Deps:* none.
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
- [ ] **6.5 Final loose-audio core + catalog** — replace whole-file/game-thread-shaped playback
  with `docs/architecture/audio-architecture.md`'s request/handle service: one canonical case-insensitive resolver,
  PL17's duration/codec/reference catalog, worker decode, byte-budgeted PCM LRU for short sounds,
  bounded streaming buffers for dialogue/music/radio, generation-safe handles, owner + map-epoch
  cancellation, completion carrying actual audio start/duration. **Acceptance:** a long MP3 never
  exists as whole-file PCM; prefetch/decode does no game-thread file/codec work; map travel cancels
  every old-map request; forced small buffers exercise underflow diagnostics without a stale-handle
  crash. *Deps:* 6.1, 6.2, PL17.
- [ ] **6.6 UE mixer graph + mix policy** — committed/regenerated game-agnostic Sound Classes,
  Submixes, reverb return, attenuation profiles, concurrency/virtualization and Audio Modulation
  buses; user master/music/dialogue/ambience/SFX/UI control, dialogue ducking +
  `flag_no_voice_duck`, `Dry`, pause/`NoPause`, category priority, selective occlusion. Retire the
  per-play attenuation allocation and make `elysium.Mute` a debug override rather than the
  default-on gate. **Acceptance:** the Audio debugger names the request's class/submix/buses,
  category sliders survive restart, dialogue ducking exempts an authored voice, loops resume from
  virtualization without restarting, and a shipping-config launch is audible. *Deps:* 6.5.
- [ ] **6.7 Map ambience, music + DSP closure** — finish every authored `ambient_generic` flag/
  envelope/lifetime path; make SoundScheme transitions deterministic and honor `Dry`, `NoPause`,
  `RandomSoundCount` and `RoomDSP`; drive `events_world`'s six music outputs from real explore/
  alert/combat state; one listener-zone resolver combines scheme DSP with
  `trigger_environmental_audio`. Settle RE30/RE31 and classify the 15 unresolved wires found by
  `audio_surface_survey.py`; never silently swallow one. **Acceptance:** scheme trigger pairs in
  tutorial + hubs crossfade without duplicate stems, the tutorial's authored `room_type` volumes
  change and restore DSP, and every exported map's point/scheme controls either resolve or carry an
  explicit optional-content disposition. *Deps:* 4.5, 4.9, 6.5, 6.6, RE30, RE31.
- [ ] **6.8 Gameplay audio adapters** — typed `Character`/`Openable`/`Switches`/`Computer`/
  `Weapons` event resolution (never a bare `soundgroup` lookup); NPC sentences, whispers,
  `SetSoundOverrideEnt`/`SetFakeSilence`; surface footsteps/impacts/scrapes; item/weapon/discipline
  `SoundData`/`SoundFX`; radio/news; and the separate AI-hearing event from
  `sound_volume_table.txt`. Dialogue and scene line presentation remain 9.2/12.2, consuming the same
  line service. **Acceptance:** one door, computer, NPC voice set, alternating surface footstep,
  weapon shot + AI stimulus, whisper, radio loop and news story all resolve through the one request
  ledger with the correct owner/category. *Deps:* 6.5, 6.6 and each owning gameplay caller.

## P7 — Dressing & parity *(Track A completion; parallel lane)* — **open tasks FROZEN** *(playable-path rule 2)*

- [ ] **7.1 Coronas** *(was L3.3 / M2)* — `.sprites` consumer: additive depth-tested
  billboards, StartOff spawnflag filtering. *Deps:* 0.4.
- [x] **7.2 Decals** *(M2)* — `infodecal`s as **deferred `UDecalComponent`s** (owner call — the
  PMC-parity stage skipped; a deferred decal is lit exactly like its host wall, Lumen bounce
  included): the exporter writes a `<map>.decals` projector sidecar (Unreal cm), and the new
  `M_Decal` master (`pipeline/unreal/make_decal_material.py`) + `BuildDecals` spawn one component per line —
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
  (`pipeline/unreal/make_world_materials.py`), the full feature set as named params: `Albedo`,
  `Emissive`+scale, `BumpMap`, `EnvMask`+`EnvStrength` ($envmap → **Lumen roughness**, see
  `docs/vtmb/reflections.md`), `BaseTex2`+`BlendAmount` (WorldVertexTransition via the `.blend` sidecar →
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
  **`docs/vtmb/reflections.md`**; decision:. *Verified:* build + `uv run elysium test`
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
- [ ] **7.9 Weather & wetness** *(facts + design: `docs/vtmb/weather.md`)* — the rain system, never
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
  divergence — owner call, recorded in `docs/vtmb/weather.md`. *Deps:* PL12, RE23 (c–e only; a–b are unblocked).

**Slice acceptance** *(Track A criterion, re-based)*: side-by-side A/B match with the
original game's reference captures (RE17) on `sp_tutorial_1` + hub maps.

## P8 — Characters & UI *(design: `docs/project/rebuild-strategy.md` B5, `docs/project/remaster-direction.md` axis 1; `docs/vtmb/m0_menu_build.md` = structural reference, not a port target)*

- [x] **8.1 PL1: entity-model export** — `UE_bsp_to_scene.py` decodes every static-`.mdl` entity
  model (the prop family; skeletal `npc_*` excluded → 8.2/8.5) into the shared `props/` dir and
  annotates each entity with `model_mesh`. Tutorial: 160 props / 64 models. Runtime consumption is
  8.3/8.4. *Deps:* none.
- [x] **8.2 glTFRuntime adoption spike** — implemented (compiles + links on 5.8; **the
  visual check landed with B3** — Jack + the Sabbat stand their real `.glb` bodies in the
  built game): `rdeioris/glTFRuntime` (MIT) restored under `Plugins/External/glTFRuntime`;
  `mdl_gltf.py` emits standard glTF 2.0, so the plugin reorients at load (the standing `UE_`
  exemption); `UElysiumNpcSubsystem` + `elysium.npc.*` verbs + the `Elysium.NPC` Cog window load
  mesh + skeleton + clip from `$ELYSIUM_EXPORT_ROOT/npc/*.glb`. **Decision confirmed:** glTFRuntime is adopted
  for the NPC track; remaining risk lives in 8.5/PL4 (banks, multi-sequence merge), not the
  plugin. *Deps:* none.
- [x] **8.3 Dynamic props** — `prop_dynamic`(+`_ornament`) stand their decoded static `.mdl`,
  per-instance addressable (ScriptHide/Unhide, body-follow, `Break`); skin families repaint them via
  a baked `PropSkinSet`. Non-solid — collision is 8.4.
- [x] **8.4 Physics props** — `prop_physics` / `phys_hinge` as Chaos rigid bodies on the baked
  `SM_<stem>`; collision and mass reproduce VtMB's own `.phy` exactly. **Chaos settle/push feel +
  hinge swing await an owner in-game play test.** → `docs/vtmb/phy_vphysics.md`.
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
  a menu is up, since `sm_hub_1`'s `havenbum` opens a conversation unprompted. Design: `docs/architecture/ui-architecture.md`; the RE it is checked against: `docs/vtmb/vtmb-ui.md`
  (the two UI stacks, both schemes, the **1024×768** canvas law, the HUD class inventory, and four
  corrections to `docs/vtmb/m0_menu_build.md`). **PL8** [x]. The **Nocturne** type set (Spectral SC /
  Spectral / Inter, SIL OFL, no RFN) ships as generated local `UFontFace` assets
  (`fetch_ui_fonts.py` → `make_ui_fonts.py`). **CommonUI + CommonInput** adopted with widget trees
  in C++ Slate, so **no Widget Blueprint assets**. `UElysiumUISubsystem` +
  `UElysiumMainMenu` + `ElysiumUIStyle`/`Strings`/`Texture`; `elysium.menu [pause]`,
  `elysium.menu.close`, `elysium.MenuVantage`, `elysium.BootMenu`. Six owner calls:.

  Three findings the build forced, all recorded in `docs/architecture/ui-architecture.md`: `make_ui_fonts.py`
  **cannot** run in the headless content commandlet, so the policy export coordinates a
  Slate-enabled editor pass; a `UCommonActivatableWidget` added straight
  to the viewport is **collapsed until `ActivateWidget()`**; and `ElysiumScreenshot::Request` grew a
  `bShowUI` flag because the harness's UI-free capture silently omits every Slate widget — the MCP
  tool now passes true, the regression harness keeps false so baselines hold.

  **Remaining:** the `CommonUIInputData` config asset + gamepad/keyboard nav pass (11.3 routes Esc
  through the player controller and the menu's own `NativeOnKeyDown` precisely because CommonUI's
  Back action needs that asset); New Game click path untested end to end (the seam is wired, the
  console equivalent works); chargen ahead of New Game (9.4). **Open risk:** `uv run elysium debug shots` cannot see
  the UI layer, so 8.9's HUD needs UI-inclusive vantages or its regressions go unwatched.

  **Acceptance:** main menu and pause menu are legible and correctly proportioned at 1080p,
  1440p, 4K and 21:9 with no letterboxing or bitmap-font blur; New Game enters `sp_tutorial_1`
  through the 8.6a seam. *Deps:* PL8 [x]; 8.6a [x] for the seam.
- [x] **8.7 Ropes** — `move_rope`/`keyframe_rope` chains as Verlet `UCableComponent`s; rest length
  and node count reproduce VtMB's own arithmetic. **Open:** never put side by side with the running
  original, and `Subdiv` render tessellation has no analogue on `UCableComponent`. →
  `docs/vtmb/entity_visuals.md`.
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
- [ ] **8.10 Accessibility & options backing** *(`docs/project/remaster-direction.md` axis 4 — additive only;
  changes what the player can configure and perceive, never what the game does)* — full
  key/button remapping + gamepad navigation across the 8.6 component set; UI text scaling;
  subtitle size/background controls; colourblind-safe status colours + a high-contrast option;
  FOV control; real graphics/audio options screens backing settings the engine already exposes.
  Difficulty and balance are **not** in scope here — those are the logic layer. The remapping
  screen is the **10.6g** carve-out: it drives `UElysiumInputUserSettings` —
  `QueryMapKeyInActiveContextSet` for conflicts, `MapPlayerKey` per slot, `ResetAllPlayerKeysInRow`
  for Use Defaults — over three columns (Key/Button, Alternate, Gamepad) and filters its key
  selector against `FElysiumReservedKeys` (`docs/architecture/input-architecture.md`).
  *Deps:* 8.6, 10.6 (input path built).
- [ ] **8.11 The player body** *(design: `docs/vtmb/camera-view-modes.md` → "Player mesh, fade and
  first-person rendering")* — the PC's own skeletal body. The durable record resolves clan, sex,
  and armor slot to the full authored `.mdl` before the player entity spawns; both movement-body
  implementations build and rebuild that model through the NPC/glTF skeletal path. Two carve-outs,
  sequenced apart because they need different things:
  - [x] **a. The body** *(PP2)* — the mesh stands, animates under choreography, and fades on the band.
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
    actor, so the theatre's embrace + trial animate the PC on camera (`docs/vtmb/choreographed_scenes.md`).
    **Landed:** `BodyIdentity` save schema v7 with v6→slot-0 migration; clan/sex/slot model
    resolution before spawn; the same skeletal visual on both movement bodies; runtime `SetModel`
    rebuild/clear; a masked dithered `M_PlayerBody` driven by the camera's `ModelAlpha`; and
    first-person/scripted-camera visibility tests. The aggregate theatre run shows the PC and
    `player_understudy` present, correctly skinned, and animating through the embrace.
    *Deps:* 8.2 [x], 8.5 [x], 11.7 [x], PL13 [x]; 9.4 for real identity.
  - **b. Locomotion** *(PP4, beside 4.7)* — idle/walk/run/crouch driven by movement state.
    `UElysiumNpcAnimInstance` is a two-sequence idle crossfade; a player locomotion blend is new
    work, and the states it blends between are the Source movement port's, so it lands beside 4.7
    rather than ahead of it. *Deps:* 8.11a, 4.7.
  *Acceptance (a):* on `sp_tutorial_1`, `togglecamera` shows the PC's own clan model on the boom,
  dissolving in across `cam_fadeend`→`cam_fadestart` and culled at weight 0; the theatre's scenes
  animate it. *(b):* the gait matches the mover's reported state through a walk/run/crouch pass, and
  a `uv run elysium test Play` beat asserts it.

**Slice acceptance** *(M5 criterion)*: New Game starts from a real, modern menu that is legible
and correctly proportioned from 1080p to 4K and at 21:9; the HUD and the tutorial's popup signs
draw on the same stack; NPCs stand in the world at their entity origins.

## P9 — Dialogue & persistence *(design: `docs/vtmb/game_runtime.md`, `docs/project/rebuild-strategy.md` B7/B9)*

- [x] **9.1 `.dlg` parser + dlgexpr** — `ElysiumDlg.{h,cpp}`: the 13-field parser, the `dlgexpr`
  front-normalizer, the host-agnostic branch machine. NPC col-4 = action, PC col-4 = gate. →
  `docs/vtmb/game_runtime.md`.
- [ ] **9.2 Conversation UI + audio-by-path** — dialogue screen on the 8.6 UI foundation, line
  audio via 6.5/6.6's shared line service. Content is **reproduced verbatim** (lines, conditions, branch structure,
  ordering); presentation modernizes — vector type, reflowing line lists, speaker/emotion cues,
  the 8.10 subtitle path. *Deps:* 9.1, 6.5, 6.6, 8.6.
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
    Character methods still dispatch off the Entity/Player getattro), see `docs/vtmb/python_bridge.md`. The four
    `from vamputil import RandomLine` maps (`santamonica`, `chinatown`, `gallery`, `fusyndicate`) now
    resolve that name. *Verified live via MCP.*
  *Deps:* 9.3a, B2. *Remaining blocked on:* the inventory follow-up, 9.4.
- [x] **9.3b Console bridge — `ccmd` + the `cfg` alias table** *(the fifth scripting surface)* —
  `vampire.ccmd`/`cvar` over a host-agnostic `FElysiumConsole` store seeded from the PL5d `$ELYSIUM_EXPORT_ROOT/cfg`
  mirror, with the console→Python fallthrough. Binding them lets the **real `vamputil.py` import**.
  → `docs/vtmb/python_bridge.md`; the `Character`-shim divergence:.
- [x] **9.3c The script filesystem** — `FElysiumScriptFS` gives the VM its own filesystem namespace:
  reads union the `Saved/` overlay over the `$ELYSIUM_EXPORT_ROOT/` mirror, writes land in the overlay with copy-up,
  escaping the sandbox is the one denial. → `docs/vtmb/python_bridge.md` → "The script file layer".
- [x] **9.7 The script→engine action surface — survey, RE, spec** — **16,860 executable call sites /
  676 called names** surveyed, the `PyMethodDef` tables and datamaps recovered, and `docs/vtmb/script_api.md` written as
  the per-name inventory + demand-ranked build order. **d** landed `OneOfSet` for real (the 589
  dialogue gates now select) and guarded the `Whisper`/`FrenzyTrigger` receiver split. →
  `docs/vtmb/script_api.md`; roll model:.
- [ ] **9.8 Inventory & items** — **853 corpus calls**, the largest gap with no owning task:
  `HasItem` 327 / `RemoveItem` 182 / `GiveItem` 126 / `StartBarter` 108 / `AmmoCount` /
  `GiveAmmo` / `HasWeaponEquipped`, plus the `Inventory_Remove` input and
  `SpawnItemInContainer`/`AddEntityToContainer`. String-keyed against `vdata/items/` (244 files,
  on disk since PL5b); the receiver is the combat character at `+0x9c` (`docs/vtmb/script_api.md`).
  `StartBarter` lags the rest — it needs the barter UI (8.6). *Deps:* 9.7c, 9.4.
- [ ] **9.9 NPC disposition & reactions** — the single largest engine demand in the game,
  **2,862 calls**: `SetDisposition(name, level)` alone is 2,510, 2,467 of them in `.dlg` column 4
  (an NPC line's *action*), plus `SetRelationship` 334 on `CAI_BaseNPC` and
  `React`/`SetExpression`/`SetGesture`. Needs `vdata/dispositiontable` + `reaction*` and an NPC
  emotional-state model; the dialogue runner (B4) is the caller. *Deps:* 9.7c, B4.
- [ ] **9.10 Economy** — **250 calls**: `MoneyAdd`/`MoneyRemove` (INTEGER inputs on the combat
  character) + `CurrentMoney`/`SetMoney`. The smallest self-contained system on the ledger; one
  integer on the sheet plus vendor `worth` when 9.8 lands. *Deps:* 9.7c.
- [x] **9.4 Quests/XP, RPG sheet, character screen, chargen, and genesis** — all seven PP1
  substeps landed. VtMB facts: `docs/vtmb/game_runtime.md`, `docs/vtmb/vdata-catalog.md`; UI design:
  `docs/architecture/ui-architecture.md`; genesis travel and the intro-skip divergence: `docs/vtmb/level_transitions.md`.
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
  `$ELYSIUM_EXPORT_ROOT/vdata/system/`). **Remaining build work:** the C++ resolver loading its
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
- [ ] **10.3 Floor validation `[needs 4060]`** *(was L5.3)* — 1440p/60 on a real RTX
  4060-class 16 GB card; the one gate look cannot judge. *Deps:* P3, 10.2.
- [ ] **10.4 Async travel state machine** — `docs/architecture/map-architecture.md` design (fade → unload →
  task-thread parse → spawn → fade in), built on the 10.8 OpenLevel foundation (the heavy
  build runs in the shell world's `BeginPlay` behind a loading screen); **trigger: when
  synchronous hitches start to matter, not before.** *Deps:* 4.6, 10.8.
- [ ] **10.5 Packaged-build content path** — `content/` next to the exe, packaging story,
  Shipping config sweep (debug layer compiled out), and the **`GameInputRedist.msi`** prerequisite
  10.6e introduces (Windows 10 19H1 floor). The gitignored `/ElysiumBaked` mount joins this
  story (0.9): a package must either ship a bake-on-first-run path or the user-side bake
  tooling. *Deps:* none until first package.
- [ ] **10.6 Input path — Enhanced Input, remapping, first-party gamepad** *(design:
  `docs/architecture/input-architecture.md`; VtMB facts: `docs/vtmb/controls.md`)* — retire the legacy `DefaultInput.ini`
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
    `IA_*`/`IMC_*` assets emitted by `pipeline/unreal/build_content.py`, so `uv run elysium export bundle policy` keeps them in
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
    authoritative; `FElysiumConfigWriter` emits Valve-format text into `$ELYSIUM_EXPORT_ROOT/cfg/config.cfg` so
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
  replacing `info_node`); ragdoll/IK/anim blends; `.emc`-style cache for `.ents` if
  parse time bites; lump-8 lighting bake as a low-end contingency (parked with the dynamic-path
  commitment); retail `.sav` import (needs RE7 wire format — currently a non-goal). For the
  low-end contingency, **Lumen Lite** (5.8's medium-quality irradiance-field GI, ~2× faster,
  runs on PC) is noted as a cheaper alternative to a lump-8 bake path — see Options.
  **vdata-driven gameplay systems** — data already on disk (PL5b, `$ELYSIUM_EXPORT_ROOT/vdata/`); each table's
  consumer + schema is mapped in `docs/vtmb/vdata-catalog.md`, and these are the systems that read
  them: **disciplines/vampire powers** (`disciplinetgt_*`, ~300 KB — the largest; → **13.2**),
  **stealth** (`stealth`/`stealthkillrules`; → **13.1**), the **hacking minigame** (`hackterminals/`),
  **economy/vendors** (`vendors`, item `worth`), **NPC disposition + reactions**
  (`dispositiontable`/`reaction*`), **data-driven conversation camera** (`camerashots/`),
  **radio + TV-news ambient content** (`radio_data`/`newscaster_*` → **6.8**), **impact FX**
  (`particleimpacttable`), **per-category entity sound schemes + AI-hearing volume**
  (`sndscheme_*`/`sound_volume_table` → **6.8**, distinct from PL5a's map SoundSchemes), and the **minor UI
  content tables** (`loadingtips`/`infobartypes`/`mapnames_localized`/`keynames`/
  `interestingplacetypelist`). Promote any to its own task when reached.
- [x] **10.8 OpenLevel map lifecycle** — hard travel opens each generated
  `/ElysiumBaked/<map>/<map>` level; GI-scoped state survives while the old world and its
  per-map runtime state are reclaimed. → `docs/architecture/map-architecture.md`, `docs/architecture/uasset-bake-spike.md`.
- [ ] **10.9 Asset enhancement** — run the offline delight → super-resolve → style-anchored
  PBR pipeline as an `elysium.EnhancedTextures` A/B layer over the faithful world.
  *Acceptance:* toggle-off remains byte-for-byte on the faithful inputs; curated Tier 0/1
  outputs meet the `docs/architecture/rendering-perf.md` floor budget; no game-derived output is committed.
  *Deps:* PP6, 10.3, 7.4. *Design:* `docs/architecture/asset-enhancement.md`; governing test:
  `docs/project/remaster-direction.md`.

## P11 — Runtime spine *(design: `docs/architecture/runtime-architecture.md` + `docs/architecture/save-architecture.md` — read them; steps here are the tracker)*

The structure *between* the systems P1–P10 design: lifetimes, the frame, the player object, the
session, and the seams. It exists because the slice ladder now reaches "boot a New Game and play it",
and that is the one thing no current doc owns. Its rules are **S1–S10** (`docs/architecture/runtime-architecture.md`
§13), orthogonal to `docs/architecture/engine-core.md`'s R1–R8.

Steps are ordered so each compiles, ships and is observable alone. **11.4 was the hinge** — 9.4, 9.5,
9.8, 9.9 and 9.10 all sit on it, and it landed before any of them.

- [x] **11.0 Adopt the spine** — all seven `docs/architecture/runtime-architecture.md` §16 owner calls recorded in; both design docs flipped to adopted. Opened RE21, RE22.
- [x] **11.1 Frame + clock ownership** *(S1, S2)* — the tick table pinned by tick group and
  prerequisite; `FElysiumTimeControl` as the one pause/scale facade over a private-writer
  `FElysiumGameClock`. **Step order superseded by 11.11**; the clock and facade survived unchanged.
- [x] **11.2 World services** — `FElysiumWorldServices` (`IElysiumEmbodiment`/`IElysiumAudio`/
  `IElysiumTravel`/`IElysiumPresenter`) injected into `FElysiumEntityWorld`; no `Cast<AElysiumMapActor>`
  or `GetFirstPlayerController()` left under the substrate.
- [x] **11.3 App state machine + pause + loading + game over** — `UElysiumGameFlowSubsystem` owning
  `EElysiumAppState` and its transition table; **the screen is a pure function of the state**.
  **Remaining:** the game-over copy is invented (VtMB's death screen is un-RE'd).
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
  `vdata/camerashots/`). → `docs/vtmb/camera-view-modes.md`.
- [x] **11.8 Presentation seam** *(S8)* — `UElysiumPresentationSubsystem` rebuilds `FElysiumViewState`
  once per frame and is the production `IElysiumPresenter`; no widget/HUD path references
  `FElysiumEntityWorld`.
- [x] **11.9 Save/load** *(= 9.5)* — `docs/architecture/save-architecture.md` built in full: the versioned compressed
  payload, the four blocks, `EElysiumField::Save` field walk, per-map snapshots against a post-Load
  baseline, and the slot/quick/autosave ring. Verbs: `elysium.save.slots`, `.cansave`, `.delete`,
  `.diff`.
- [ ] **11.10 Play test tier** *(S10)* — a fourth automation tier that drives a real headless world:
  the beat-script driver (`do`/`wait`/`assert`/`shot` over the command registry, injected input, `G`/
  quest/entity predicates and the 2.9 shot baseline), command-stream replay, the save round-trip, and
  the matching MCP tools (`input_inject`, `beat_run`, `save`/`load`, `time`). *Acceptance:*
  `uv run elysium test Play` walks the tutorial opening unassisted and fails loudly when a beat regresses — P9's
  slice acceptance becomes a CI run rather than a manual play-through. *Deps:* 11.6, 2.7, 2.9.

- [x] **11.11 Rework the frame to retail order — move first, then think** *(reversed 11.1's tick
  table)* — **RE21**'s move-first order is the shipped order: sample input → pre-move pass → move →
  gameplay pass → physics → post-move pass → camera → publish, with `FElysiumEntityWorld` driven
  twice a frame. **What the inversion gave up, measured:** a closing door displaces the pawn rather
  than dealing `dmg` or firing `OnBlockedClosing`, so VtMB's authored `MOVETYPE_PUSH` is **4.8's
  remainder**. → `docs/architecture/runtime-architecture.md` §3. Discharged 4.7's owner call (c).
- [x] **11.12 Map activation barrier** — `AElysiumMapActor` owns an explicit
  `Building → WaitingForPrerequisites → Activating → Active|Failed` lifecycle. Entity worlds load
  dormant; a wall-clock watchdog fails closed unless runtime construction, final frozen player
  placement (gameplay maps), tick wiring, and every required async collision cook are complete.
  Activation reconciles final overlaps and runs the initial player/entity/event/audio pass at the
  unchanged game time before `UElysiumMapSubsystem` publishes `MapReady`; app state stays `Loading`
  and a viewport Slate overlay covers the post-`LoadMap` build until that callback. `MapFailed`
  retains the overlay with the missing prerequisite. This is the correctness gate under **10.4**;
  parsing/spawning are still synchronous and 10.4 remains open. → `docs/architecture/map-architecture.md`,
  `docs/architecture/runtime-architecture.md` §3/§10.

**Slice acceptance:** from a cold launch — the menu comes up over the backdrop, New Game runs chargen
and enters the story, the tutorial's opening beats play on rebindable controls with a HUD, Esc pauses,
Save and Load round-trip the run, and `uv run elysium test Play` asserts the whole thing headlessly.

## P12 — The theatre: choreography & faces *(the PP2 rung — everything blocks)*

The intro cinematic (`sp_theatre` — embrace + trial) as VtMB plays it: `logic_choreographed_scene`
driving actors, scripted camera (11.7), line audio, subtitles, and facial animation. The fidelity
bar is an owner call: the scene is not done until the faces are alive — **eyes and lipsync
included**. **RE19** closes the scene format and event semantics
(`docs/vtmb/choreographed_scenes.md`); **PL9** supplies the corpus under `$ELYSIUM_EXPORT_ROOT/scenes/` and
`$ELYSIUM_EXPORT_ROOT/lip/`; **RE20** closes the flex/eyeball chunks, `.lip` grammar, and
phoneme→controller tables (`docs/vtmb/facial_animation.md`). **PL10** bakes the faces into the NPC
export — morph targets in each glb, the flex rig in `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, and
`$ELYSIUM_EXPORT_ROOT/expressions/`. **RE32 and RE33 remain open:** skeletal pose,
scene placement, secondary-motion/physics, and facial/lip runtime equivalence are
consumed here, while their capture-harness tasks, experiments, evidence gates, and current status live in
`docs/project/retail-capture-roadmap.md`. Confirmed behavior remains in
`docs/vtmb/animation_and_movers.md`, `docs/vtmb/choreographed_scenes.md`, and
`docs/vtmb/facial_animation.md`.

RE20 changes what 12.4 can be: **no model in the install carries eyeball data** — the whole
cast ships `NumEyeballs == 0`, so there is no authored eye pose, look-at cone or procedural
lid. Eyes in VtMB are *eyelids*: eight `eyelid` flex controllers driving 16 eyelid flexdescs
through four RPN rules. Blink and lid shaping are reproducible; gaze is not RE-able because
it was never authored.

- [~] **12.1 Choreographed scenes** — `logic_choreographed_scene` as a real class + the scene-file
  parser (PL9) + an event timeline on the game clock, `Start`/`Pause`/`Resume`/`Cancel` inputs and
  the seven outputs; actors resolve **by name** and play through the 8.5 anim seam. Spec:
  `docs/vtmb/choreographed_scenes.md` (RE19) — nine live event types (`speak`, `silence`, `loud`,
  `expression`, `gesture`, `sequence`, `firetrigger`, `python`, `bodysound`), absolute scene time
  offset by the audio mixahead, `position_start`/`position_end` actor placement, and
  `firetrigger "N"` → `OnTriggerN`. *Acceptance:* the theatre's first scene runs its actors and
  fires its completion wires in the built game. *Deps:* 8.5, 11.1, RE19 [x], PL9 [x], PL16 — a
  `SceneFile` resolves to `$ELYSIUM_EXPORT_ROOT/scenes/` + the path with its `sound/` prefix stripped.
  - **Implemented, pending the remaining RE32 material/remap work and live acceptance:** the reader/timeline/entity
    cover all four inputs, seven outputs, nine live event
    types, `active 0`, absolute seek/stop, pause catch-up, exact completion/cancel ordering,
    `position_end == 3`, actor/dialogue/controller binding, diagnostic reset, voice ownership and
    mid-scene snapshot restore without duplicate instantaneous outputs. `camera_track` and
    `camera_keyframe` provide paired value streams, authored timing/easing/four-key interpolation,
    focal conversion, holds/restores/outputs and save state. The PC body resolves before spawn and
    rebuilds at runtime; `npc_VPlayerController` is a real transferable map-epoch entity; generated
    skeletal manifest v4 and `prop_dynamic` cover all seven authored opening prop targets (two
    wineglasses and five stake entities) while
    v3 remains readable. Tests: `Elysium.Substrate.Camera*`, `.SceneParse`, `.SceneTimeline`,
    `.ChoreoScene`, `.OpeningEmbodiment`, `.AnimatedPropManifest`, `.SavePayload`, plus
    `Elysium.Content.SceneCorpus`, `.SceneAnimSets`, `.OpeningCameraTracks`,
    `.OpeningAnimatedProps`, `.OpeningPoseEnvelope`, `.OpeningScenePlacement`,
    `.PlayerBodyMaterial`, and `.TheatreSkeletonBinding` (48/48 runtime USkeleton binds, including
    six player-body binds). Rendered isolation is green for all five bodies and all nine named prop
    clips. The authored-camera `embrace` gate derives 63 current-map samples across every cut,
    movement midpoint, and fade boundary; it applies the six fades, records eight key bones per actor,
    and confirms all five cast members enter frame outside opaque fades. Target-model
    `split_bones` metadata preserves the source flag inventory, but runtime
    application remains disabled. The generated inputs to the removed
    post-crossfade experiment already carried the discarded fixed-quaternion
    rewrite, so its quarter-turn discontinuities do not adjudicate the recovered
    retail rule. `sp_theatre` is exported and baked.
  - **Live acceptance:** `newgame_ttd` activates both camera owners, renders exact-zero edits as
    clean cuts, and carries the authored positive-time moves without the generic look tracker or
    Unreal motion blur turning them into scrolls. The PC and `player_understudy` are visible and
    animate with intact geometry/materials; all seven opening prop targets receive their clips;
    both embrace scenes start and complete exactly once; controller creation, `!playercontroller`,
    removal/transfer, and the next courtroom handoff run. The active courtroom scene resolves all
    four actors. No unexpected actor, clip, camera, NPC, or prop diagnostics remain. Deferred scene
    audio/facial/lip work belongs to 12.2–12.5. `hide_ents` remains behind
    `elysium.SceneHideEnts` (default 0), and the authored missing `controls` target remains a single
    non-fatal diagnostic rather than a synthetic entity.
- [ ] **12.2 Scene audio + subtitles** — per-line audio through 6.5/6.6's shared line service (the
  `PlayDialogFile` file-resolution rules) synced to scene time; a subtitle surface on the view
  state (11.8). *Acceptance:* the scene's lines are audible and subtitled in sync. *Deps:* 12.1,
  6.5, 6.6, 11.8.
- [ ] **12.3 Facial flex track** — the morph targets are baked (PL10 [x]); what is left is the
  three layers above them, which are runtime evaluation: 44 flex controllers → 60 RPN flex rules
  → 65 flexdesc weights → the per-flex target ramp → the morph weight. All four inputs are in
  `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, index-aligned with the glb's morph targets;
  `UElysiumNpcAnimInstance` grows a morph-track player over the body animation. Two load
  contracts the bake fixes: a morph that spans two materials arrives as one same-named piece per
  primitive, so the skeletal-mesh config must set `MorphTargetsDuplicateStrategy::Merge`, and a
  morph target is one *flex record*, not one flexdesc — the eyelid pairs hinge a single flexdesc
  into two ramps. Spec: `docs/vtmb/facial_animation.md`. *Acceptance:* a flex authored in the model
  moves the face in-game. *Deps:* 8.5, RE20 [x], RE33, PL10 [x].
- [ ] **12.4 Eyelids** *(was "Eyes")* — blink + lid shaping off the eight `eyelid` controllers
  and their four rules (`raiser × (1 − droop·0.8) × (1 − blink)` and its complements). **There is
  no eyeball data to consume** — RE20 found `NumEyeballs == 0` on all 4,444 models, so eye posing
  and look-at have no faithful baseline. *Acceptance:* actors blink and their lids shape through
  the theatre scene. *Open owner call:* whether to add gaze/look-at at all — it is an invention
  under `docs/project/remaster-direction.md`'s Feel layer, not a reproduction, so it needs an owner call
  recorded in `docs/vtmb/facial_animation.md` before it is built. *Deps:* 12.3, RE33.
- [ ] **12.5 Lipsync** — `.lip` phoneme tracks (RE20 [x]; 9.3c already logs the scripts' `.lip`
  probes as a named divergence) driving mouth flexes against 12.2's line audio; the 7,136 files
  are on disk in `$ELYSIUM_EXPORT_ROOT/lip/` (PL9 [x]), keyed by the line's own sound path. A **three-file join
  per line**: the `.lip` for phoneme timing, `expressions/<model stem>_phonemes.txt` for the
  phoneme→controller weights (249 tables, chosen by the actor's model basename), and
  `mstudiomouth_t` for the amplitude-driven jaw that runs alongside. Key on the phoneme
  *string* — the `.lip` numeric code is not stable across the corpus. All three inputs are on
  disk: `$ELYSIUM_EXPORT_ROOT/lip/`, `$ELYSIUM_EXPORT_ROOT/expressions/` (the 249 `.txt` tables, PL10 [x]) and `mouths` in
  `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`. Spec: `docs/vtmb/facial_animation.md`. *Acceptance:* mouths move
  with the words on every theatre line. *Deps:* 12.2, 12.3, RE33.

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
retail end to end, and `uv run elysium test Play` proves it headlessly.

## Pipeline backlog (indexed; owned by phases above)

| ID | Task | Needed by |
|---|---|---|
| PL1 [x] | `.ents`-referenced model export landed; visual contract: `docs/vtmb/entity_visuals.md`. | 8.1 [x] |
| PL2 [x] | Loose scripts and dialogue are mirrored under `$ELYSIUM_EXPORT_ROOT/`; formats: `docs/vtmb/python_bridge.md`, `docs/vtmb/game_runtime.md`. | 5.1 [x] |
| PL3 [x] | The use-icon atlas and enum metadata are exported; semantics: `docs/vtmb/entity_io.md`. | 4.4 [x] |
| PL4 [x] | NPC models and shared animation banks are batch-exported with include resolution. | 8.5 [x] |
| PL5 [x] | Sound schemes, rulebook data, and sign assets are mirrored to their runtime sidecars. | 6.3, 9.4, 4.10 [x] |
| PL5b [x] | The patch-first `vdata/` rulebook mirror landed; consumer map: `docs/vtmb/vdata-catalog.md`. | 9.4, 9.6, 10.7 [x] |
| PL5d [x] | Patch-first `cfg/*.cfg` mirroring landed; contracts: `docs/vtmb/controls.md`, `docs/vtmb/python_bridge.md`. | 9.3b [x] |
| PL6 | Texlight merge in exporter | 3.4 |
| PL11 | Remove the dead Lumen-card path the bake superseded (found by 0.9): `export_all.py`'s `bake_cards`/`--no-cards` calls a `cards.bat` that no longer exists and prints a "skipped" line every run; `ElysiumCardGen.cpp` (`ELYSIUM_WITH_CARDGEN`, `elysium.cards.probe`) still builds into editor targets. Nothing depends on either | 0.9 |
| PL12 | Mirror `particles/*.txt` (**1,594**) + the `particles/*.tga` sprite set (**309**) verbatim → `$ELYSIUM_EXPORT_ROOT/particles/` — patch-first, wired into `export_all.py`. Weather is the immediate consumer (33 rain definitions) but the set is engine-wide: fire, muzzle flashes, disciplines, the menu background. Also bake the top-down occlusion height map per map from `<map>.obj` + `worldspawn`'s `world_mins`/`world_maxs` (1024², ~11 cm/texel on `sm_hub_1`). Format: `docs/vtmb/weather.md` | 7.9 |
| PL13 [x] | All 56 player bodies are exported from the clan table; animation/facial implications live in `docs/vtmb/animation_and_movers.md` and `docs/vtmb/facial_animation.md`. | 8.11 |
| PL15 [x] | The Masquerade meter is a sub-rectangle of `cm_topbar`; no asset is missing. → `docs/vtmb/vtmb-ui.md`. | 8.9 |
| PL14 | **Export the first-person hand viewmodels.** `clandoc000.txt` also names `M_Hands`/`F_Hands` per clan — the patch-restored per-clan viewmodels under `models/hands/**` (21 in the merged install) — and PL13 deliberately left them out: they are the first-person half of the body and 8.11a's acceptance is the third-person boom. Same seed function, one more key pair; none carries a flex rig | 8.11a |
| PL16 [x] | Cinematic animation sets are exported and split into actor-addressable banks. → `docs/vtmb/choreographed_scenes.md`. | 12.1 |
| PL17 | Build the patch-first audio catalog + typed sidecars: codec/channel/rate/frame/duration metadata, complete static reference closure, parsed map + entity sound schemes, sentences/surfaces, item/discipline events, radio/news, case collisions and missing refs. Raw game audio remains gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`. → `docs/vtmb/audio_pipeline.md`, `docs/architecture/audio-architecture.md`. | 6.5–6.8, 9.2, 12.2 |
| PL18 | Resolve the four structured NPC-export source warnings: the absent generic Night Watchman doppleganger model and the truncated skeletal records in `bottleb`, `bottlec`, and `stage_light`. The current export records all four; the three props use their successfully decoded static `model_mesh` fallback. → `docs/vtmb/animation_and_movers.md`. | 8.5, 12.1 |
| PL7 [x] | The sidecar-space audit found no fixes: all consumed sidecars are already Unreal centimetres. | 0.4 [x] |
| PL9 [x] | Choreographed scenes and `.lip` files are mirrored patch-first. → `docs/vtmb/choreographed_scenes.md`, `docs/vtmb/facial_animation.md`. | 12.1, 12.5 |
| PL10 [x] | NPC flex data, morph targets, facial sidecars, and expression tables are exported. → `docs/vtmb/facial_animation.md`. | 12.3–12.5 |
| PL8 [x] | The UI layouts, schemes, strings, menu scene, and art inventory are exported for the re-skin. → `docs/vtmb/vtmb-ui.md`. | 8.6 |

## RE backlog (reverse-engineering work; each cited where consumed)

| ID | Question | Consumed by | Status |
|---|---|---|---|
| RE1 | Trigger/button spawnflag semantics are recovered. → `docs/vtmb/entity_io.md`. | 4.2, 4.5 | [x] |
| RE2 | Retail services thinks before queued events. → `docs/vtmb/game_runtime.md`. | 1.4 | [x] |
| RE3 | Attribute writes, error-to-false, and `G` default-zero are recovered. → `docs/vtmb/python_bridge.md`. | 5.2, 9.1 | [x] |
| RE4 | Ghidra datamap export (validate our input/field tables vs retail) — method confirmed, CBaseEntity base map extracted. **4.5 proved the fast path: `run.ps1 -Script DumpGrep` (str=/cls= anchors) over the persisted `vtmb` project, no re-import — recovered every P4.5 class factory/datamap and settled `logic_case_toggle`'s delta-advance divergence.** | 4.5+ (optional, valuable) | [~] |
| RE5 | The dice resolver is verified. → `recovered/dice-system.md`. | 9.6 | [x] |
| RE6 | Retail entity-I/O survey counts are reconciled. → `docs/vtmb/entity_io.md`. | 0.6 | [x] |
| RE7 | Retail `.sav` block wire format | 10.7 (only for importing retail saves) | [P] |
| RE8 | The entity-I/O survey is rebased on the patch-loaded map set. → `docs/vtmb/entity_io.md`. | 0.8 | [x] |
| RE9 | Screen-fade flags and outputs are recovered. → `docs/vtmb/entity_io.md`. | B1 | [x] |
| RE10 | Source sky-face orientation is settled. → `docs/vtmb/sky-ambience.md` K1. | 3.7 | [x] |
| RE11 | The labelled-sky in-game probe confirms the recovered orientation. → `docs/vtmb/sky-ambience.md`. | 3.7 | [x] |
| RE12 | Model lighting and lump 15's runtime role are recovered. → `docs/vtmb/sky-ambience.md` K3/K5. | 3.7, 10.1 | [x] |
| RE13 | VtMB carries one lightmap bake; the apparent day/night arrays are unused. → `docs/vtmb/sky-ambience.md` K4. | 3.7 | [x] |
| RE14 | The full-game sky inventory is measured. → `docs/vtmb/sky-ambience.md`. | 3.7, 10.1 | [x] |
| RE15 | VRAD's sky transfer and lump-8 absolute scale are recovered. → `docs/vtmb/sky-ambience.md` K6. | 3.7, 10.1 | [x] |
| RE16 | The sky brightness chain is settled. → `docs/vtmb/sky-ambience.md` K7, `docs/vtmb/color_gamma.md`. | 3.7 | [x] |
| RE17 | **Owner-run reference captures** *(was sky-ambience RE-A6)* — original-game screenshots at the shared vantages (3–4 sky maps + one sky-only view per skyname), for the **world** half of the display ratio (`albedo × lightmap × 2` beside a sky texel — the sky's own transfer is the identity, RE16) and as 7.8's reference. **Gate cleared (SDK cross-reference, pending VtMB binary confirmation):** `snapshot` grabs pre-gamma-ramp — capture and the hardware gamma ramp are separate D3D surfaces that never touch. → `docs/vtmb/color_gamma.md` → "Screenshot capture happens before the gamma ramp". What is left is the owner actually running the captures | 3.6/3.7, 7.8 | [ ] |
| RE18 | The script→engine action inventory and demand ranking are recovered. → `docs/vtmb/script_api.md`. | 9.7–9.10 | [x] |
| RE19 | Choreographed-scene format, binding, timing, and completion semantics are recovered. → `docs/vtmb/choreographed_scenes.md`. | 12.1 | [x] |
| RE20 | MDL facial data, flex rules, eyeball absence, and `.lip` format are recovered. → `docs/vtmb/facial_animation.md`. | 12.3–12.5 | [x] |
| RE21 | Player commands run before the think/event pass; the full frame order is recovered. → `docs/vtmb/game_runtime.md`. | 11.1, 11.11, 4.7 | [x] |
| RE22 | Player hull/view constants and the absence of ladder movement are recovered. → `docs/vtmb/source_movement.md`. | 4.7, 11.6 | [x] |
| RE23 | **The particle format + the wetness channel** — VtMB's weather is Troika-custom, not Source: no `func_precipitation` anywhere in the install, and the parser lives in a forked `Bin/engine.dll` (gate cvar `particles_enable_precipitation`). The `particles/*.txt` grammar is partly reconstructed (envelope, emitter-vs-particle roles, the `a~b` / `a,b,…` / `v(n)` value forms, the `collide { spawn / decal }` block) — `docs/vtmb/weather.md` marks what is inferred. Eight open questions, the load-bearing ones being **what `FadeGlobalWetness` actually scales** (`GlobalWetness` crosses into `client.dll`, so it reaches the render side), **who calls it** (survey `$ELYSIUM_EXPORT_ROOT/scripts/`), and whether `func_particle`/`env_particle` take the standard I/O + `start_hidden` surface. No public RE exists — the community FGD defines neither classname and annotates all three wetness keys "Not tested yet...". Full: `docs/vtmb/weather.md` | 7.9, PL12 | [ ] |
| RE24 | Sheet storage, feat/XP math, and health semantics are recovered. → `docs/vtmb/game_runtime.md` §3. | 9.4b–c | [x] |
| RE25 | Chargen pools, costs, sentinel behavior, and trait-effect source are recovered. → `docs/vtmb/game_runtime.md`. | 9.4f | [x] |
| RE26 | Trait-effect accumulation and dialogue sex gates are recovered. → `docs/vtmb/game_runtime.md`, `docs/vtmb/python_bridge.md`. | 9.4c–f, B4 | [x] |
| RE27 | Quest addressing, journal replacement, and award ordering are recovered. → `docs/vtmb/game_runtime.md`. | 9.4d | [x] |
| RE28 | Chargen close unpauses and teleports into the authored genesis exit. → `docs/vtmb/game_runtime.md`, `docs/vtmb/level_transitions.md`. | 9.4g | [x] |
| RE29 | Entity-name matching is case-insensitive with final-`*` prefix semantics. → `docs/vtmb/entity_io.md`. | entity I/O | [x] |
| RE30 | Recover `trigger_environmental_audio` touch behavior and the precedence/interpolation among its `room_type`, SoundScheme `RoomDSP`, and the player's networked `m_sndRoomDSP`/`m_sndPlayerDSP`. → `docs/vtmb/audio_pipeline.md`. | 6.7 | [ ] |
| RE31 | Recover the SoundScheme RandomSound frequency scheduler/distribution and transition edge cases; the current approximate curve is not a faithful baseline. → `docs/vtmb/audio_pipeline.md`. | 6.7 | [ ] |
| RE32 | Capture one source-attributed `sp_theatre` run from pre-map resource loads through actors/models/skeletons, every fired skeletal contribution, pose-build stages, and final render matrices in one queryable database. Join observed owner/sequence/animation identities to exact patch-first bytes and current export/decoder output, then trace only selected mismatches through decoding, blends/remaps, scene placement, root/entity motion, procedural work, hierarchy, and render handoff. Detailed status and experiments: `docs/project/retail-capture-roadmap.md`; facts: `docs/vtmb/animation_and_movers.md` and `docs/vtmb/choreographed_scenes.md`. | 8.5, 8.11, 12.1 | [~] |
| RE33 | Trace expression, VCD/audio, and `.lip` resources from source bytes through runtime objects, controller mixing, flex rules/ramps, eyelids, amplitude mouth, vertex deformation, and render submission. Detailed status and experiments: `docs/project/retail-capture-roadmap.md`; facts: `docs/vtmb/facial_animation.md`. | 12.3–12.5 | [~] |
| SKY | The sky/ambience rework is complete; remaining work is tracked as 3.10–3.13 and RE17. Facts: `docs/vtmb/sky-ambience.md`. | 3.6, 3.7 | [x] |

The Ghidra extraction findings behind the closed rows (the RE1/RE2/RE3/RE4 detail: addresses,
datamap shapes, method notes) live in the owning topic docs — `docs/vtmb/python_bridge.md` and
`docs/vtmb/entity_io.md`. The extractor workflow is `research/tooling/ghidra/driver/README.md`.

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

## Risk register

| Risk | Impact | Mitigation |
|---|---|---|
| MegaLights silently disengages (VSM fallback) | frame collapses | 0.2 check lives in the profiling routine forever; 3.1 pins it |
| Embedded VM fails to start in a packaged build | whole scripting surface silently error-to-false | 9.3a: `MakePreferredScriptHost` checks the VM actually started and falls back to the expr host, logging a warning, rather than leaving every eval Void |
| Chaos kinematic movers push/block poorly | doors feel wrong | 4.1 prototypes one door first |
| Floor perf unproven (no 4060/16 GB card on hand) | late surprise | validate 1440p/60 at 10.3; lump-8 bake remains a parked contingency; Lumen Lite is the cheaper option |
| Tutorial-only calibration bias | rework on other maps | 0.3 second map early; all calibration provisional until 10.1 |
| Sidecar space drift (legacy non-`UE_` exporter leftovers) | subtle geometry/logic bugs | 0.4 audit before any new consumer |
| Save determinism erodes | broken saves late | no engine timers; use owned serializable structs |
| Legal posture | project-ending | bring-your-own-game holds; nothing game-sourced committed — standing constraint on every task |
| `GameInputWindows` is a beta plugin with a redist prerequisite | PlayStation pads regress or fail to enumerate on a player's machine | 10.6e authors device configs against the documented VID/PID set and keeps the mapping in `Config/DefaultGameInput.ini` (data, not code); Xbox/XInput remains the fallback path, so a GameInput failure degrades to "PS pads need Steam Input" rather than to no gamepad; `GameInputRedist.msi` is tracked as a 10.5 packaging prerequisite |
| Asset enhancement drifts off-style | silent look regression | `docs/architecture/asset-enhancement.md` adjudication test + `elysium.EnhancedTextures` A/B toggle keeps the faithful set as reference; per-family review, not per-texture |
| Modern UI loses VtMB's voice (reads generic/AAA) | the remaster stops feeling like VtMB | 8.6 keeps the original's structure, palette and iconography and re-skins only the craft; presentation test applied per screen; `docs/vtmb/m0_menu_build.md` + extracted `.res`/scheme (PL8) are the intent reference every screen is checked against |
| "Polish" leaks into the logic layer | silent divergence from retail behavior | `docs/project/remaster-direction.md`: RE first, owner call, and faithful/chosen behavior recorded once in the owning topic doc; default is reproduce |
| No classic-UI mode to A/B against | a UI regression has no reference | the original's structure is captured as data (PL8) and in `docs/vtmb/m0_menu_build.md`, so screens are checked against intent rather than pixels; the *world* keeps its faithful A/B path unchanged |
| A shots baseline silently invalidates across a re-bake or content rebuild (measured: up to ~10 mean on bounce-dominated vantages from **byte-identical** inputs) | a look regression hides in toolchain noise — or toolchain noise reads as a regression | B6's measured rule: re-baseline after any bake/content change; A/B a small effect as two runs over one fixed asset set (a cvar A/B), never across a rebuild |

## Traceability (old plan IDs → this doc)

| Old | Here |
|---|---|
| M0 | Foundation baseline |
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
