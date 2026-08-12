# Elysium-Unreal — Consolidated Master Roadmap

**This document is the master work tracker.** It consolidates and supersedes the plan/tracking
sections of `docs/project/rebuild-strategy.md` (milestones M0–M6, pipeline backlog),
`docs/architecture/debug-tooling.md` (build order), and `docs/architecture/engine-core.md` (Phases 1–2). Those docs remain the
**design/reference detail** behind the tasks here; this doc owns project-wide sequencing,
playable-path priority, and roll-up status.

The private, exact-build retail animation instrument and its resource-to-render
investigation delegate detailed task status to
`docs/project/retail-capture-roadmap.md`; this file
retains the parent rows `0.10`, `RE32`, and `RE33`.

The skeletal animation asset programme — the character asset bake, the shared skeleton, layer masks,
blend spaces, and the extraction and bake of the action catalog — delegates detailed task status to
`docs/project/animation-roadmap.md`; this file retains the parent row `8.5`.

The player-feel vertical — the mover's published body state, the resolver seam, the player animation
graph, the camera service and player rig, input response, and the gym that measures them — delegates
detailed task status to `docs/project/three-cs-roadmap.md`; this file retains the parent rows `4.7`,
`8.11b`, `10.6` and `11.13`.

Those three are the only scoped subtrackers. There is no as-built archive and no decision log: git
history is the as-built record, and a decision's outcome is a present-tense fact in the doc that
owns the system.

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
| **PP4 — core mechanics** | faithful movement (owner call: **in** the path), modern persistent first-/third-person camera, the body's gait, feeding, items + object interaction, dice, the vitals HUD | **`docs/project/three-cs-roadmap.md` CCC0–CCC9** *(4.7, 11.13, 8.11b, 10.6)*, B6, 9.8, 9.6, 8.9 |
| **PP5 — persistence** | save / quick / autosave + load mid-run; `trigger_autosave` live | **[x]** *(11.9 = 9.5)* |
| **PP6 — complete the tutorial** | stealth, disciplines, firearms — every retail beat to the exit, proven headlessly | 13.1, 13.2, 13.3 → P9's slice acceptance as `uv run elysium test Play` |

**PP4's feel stack runs ahead of PP2 and PP3 — owner call.** The three C's are built and proven as
one vertical before the theatre cinematic and the tutorial landing. The reason is recovered rather
than preferential: VtMB's `m_flMaxSpeed` comes from the current sequence's own root motion, so the
animation is the movement's speed authority and the mover cannot be closed behind it
(`docs/project/three-cs-roadmap.md` → "The ladder"). The rest of PP4 — feeding, items, dice, the
vitals HUD — keeps its place in the sequence.

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
2. **The 3 C's slice** — `docs/project/three-cs-roadmap.md` CCC0 onward: the gym and its channels,
   the mover's published body state, the camera service and player rig, then the resolver seam and
   the player animation graph. PP4's feel stack, promoted ahead of PP2/PP3 by owner call.
3. **11.10** — finish PP0 with the played-input harness. It gates no rung of the 3 C's slice:
   `CCC8` is an owner-played acceptance by owner call, not a beat script.
4. **9.8 / 9.9 / 9.10** — inventory, NPC reactions, and economy on the durable player/entity spine.

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
- [x] **B5 `ccmd` + the `cfg` alias table** *(= 9.3b + PL5d)* — `unhidePlus()` resolves through
  Elysium's Plus-profile selector and `setPlus()` arms `trig_popup_move`, unassisted from map load;
  the source install's personal Basic/Plus choice does not alter the runtime profile.
  → `docs/vtmb/python_bridge.md`.
- [x] **B6 Feed interaction (post-warp-2 continuation)** — the feed command, acceptance policy,
  paired state machine, pulse transaction and maker child output wiring landed; feeding the
  `FastFood` blueblood through the first-press/start, second-press/release path enables the chopshop
  trigger from the authored `OnFedUponEnd` wire in automated substrate/content acceptance. A played
  `sp_tutorial_1` acceptance run is not claimed. →
  `docs/vtmb/feeding.md`, `docs/architecture/gameplay-systems-architecture.md`.

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
- [~] **0.10 Retail animation RE instrument** — a private one-build launcher/probe.
  It produces hook-active `sp_theatre` cutscene runs consolidated into one queryable
  capture database per run, carrying actor/model/skeleton identity, every fired
  source animation contribution, BASE/FINL state, and final matrices, bracketed by
  the cutscene's own trigger and map-transition events. An offline calibration reads
  a finalized database read-only and reports its integrity, rates, census, and derived
  run zero, so filter and storage decisions are measured rather than estimated. A second
  offline reader joins the draw and skeletal streams on the measured fixed +4 between the
  render-info entity field and the `C_BaseAnimating` instance, so a captured pose and the
  draw it produced name one actor. Entry/exit brackets on the client pose builder and on
  both engine frames that submit a studio draw stamp a per-thread generation on every record
  they enclose, and a third offline reader reports assignment coverage, bracket integrity,
  and whether the enclosing generation and the carried instance identity agree. Every record
  in a full run is assigned and the two attributions never disagree, so a pose group is
  scoped by construction rather than by pointer lifetime. A third stream censuses the models
  themselves: one observation per sighting of a studio header at an address and one model
  image per checksum, so the bytes a pose was decoded from survive the run and a captured
  runtime pointer becomes a model-image offset. A fourth offline reader decodes those images
  against the pipeline's own bone decoder and against the patch-first install, and proves the
  runtime header is the `.mdl` image at offset 0 while naming the narrow ranges the loader
  patches in place. A fourth stream censuses the actors: identity per sighting keyed by the
  entity address, plus the construction and destruction the shared client-entity pair
  witnesses, so an address join is bounded by the window in which that address meant that
  actor rather than by the address alone. Every actor a full run posed is identified before it
  is used, every reused address is accounted for, and the composed pose now retains the frame
  it is placed into. A fifth stream names the source of every fired contribution: the frames
  below the virtual-model resolver take the owning studio header as an argument, so a
  contribution records the owner model, the owner-local sequence and animation indices, the
  active blend cells and the weights the runtime itself resolved, the cycle, the pose
  parameters, the selected-bone mask and its immediate caller, with repeated calls kept as
  separate events. A fifth offline reader proves every contribution is scoped to a pose build,
  every owner reached the model census with an image, and every captured descriptor pointer
  equals the index and stride it claims. Two complete cutscenes reproduce it with no dropped,
  faulted or unscoped record, which also closes the second-acquisition requirement. An offline
  walker then converts each fired contribution's witnessed pointers into spans of the owning
  model image and is checked against the probe's own witnesses rather than against itself, and
  a sixth stream records the server-side scene requests, so a pose group is attributable to
  what asked for it through a handle both modules record independently. A finalized database
  is finally deduplicated by content hash and indexed: each distinct payload is stored once
  behind a view that returns the original bytes, and the joins the readers used to rebuild
  per-tool become one spine keyed on the generation every record already carries, with the
  integrity roll-up stored so the database reports its own counts. A last offline reader turns
  the run outward and joins every fired identity to the patch-first installed bytes, the current
  export and the player inventory, writing a report beside the database rather than into it,
  because that join depends on the machine's install and export state. Every identity and every
  contribution record of one cutscene resolves to installed bytes, each captured pointer lands
  where the installed header — a copy the probe never touched — declares its array position, and
  the descriptors are byte-identical to the captured copies outside the one dword the loader
  rewrites. What the current export cannot name is the product: the fired sequences it reaches,
  the owner models no export seed reaches, and the blend cells it never bakes. A final offline
  reader then differences the bytes retail dereferenced against the bytes the pipeline's own
  decoder reads, measured by running that decoder unmodified under observation rather than by
  describing it, so the two sides are not one walker agreeing with itself. Re-walking the
  stored span union reproduces it byte for byte, and the difference is five fields: the
  per-bone animation weight the zero test reads, and the blend-grid extents, pose-parameter
  indices, cell count and fired cells beyond the base one. Bytes read by neither stay recorded
  as unknown. Open: the per-bone transform difference that join makes possible.
  Header residency stays as it is: there is no
  single model-cache free to hook, so an unload event would cost three brackets and still
  report absence rather than a per-model free, and it is parked until a difference report
  names it. Editable raw byte-span recipes are added
  only when the first mismatching stage needs them; there is no public
  compatibility surface or migration system.
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
  (`AcceptInput`, `FElysiumEventQueue::Add`), the `FElysiumIOSink` taps, stable equal-time FIFO,
  recursive same-frame drain, reverse repeated-row firing, authored-`times=0`-as-unlimited
  normalization at parse, stable entity-list fan-out order for duplicate targetnames, the
  backward-`curtime` enqueue guard (save-versioned `EventClock`), and the single-result
  leading-`!` path with `!pvsplayer` are live; the `Elysium.Substrate` ordering suite
  (`OutputRowOrder`, `QueueDrainOrder`, `RecordServiceOrder`, `OutputTimes`,
  `LateBindingAndDrops` over `FElysiumOrderedIOSink`) asserts the delivery contract. The
  10,000-event safety cap is the intentional divergence: it defers a due tail to the next
  think-first frame while retail has no gameplay cap and can starve/hang the current frame;
  loop-guard trips are counted and marked in the I/O history.
- [x] **1.5 Brush bodies** — `UElysiumBrushComponent` per brush entity, per-classname solidity,
  overlap → `OnTouchStart`/`OnTouchEnd`; disabled/hidden/dead trigger state is physical, touch
  admission precedes deduplication, ends release first, and runtime teleport containment is a
  deterministic post-movement end-then-begin diff. Frozen map arrival retains its immediate
  activation reconciliation.
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
  (`0x100`=TOUCH, `0x400`=USE — the `docs/vtmb/entity_io.md` label swap fixed). Use-enabled buttons
  participate in the shared post-move interaction pipeline, firing `OnIn`/`OnOut` on focus
  enter/leave and consuming the same user-command edge as doors/model controls. *Deps:* 4.1.
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
- [x] **4.4 `+use` foundation + use-icon HUD** — `use` is one `FElysiumUserCmd` button pair for
  keyboard, gamepad, console and replay (`E`, `RB`). The controller queues command edges; the
  post-move entity pass settles focus first, fires focus transitions, and then begins/releases the
  captured logical entity. `IElysiumEmbodiment` owns a component→entity anchor registry and the
  exact-first modern camera/body query: 225 cm body reach, dual LOS, a restrained 2.5-degree assist
  cone, 25 percent assisted-focus hysteresis, deterministic ties, and `elysium.UseAssist` exact-only
  diagnosis. Only focus/prompt/session state persists; no nearby-candidate subsystem exists.
  Brush doors/buttons use their bodies and `prop_button` uses query-only visual bounds, with
  dormancy removing anchors. The presentation seam publishes `FElysiumInteractionView`; the PL3
  ring/icon atlas fades 0.10 s in and 0.15 s out and labels the active CommonInput binding with
  `E`/`RB` fallback. Modal/cinematic/HideHUD rules suppress it. Headless coverage proves command
  edge/replay identity, deterministic selection, focus/session lifecycle, hidden/locked model
  buttons, exact/assist/hysteresis, adjacent controls, body reach, occlusion, offset cameras, and
  teardown. Live tutorial first-/third-person acceptance remains to run. The owner-called modern
  Feel divergence is recorded beside the faithful facts in `docs/vtmb/entity_io.md`. *Deps:* 4.2,
  PL3.
- [x] **4.5 Tutorial logic classes** — the tutorial's logic/point/brush + trigger classes as
  decompile-grounded leaves (`ElysiumLogicClasses.cpp`): `math_counter`, `logic_timer`,
  `logic_case` + the VtMB-divergent **`logic_case_toggle`** (`InValue` matches case strings while
  the added `InValueDelta` advances a configured-case pointer), `env_fade`, `func_brush`, `point_teleport`,
  `trigger_hurt`/`trigger_look`/`trigger_autosave`; the Source `COutput<T>` value seam
  (`FireOutput` fills an empty map-param); the `Elysium.Logic` Cog window.
  `point_teleport` caches its live activation transform, implements spawnflag `1`, refuses parented
  targets, restores all angles atomically, persists its cache, and resolves `!activator` at input.
  The late `Activate` pass runs after final frozen player placement; runtime-spawned entities enter
  it immediately once the world is active.
  `trigger_environmental_audio` already inherits the physical `CBaseTrigger` disabled/admission
  contract, while its room presentation stays inert and is owned by 6.7;
  `trigger_stealth_mod`/`trigger_inventory_check` remain inert until their backing systems exist.
  *Deps:* 1.6.
- [x] **4.6 `trigger_changelevel` + landmark travel** — cross-map travel through a shared
  `info_landmark`, translation-only, grounded in the decompiled `CChangeLevel`: a touch or a
  scripted `ChangeLevel` fires `OnChangeLevel`, captures the player's source-landmark offset +
  view yaw, and `UElysiumMapSubsystem::RequestLandmarkTravel` runs the deferred travel next tick,
  seating the player's Source feet at `dest_landmark + offset`. Authored spawns and landmarks are
  feet-space; the stage and legacy save payload remain capsule-centre space, converted once when
  the pawn body exists. The scripted `ChangeMap()` is real;
  `elysium.map <map> [landmark]`; a Transitions section in the Maps window. *Verified headless:*
  tutorial → `sm_pawnshop_1` at `dest_newgame + offset`. *Deps:* 1.6, 0.3.
- [x] **4.7 Source movement component** *(was M1.1)* — `CGameMovement` ported line-by-line from the
  decompile onto an `APawn` + box hull, because `ACharacter`'s capsule cannot pass `StepMove`'s
  standable test. → `docs/architecture/movement-architecture.md` (the object graph, the A/B, the
  divergences, the measured frame-rate residual) and `docs/vtmb/source_movement.md` (the VtMB rules).
  *Verified:* `Elysium.Substrate.Movement` plus `uv run elysium debug move`.
  *Remaining:* delegated — the sited courses' surveyed coordinates and the gait/ducked speed
  authority are `docs/project/three-cs-roadmap.md` CCC0 and CCC7. *Deps:* 11.6, 11.11.
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
  **RE43/RE44 close the retail leaf:** `prop_sign` has one exclusive player session, fires
  `OnReadBegin`, loads `definition_file`, selects the first true dependency redirect, and fires
  `OnReadEnd` when use ends. **Still open:** implementing that `prop_sign` +use path,
  `NewspaperData`/multi-column, `ClientCommand`, `fade_out` linger, `pause` semantics +
  `spawnflags 5` undecoded; real `.fnt`-role type is 8.8. *Acceptance (rest):* `+use` on
  `sign_chopshop_upstairs` reads "password: chopshop"; a dispatch-wrapper sign picks its variant
  from `G`. *Deps:* 4.4, 1.6, PL5c; 5.2 for the redirect.
- [~] **4.11 Close the trigger and `+use`-prop I/O gaps RE35 exposed** — four separable pieces.
  (a) **`trigger_hurt` cadence is wrong**: retail deals `damage × 0.5` on entry then `damage × 3.0`
  every 3.0 s (sustained rate = `damage`); the runtime deals `damage` on entry then `damage` every
  0.5 s, i.e. double damage at six times the tick rate. Register `HurtNow`, `SetDamage`, `OnHurt`
  and `OnHurtPlayer` (6 + 11 shipped wires) while there.
  (b) **Trigger `filtername` is live:** late activation resolves the retained handle and
  `filter_activator_name` plus AND/OR `filter_multi` reject before touch-pair deduplication.
  (c) **`prop_switch` and the lockable family have no inputs at all** — 49 shipped wires land
  nowhere, and because the classnames are already claimed the stub registrar skips them, so they are
  invisible to `elysium.stubs`. `CPropSwitch` is a sequence player over `activate`/`deactivate`/
  `idle_on`/`idle_off` whose `OnActivate` (117 wires) fires on clip end, not on use; the four
  lockable classes share one `CBaseLockableEnt`/`CBaseVampireSkillEntity` implementation.
  (d) **The doorknob handle sequence** is a virtual the *door* calls, which is why an animated
  doorknob stands in bind pose. Knobs, switches, signs, terminals, and containers intentionally
  expose no player-focus anchor until these specialized leaf behaviors own the interaction; they
  plug into 4.4 without another selection-system redesign. → `docs/vtmb/entity_io.md`. *Deps:* 4.1,
  8.3.

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
- [x] **7.4 Master-material set (rest)** *(was M1.2)* — the generated surface masters
  (`M_World_Opaque`, `M_World_Masked`, `M_World_Translucent`, `M_World_Glass`, `M_Refract`,
  `M_Additive`) from one generator
  (`pipeline/unreal/make_world_materials.py`), the full feature set as named params: `Albedo`,
  `Emissive`+scale, `BumpMap`, `EnvMask`+`EnvStrength` (the `$envmap` inputs whose translated
  response is owned by 7.5, see
  `docs/vtmb/reflections.md`), `BaseTex2`+`BlendAmount` (WorldVertexTransition via the `.blend` sidecar →
  vertex colour; `.emc` bumped to EMC2). The runtime picks the master per blend flag and binds
  per-channel. Substrate + content tests (439 tutorial materials). *(7.5 supersedes the reflection
  half of this entry, and the `elysium.*` material knobs now reach the baked instances through
  `ApplyMaterialOverrides` rather than the factory.)* *Deps:* none.
- [ ] **7.5 Real reflections** *(was L3.1)* — the `$envmap` RE and whole-game inventory are
  complete: VtMB's composite is `(base + cube·mask·tint) · lightmap · 2`, an albedo term the light
  multiplies, with no Fresnel; props are the larger half of the reflective set (1,419 of 2,610
  VMTs). Presentation acceptance is open. The fixed roughness/specular-only translation preserves
  neither the source cube's radiance nor `GlobalWetness`'s tint semantics and produces an unusable
  high-frequency direct-specular response in the `sm_hub_1` wetness A/B. Target: one graph samples
  the exported cube through the raw linear mask as a primary-view additive term for the source
  endpoint, excludes that view-dependent term from Lumen's Surface Cache, and crossfades it against
  a coarse-mask roughness/specular response to the live scene. The first acceptance surface is the
  14 patch-first `sm_hub_1` wet materials; general world/prop cubes and the 102 chromatic-tint metal candidates
  follow the same contract after that slice. The existing green build/tests, ten-map rebake, and
  0.15–0.22 ms Lumen measurement prove the superseded PBR-only path, not this acceptance. Full:
  **`docs/vtmb/reflections.md`**. *Deps:* 3.5, 7.4.
- [ ] **7.6 Bloom/glow tuning** *(was L3.2)* — VtMB's overbright neon/selfillum vs pinned
  exposure. *Deps:* 3.6.
- [ ] **7.7 Shadow quality** *(was L3.4)* — contact shadows on hero lights, penumbra softness,
  within 0.1 budget. *Deps:* 0.1, 3.1, 3.2.
- [ ] **7.8 A/B capture harness** *(was L5.1, re-based)* — scripted fixed-camera captures vs
  **the original game** at the shared vantages (RE17's protocol — the original is the only
  reference). The local half exists (2.9 +
  `shots_diff.py`); respect its measured noise floor — re-baseline after any bake or content
  rebuild. *Deps:* 0.3, 3.9, RE17.
- [ ] **7.9 Weather & wetness** *(facts + translation boundary: `docs/vtmb/weather.md`)* — the
  verified `sm_hub_1` work is retained as data and logic: strict patch-first particle/VMT closure,
  versioned weather sidecar, corrected 2048² R16 cover map, serializable wetness/emitter ramps,
  `env_particle` I/O/fanout, and rain audio fades. The patch-first logic/controller slice is live
  through one `IElysiumWeather` service: authored transitions drive the environment MPC, and the
  `Elysium.Environment` override never alters or pauses entity/save state. Material presentation
  acceptance now shares 7.5's one-graph contract: bind `cubemapdefault` and a linear mask for the
  source endpoint; keep the view-dependent cube out of the Lumen Surface Cache; use a coarse mask
  for the enhanced PBR response; and coordinate source retain, wet roughness/specular, and darkening
  under the single `RainEnhancement` value. The panel must expose those values plus mask/cube/source
  debug views before tuning. `env_particle` presentation remains data-only while RE23 settles
  `attach_type=11`, `bounds`, distribution, lifetime/keyframe units, blend/mask semantics, retail
  appearance, and the wetness interpolation units. Sewer drips, fixed `func_particle` boxes, NPC
  shelter behavior, lightning, other maps, and a general particle runtime remain deferred.
  *Deps:* 7.5 material slice, PL12, RE23.

**Slice acceptance** *(Track A criterion, re-based)*: side-by-side A/B match with the
original game's reference captures (RE17) on `sp_tutorial_1` + hub maps.

## P8 — Characters & UI *(design: `docs/project/rebuild-strategy.md` B5, `docs/project/remaster-direction.md` axis 1; `docs/vtmb/m0_menu_build.md` = structural reference, not a port target; skeletal animation: `docs/project/animation-roadmap.md`)*

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
- [~] **8.4a Close the `prop_dynamic` divergences RE35 exposed** — the animation half is done; the
  collision and shadow halves are not.
  - [x] (a) **Skeletal placement basis.** The animated representation took `Def->ModelQuat`, the
    OBJ placement quaternion, while every other glTF body composes the fixed −90° glTFRuntime
    offset. `ElysiumSkeletalBasis::FromPlacementQuat` composes it — `Placement * ModelFix` rather
    than a yaw-only substitution, because 15 of the corpus's 152 animated-prop placements are
    leaning palms whose pitch and roll a yaw-only form would flatten. The static path is unchanged.
  - [x] (b) **The rest pose** is now a *held* pose: `SelectWeightedSequence(ACT_IDLE)` with retail's
    sequence-index-0 fallback, played non-looping and seeked to frame 0 through the existing
    `SeekCinematicClip` seam. Only 3 of 19 prop models tag `ACT_IDLE`, so the index-0 branch is what
    fires for the theatre's cinematic props.
  - [x] (c) **`CDynamicProp::Activate`** landed as a `PostSpawn` override resolving `LoopSequence`
    and arming a `RandomFloat(0.1, 0.99)` stagger consumed by a pending-start flag in `Think`, which
    fires `OnAnimationBegun`.
  - [x] (d) **`SetAnimation` honours the clip's own `STUDIO_LOOPING` bit** instead of forcing one
    shot.
  - [x] (e) **The revert-to-`LoopSequence` is dropped — reproduce.** Retail's think disarms
    permanently on the finish frame, so the branch is unreachable in shipped data and a finished
    one-shot holds its final frame. No shipped placement changes behaviour.
  - [ ] (f) **`solid` is unread** — 699 of 903 shipped `prop_dynamic` placements build static
    collision from the model's `.phy` in retail and are walk-through here.
  - [ ] (g) **`disableshadows` is unread** — 849 props cast shadows the author switched off.
  - [x] (h) **A resolved animated entry that bakes no clip** no longer displaces the static mesh.
    The exporter keeps such models out of the index (PL18) and `BuildBody` guards the same case.

  Manifest v6 carries each prop's clip vocabulary inline in the model's own declaration order with
  its selection keys, because the v4/v5 alphabetical sort chose the wrong sequence 0 for
  `drknobantique`, `clamp` and `wolf_form`. Tests: `Elysium.Substrate.AnimatedPropPlacement`,
  `.PropRestPose` coverage inside `.PropAnimateThink`, `.PropZeroClipFallback`,
  `.AnimatedPropManifest`, `.OpeningEmbodiment`, `Elysium.Content.OpeningAnimatedProps`, and
  `pipeline/tests/test_animated_props.py`. → `docs/vtmb/entity_io.md`, `docs/vtmb/phy_vphysics.md`,
  `docs/vtmb/entity_visuals.md`. *Deps:* 8.3, 12.1.
- [x] **8.5 NPC presence + native locomotion + `scripted_sequence` minimal** — NPCs stand, patrol/use authored places, and scripted Walk travels through the existing motor at its selected clip's decoded ground speed; decoded animation-event playback and perception/combat AI remain open implementation. → `docs/vtmb/entity_io.md`, `docs/vtmb/animation_and_movers.md`. **The animation half — how a clip is selected, blended and layered — is owned by `docs/project/animation-roadmap.md`; what stays here is the motor, the route and the entity behaviour.**
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

  **Landed — the unified player UI and main menu run.** `UElysiumPlayerUISubsystem` owns one
  `UElysiumUIRoot` per local player: passive HUD, transient, notification, game-modal,
  system-modal and runtime-loading containers in structural paint order. Main/pause, character,
  chargen and dialogue share `UElysiumActivatableScreen`; CommonUI owns activation, Back and focus
  restoration while the existing Elysium input-scope stack remains the sole input-mode writer.
  Native `UElysiumCommonUIInputData` supplies keyboard/gamepad Accept and Back defaults, so the
  source-authored foundation needs no Widget Blueprint or data-table asset. A source-policy
  automation test rejects new direct viewport insertion or competing `SetInputMode` writers.

  The title lockup and five small-caps items sit over a local 4K
  Elysium key-art plate in the empty `/Game/Elysium` boot world. Cold boot therefore raises the
  front end without loading or building a VtMB map; Quit to Main Menu travels back to the same
  empty shell. The plate lives below the gitignored export root because it incorporates decoded
  clan sigils. Design: `docs/architecture/ui-architecture.md`; the RE it is checked against: `docs/vtmb/vtmb-ui.md`
  (the two UI stacks, both schemes, the **1024×768** canvas law, the HUD class inventory, and four
  corrections to `docs/vtmb/m0_menu_build.md`). **PL8** [x]. The **Nocturne** type set (Spectral SC /
  Spectral / Inter, SIL OFL, no RFN) ships as generated local `UFontFace` assets
  (`fetch_ui_fonts.py` → `make_ui_fonts.py`). **CommonUI + CommonInput** adopted with widget trees
  in C++ Slate, so **no Widget Blueprint assets**. `UElysiumUISubsystem` remains the GI-scoped
  flow facade; screen composition belongs to the local-player root. Verbs: `elysium.menu [pause]`,
  `elysium.menu.close`, `elysium.BootMenu`.

  Three findings the build forced, all recorded in `docs/architecture/ui-architecture.md`: `make_ui_fonts.py`
  **cannot** run in the headless content commandlet, so the policy export coordinates a
  Slate-enabled editor pass; activatable screens must be pushed through a CommonUI container; and
  `ElysiumScreenshot::Request` grew a
  `bShowUI` flag because the harness's UI-free capture silently omits every Slate widget — the MCP
  tool now passes true, the regression harness keeps false so baselines hold.

  **Unified navigation slice landed and is automated.** Menu, dialogue, chargen, character/chargen
  sheets and signs use stable-id `UElysiumActionButton` targets under one
  `UElysiumNavigableScreen` focus owner. Lists wrap, dynamic response lists repair focus, mouse
  hover shares controller/keyboard selection, retained number shortcuts execute the same semantic
  actions, and transition latching prevents duplicate activation. The root automation now asserts
  the active leaf and underlying-modal restoration rather than registration alone.

  **Remaining:** live mouse/keyboard, Xbox-style and DualSense navigation acceptance across the
  screen set, including hot device switching, focus loss and reconnect; New Game click
  path untested end to end (the seam is wired, the console equivalent works); chargen ahead of New
  Game (9.4). **Open risk:** `uv run elysium debug shots` cannot see
  the UI layer, so 8.9's HUD needs UI-inclusive vantages or its regressions go unwatched.

  **Acceptance:** main menu and pause menu are legible and correctly proportioned at 1080p,
  1440p, 4K and 21:9 with no letterboxing or bitmap-font blur; New Game enters `sp_tutorial_1`
  through the 8.6a seam. *Deps:* PL8 [x]; 8.6a [x] for the seam.
- [x] **8.7 Ropes** — `move_rope`/`keyframe_rope` chains as Verlet `UCableComponent`s; rest length
  and node count reproduce VtMB's own arithmetic. **Open:** never put side by side with the running
  original, and `Subdiv` render tessellation has no analogue on `UCableComponent`. →
  `docs/vtmb/entity_visuals.md`.
- [~] **8.8 Sign / popup panels on the UI foundation** — 4.10's Canvas panel re-drawn on 8.6's
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

  **Landed:** the Canvas input/render path is retired. `UElysiumSignScreen` reconciles the current
  sign on the local-player game-modal layer, owns one visible CommonUI Continue action, consumes
  Back, removes gameplay contexts without changing time control, and routes dismissal through the
  presentation seam. `FElysiumEntityWorld` revalidates `CloseOnLeftClick` and `MinShowTime`; the
  legacy `+attack` request remains compatible with that same world rule. Automation covers the
  focus target, repeat suppression and both world gates. **Remaining:** the format/presentation
  features listed above and physical-device/resolution acceptance.
- [~] **8.9 HUD on the UI foundation** — retire the Canvas HUD as the player-facing surface:
  the +use reticle/use-icon (4.4), blood/health and status, the sign/screen-fade states, and a
  subtitle slot, composed on 8.6's stack with the same design tokens. Player pose/mode/FPS is
  dev-only and already lives in the Cog Maps window, separate from the game HUD. Use-icon art
  comes from the PL3 atlas, upscaled under the presentation test. It reads **`FElysiumViewState`**
  (**11.8**), not the substrate — the blood/health/frenzy/masquerade meters come off the player
  entity's sheet (**11.4**).

  **Landed:** the stable HUD model and local-player surface, reticle/use icons, health, discrete
  vitae droplets, Humanity/Masquerade, fade, cutscene suppression, bright-scene contrast veils and
  outlined glyph/icon treatments. Selector layouts have preview coverage but equipment,
  disciplines and inventory still need authoritative gameplay data and command wiring; signs now
  occupy the game-modal CommonUI layer, and subtitles remain open. *Deps:* 8.6, 4.4,
  4.10, 11.8.
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
  **Landed partial:** all current interactive surfaces have keyboard/gamepad focus navigation and
  programmatic action labels/captions; options, remapping, scaling, subtitle controls, colour and
  contrast settings remain open. Physical-device acceptance is not recorded yet.
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
  - **b. Locomotion** *(PP4)* — idle/walk/run/sneak/crouch/air/land driven by the mover's post-solve
    state. **Detailed status and design: `docs/project/three-cs-roadmap.md`**, which owns the
    resolver seam and the player animation graph the gait runs on. *Deps:* 8.11a; the assets it
    plays are `docs/project/animation-roadmap.md`'s.
  *Acceptance (a):* on `sp_tutorial_1`, `togglecamera` shows the PC's own clan model on the boom,
  dissolving in across `cam_fadeend`→`cam_fadestart` and culled at weight 0; the theatre's scenes
  animate it. *(b):* the gait matches the mover's reported state through a walk/run/crouch pass, and
  a `uv run elysium test Play` beat asserts it.

**Slice acceptance** *(M5 criterion)*: New Game starts from a real, modern menu that is legible
and correctly proportioned from 1080p to 4K and at 21:9; the HUD and the tutorial's popup signs
draw on the same stack; NPCs stand in the world at their entity origins.

## P9 — Dialogue & persistence *(design: `docs/vtmb/game_runtime.md`, `docs/project/rebuild-strategy.md` B7/B9)*

- [~] **9.1 `.dlg` parser + dlgexpr** — `ElysiumDlg.{h,cpp}` carries the 13-field parser, the
  `dlgexpr` front-normalizer, and the host-agnostic branch machine. NPC col-4 = action, PC col-4 =
  gate. The remaining fidelity gap is retail `CDialog::GetStartingLine`: scan starting-condition
  sentinels in physical file order, evaluate against live player/NPC/`G`, take the first passing
  valid link, then honor `usescript`/line-1/first-stored-line fallbacks. Acceptance includes Jack's
  overlapping and shadowed conditions plus line-action → `OnDialogEnd` ordering. →
  `docs/vtmb/game_runtime.md`.
- [ ] **9.2 Conversation UI + audio-by-path** — dialogue screen on the 8.6 UI foundation, line
  audio via 6.5/6.6's shared line service. Content is **reproduced verbatim** (lines, conditions, branch structure,
  ordering); presentation modernizes — vector type, reflowing line lists, speaker/emotion cues,
  the 8.10 subtitle path. *Deps:* 9.1, 6.5, 6.6, 8.6.
  **UI slice landed:** visible responses and Continue are stable CommonUI actions; number keys and
  Accept share the same choice callback, Back is consumed, response identity survives turn
  refresh, and contracted lists repair to the nearest response. Audio/presentation completion and
  live physical-device acceptance remain open.
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
  mirror, with the console→Python fallthrough. Elysium replaces only the imported personal `patchtype`
  alias with its Plus-profile selector after parsing. Binding them lets the **real `vamputil.py` import**.
  → `docs/vtmb/python_bridge.md`; the `Character`-shim divergence:.
- [x] **9.3c The script filesystem** — `FElysiumScriptFS` gives the VM its own filesystem namespace:
  reads union the `Saved/` overlay over the `$ELYSIUM_EXPORT_ROOT/` mirror, writes land in the overlay with copy-up,
  escaping the sandbox is the one denial. → `docs/vtmb/python_bridge.md` → "The script file layer".
- [x] **9.7 The script→engine action surface — survey, RE, spec** — **16,860 executable call sites /
  676 called names** surveyed, the `PyMethodDef` tables and datamaps recovered, and `docs/vtmb/script_api.md` written as
  the per-name inventory + demand-ranked build order. **d** landed `OneOfSet` for real (the 589
  dialogue gates now select) and guarded the `Whisper`/`FrenzyTrigger` receiver split. →
  `docs/vtmb/script_api.md`; roll model:.
- [~] **9.8 Inventory & items** — **the ownership core landed:** items as chain entities over the
  244-file catalogue, `FElysiumInventory` (slots, keyring, reserve ammo), the six inventory
  natives and entity-valued `Inventory_Remove`, item save state, and the shared-scope ground-model
  export/bake. **Remaining:** touch pickup and player drop, container take/give, `StartBarter`
  (needs the 8.6 barter UI), `trigger_inventory_check`, and the `TravelsWithPlayer()` absent-set
  half; buy/sell pricing remains 9.10. → `docs/vtmb/inventory.md`,
  `docs/architecture/gameplay-systems-architecture.md`. *Deps:* 9.7c, 9.4.
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
- [x] **9.6 Dice resolver** *(RE5 [x])* — `ElysiumDice::Roll` implements the recovered algorithm
  over the rulebook's `DiceRolls.txt` tables and the owned Dice RNG stream; `elysium.roll` is the
  headless driver. → `docs/recovered/dice-system.md`, `docs/architecture/gameplay-systems-architecture.md`.

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
- [~] **10.6 Input path — Enhanced Input, remapping, first-party gamepad** *(design:
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
    mappings and `bEnableLegacyInputScales`; gameplay contexts replace the gameplay portion of
    VtMB's `CClientMode*` split, while CommonUI owns menu, dialogue and prompt navigation.
    `bIgnoreAllPressedKeysUntilRelease` settles the held-input-into-conversation question on our
    side of the port. Every button-pair action binds
    **`ETriggerEvent::Canceled` alongside `Completed`** — a Hold or Tap trigger released early
    fires `Canceled`, and the missing `-cmd` leaves the button latched for the session. Also wires
    the analog path. The gamepad slice calls `SetAnalogMove`; mouse look is the separate
    `IA_MouseLook` displacement path in `IMC_Player_KBM`. `SetAnalogUp`, keyboard movement and the
    remaining keyboard/mouse binds remain in this sub-step.
  - **c. Reserved keys** — console on `` ` `` (VtMB's own `toggleconsole` key; frees F10 for
    `snapshot`) **plus `F7`** for layouts with no `` ` `` left of `1`, Cog's shell shortcuts to
    `Ctrl+F1`–`Ctrl+F4`, all other dev keys on
    `BindDebugKey`. Enforced by a **Substrate-tier test** over every generated IMC, not by
    convention; `elysium.input.ReserveDebugKeys 0` A/Bs it in dev builds.
  - **d. Mouse response** — `Mouse2D → IA_MouseLook` carries Enhanced Input's built-in `Smooth`
    sample-normalization modifier; legacy `bEnableMouseSmoothing` is off. The router reads
    `sensitivity`/`m_pitch`/`m_yaw` off `FElysiumConsole` for the per-count scale; the options slider
    writes the cvar. A future user-settings modifier may move that scale without changing the action.
  - **e. `GameInputWindows` + PS device configs + `IMC_Player_Gamepad`** — the plugin and standard
    DualSense (`054C:0CE6`) configuration are enabled for the first slice. Xbox needs no config;
    additional DS4/Edge `FGameInputDeviceConfiguration` entries remain open. The native Gamepad
    capability owns all shared controls; the configured generic Controller capability publishes
    only Create/PS/touchpad/Mute, avoiding duplicate stick, D-pad, standard-button and focus-clear
    events. The overridden hardware-device id supports glyph swapping; distinct Create/PS/Mute keys
    retain the otherwise-unrepresentable inputs.
    `GameInputRedist.msi` joins 10.5's
    packaging story. Adaptive triggers/haptics deferred. The **pad layout** — its allocation rule,
    the contextual `LT`, the quickbar radial, and its three marked divergences (crouch as a toggle,
    `toggleuiside` unbound, the `vhotkey` deferral not reproduced) — is
    `docs/architecture/input-architecture.md` § "The layout". Melee combo selection needs the stick
    quantised to four directions on a combat deadzone of its own. `LT`'s melee half and D-pad ↑ use
    the recovered **RE36** composite: held `+wpn_secondaryatk` asserts the dedicated block bit and
    forwards into ordinary `+attack2`; release clears both.
  - **f. `UElysiumInputUserSettings` + `config.cfg` projection** — the key profile is
    authoritative; `FElysiumConfigWriter` emits Valve-format text into `$ELYSIUM_EXPORT_ROOT/cfg/config.cfg` so
    `vamputil.py`'s `FixKeyBindings` reads a faithful view (imported once on first run). Slot
    `Third` is **excluded** — a gamepad row would produce a file VtMB could never write. The
    projection is not write-only: `FixKeyBindings` reads that file and then issues
    `bind <KEY> "vm_discipline"`, so a runtime `bind` must resolve to `MapPlayerKey` instead of
    being dropped by `FElysiumConsole` as it is today, or the patch's re-routing of discipline and
    feed silently dies whenever a player moves either off its default key. Declare `execonsole`,
    `player_immobilize` and `player_mobilize`. Rebinding works headlessly before any UI exists.
  - **g. Remapping screen** — lands with **8.10** on the 8.6 stack, not here.

  **Current partial slice:** the committed action table and generator emit `IA_Move`, `IA_Look`,
  `IA_MouseLook`, `IA_Jump`, `IA_Feed`, `IMC_Player_KBM`, `IMC_Player_Gamepad`, and their runtime action set.
  `UElysiumInputRouter` folds LS/RS into the existing `FElysiumUserCmd`, routes Mouse2D through the
  keyboard/mouse context's `Smooth` modifier, and routes Cross/A through the existing
  `+jump`/`-jump` bus, and routes Y/Triangle through the ordinary `+feed`/`-feed` command pair;
  `UElysiumInputSubsystem` owns both contexts across scopes and travel.
  UI navigation deliberately does not add Enhanced Input contexts: every UI-only scope removes the
  gameplay contexts and CommonUI handles focus, Accept and Back. `CursorPolicy::Auto` follows
  CommonInput device changes while leaving the active screen and its stable selection intact.
  `GameInputWindows` is the sole
  preferred Windows pad API: native Xbox plus the configured standard DualSense (`054C:0CE6`). Its
  native Gamepad processor owns standard controls, while its generic Controller processor is
  extra-buttons-only; axes and D-pad are not published twice. Keyboard and non-look mouse binds
  still use the legacy front end. The rest of a–g, the full pad layout, DS4/Edge, glyphs, haptics and the shipping
  redistributable remain open; physical Xbox acceptance also remains open.

  **Acceptance:** the tutorial is playable start to finish on keyboard+mouse and on an Xbox *and*
  a DualSense pad with no third-party driver; every action rebindable to primary/alternate/gamepad
  and surviving a restart; the reserved-key test green. Defaults are the **Patch 11.5** set. *Deps:* 9.3b (the console bus); **11.5** (the input scope stack the
  contexts are pushed through) and **11.6** (the command registry every action's string resolves
  against, and the `FElysiumUserCmd` the analog actions fill); 8.6/8.10 for the screen only.
- [P] **10.7 Long tail** *(post-tutorial; promote to tasks when reached — **promoted 2026-07-26:**
  stealth → **13.1**, disciplines → **13.2**, weapons/combat basics → **13.3**, chargen → **9.4**,
  choreography → **P12**; conversation camera → **11.13**)* — full combat AI (beyond 13.3's basics); NPC perception, reactions and
  the schedule graph beyond 8.5's native patrol/interesting-place locomotion (BT/StateTree where
  it adds value without replacing authored entity I/O). The state-1 idle branch and its task kernel
  are implemented; what remains of the recovered graph is the **door-obstruction reaction**, whose
  decision is written and tested but has no producer for either obstruction source. It needs three
  things, in dependency order: **actor→entity resolution** (a reverse lookup from a body back to its
  `FElysiumEntity`, which nothing provides today and which is reusable well beyond doors),
  `COND_HIT_BY_DOOR` from `FElysiumDoorBase::OnMoveBlocked` past its player-only pawn check plus a
  separate door-blocks-NPC-path producer for `m_hBlockedDoor`, and a **hint-node reader** over the
  authored `info_node_cover_corner`/`_med`/`_low` (342/81/36 across the exported maps) with the
  claim-release shape `FElysiumInterestingPlace` already uses. Unreachable on the tutorial path —
  nothing there blocks an NPC with a door. Two steps of the same order stay blocked on recovery
  rather than effort: the follower controller needs the unrecovered `follower_type` radii table, and
  return-to-initial has no producer. Facts:
  `docs/vtmb/npc-ai-reverse-engineering.md`; the enemy half is RE48. Two follow-ups from the same
  work: **`Elysium.Content.MapSnapshot` fails on a fixture gap, not a payload defect** — admission
  now schedules a think for every NPC, which is what makes a standing NPC run its stance machine,
  and the snapshot diffs a restored payload against a post-Load baseline that has not ticked, so
  the baseline's NPCs are unadmitted and carry no `nextthink` while the restored ones carry 0.100;
  both payloads are correct and the fix belongs in the fixture, as it did for the gaze fixture. And
  a **comment-phrasing pass**: `ElysiumStance.cpp`, `ElysiumSoundScheme.{h,cpp}`,
  `ElysiumSkeletalBuild.cpp` and `ElysiumMapActor.cpp` describe reimplementation-from-a-written-spec
  as "transcription", which understates the analysis → `docs/vtmb/` spec → independent
  implementation separation that the work actually follows. Also: ragdoll/IK/anim blends; `.emc`-style cache for `.ents` if
  parse time bites; lump-8 lighting bake as a low-end contingency (parked with the dynamic-path
  commitment); retail `.sav` import (needs RE7 wire format — currently a non-goal). For the
  low-end contingency, **Lumen Lite** (5.8's medium-quality irradiance-field GI, ~2× faster,
  runs on PC) is noted as a cheaper alternative to a lump-8 bake path — see Options.
  **vdata-driven gameplay systems** — data already on disk (PL5b, `$ELYSIUM_EXPORT_ROOT/vdata/`); each table's
  consumer + schema is mapped in `docs/vtmb/vdata-catalog.md`, and these are the systems that read
  them: **disciplines/vampire powers** (`disciplinetgt_*`, ~300 KB — the largest; → **13.2**),
  **stealth** (`stealth`/`stealthkillrules`; → **13.1**), the **hacking minigame** (`hackterminals/`),
  **economy/vendors** (`vendors`, item `worth`), **NPC disposition + reactions**
  (`dispositiontable`/`reaction*`), **data-driven conversation camera** (`camerashots/`; → **11.13f**),
  **radio + TV-news ambient content** (`radio_data`/`newscaster_*` → **6.8**), **impact FX**
  (`particleimpacttable`), **per-category entity sound schemes + AI-hearing volume**
  (`sndscheme_*`/`sound_volume_table` → **6.8**, distinct from PL5a's map SoundSchemes), and the **minor UI
  content tables** (`loadingtips`/`infobartypes`/`mapnames_localized`/`keynames`/
  `keynames`; `interestingplacetypelist` is consumed by 8.5). Promote any to its own task when reached.
- [x] **10.8 OpenLevel map lifecycle** — hard travel opens each generated
  `/ElysiumBaked/<map>/<map>` level; GI-scoped state survives while the old world and its
  per-map runtime state are reclaimed. → `docs/architecture/map-architecture.md`, `docs/architecture/uasset-bake-spike.md`.
- [ ] **10.9 Asset enhancement** — run the offline delight → super-resolve → style-anchored
  PBR pipeline as an `elysium.EnhancedTextures` A/B layer over the faithful world.
  *Acceptance:* toggle-off remains byte-for-byte on the faithful inputs; curated Tier 0/1
  outputs meet the `docs/architecture/rendering-perf.md` floor budget; no game-derived output is committed.
  *Deps:* PP6, 10.3, 7.4. *Design:* `docs/architecture/asset-enhancement.md`; governing test:
  `docs/project/remaster-direction.md`.

## P11 — Runtime spine *(design: `docs/architecture/runtime-architecture.md` + `docs/architecture/save-architecture.md` + `docs/architecture/camera-architecture.md` — read them; steps here are the tracker)*

The structure *between* the systems P1–P10 design: lifetimes, the frame, the player object, the
session, and the seams. It exists because the slice ladder now reaches "boot a New Game and play it",
and that is the one thing no current doc owns. Its rules are **S1–S11** (`docs/architecture/runtime-architecture.md`
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
- [ ] **11.13 Remaster camera director and modern player views** *(PP4)* — replace the original
  camera as the shipped player-feel target without disturbing its verified compatibility path.
  One `AElysiumPlayerCameraManager` resolves handle-based requests from the player rig, props,
  dialogue, map entities, embedded Python, VCD/map tracks, feed/death, and project-authored
  Sequencer scenes. Direct first-/third-person choices persist; the cycle contains only those two;
  third-person orbit, character facing and navigation are independent. The complete responsibility,
  API, asset, fallback, and migration design is `docs/architecture/camera-architecture.md`.
  - **11.13a–c Director foundation, authored camera library, player rig** — delegated to
    `docs/project/three-cs-roadmap.md` CCC2 `[x]`, which owns the camera service and its handle
    model, the authored profile library, the modern rig, the two persistent player modes, and the
    A/B against the faithful evaluator. The manager, the post-layer stack, the modern rig and
    `elysium.ModernCamera` have landed; the `UElysiumCameraProfile` asset and the user-settings
    surface deferred to 11.13d/8.10, where the screen that consumes them lives.
  - [ ] **11.13d Input, settings and presentation** — camera commands enter the action catalog;
    inspect/dialogue/cinematic scopes stay owned by `UElysiumInputSubsystem`; resolved reticle,
    HUD, body/viewmodel and letterbox state enters `FElysiumViewState`; accessibility covers
    separate FOV, recenter, shake/head-motion/recoil response and motion blur. *Deps:* CCC2,
    8.10 for the final options surface.
  - [ ] **11.13e Prop focus, map triggers and public API** — focusable target specs, soft-focus and
    inspect requests, collision/framing fallback, trigger component/volume, C++ value API, embedded
    Python 2.7 opaque handles, map-epoch teardown, and compatibility-safe `SetCamera`/`RemoveCamera`
    ownership. The camera never moves or rotates the player to frame an item. *Deps:* CCC2,
    11.13d; real inventory/interaction coverage joins 9.8 and B6.
  - [~] **11.13f Dialogue director** — reusable two-shot/single/over-shoulder/close-up grammar over
    speaker/listener anchors; collision, visibility, eye-line, screen-side and subtitle-safe tests;
    original `vdata/camerashots/` through the legacy adapter; player-view fallback when no safe shot
    exists. The scoped Dialogue request, source-shot-first selection, deterministic grammar,
    body-owner transaction, shared character/camera facing basis, save refusal, diagnostics and
    headless/content coverage have landed; controlled UP Plus capture and played resolution/input
    acceptance remain open. *Deps:* CCC2, 11.13d, 9.2; played first-conversation coverage joins 9.9.
  - [ ] **11.13g Sequencer bridge** — project-authored Level Sequences and Cine Cameras acquire one
    `Sequence` request; Camera Cut Track owns authored transforms/lenses/cuts/blends without a second
    interpolation; stop, abort, skip and travel release cleanly. Original VCD and Worldcraft timing
    stays in the legacy evaluator. *Deps:* CCC2.
  - [ ] **11.13h Integration acceptance** — migrate feed/death and every remaining direct producer;
    retain the theatre's 12.1 camera acceptance; add request/focus/dialogue/Python/Sequencer
    automation over the whole director. Every scoped camera returns to the exact chosen view and no
    camera path rotates or navigates the character. The player-view half of this acceptance — both
    modes, obstruction, and the played mouse+gamepad matrix — is CCC8's. *Deps:* CCC2, 11.13d–g,
    9.8, 9.9, 11.10.

**Slice acceptance:** from a cold launch — the menu comes up over the backdrop, New Game runs chargen
and enters the story, the tutorial's opening beats play on rebindable controls with a HUD, Esc pauses,
Save and Load round-trip the run, and `uv run elysium test Play` asserts the whole thing headlessly.

## P12 — The theatre: choreography & faces *(the PP2 rung — everything blocks; the skeletal animation the cast plays: `docs/project/animation-roadmap.md`)*

The intro cinematic (`sp_theatre` — embrace + trial) as VtMB plays it: `logic_choreographed_scene`
driving actors, scripted camera (11.7), line audio, subtitles, and facial animation. The fidelity
bar is an owner call: the scene is not done until the faces are alive — **eyes and lipsync
included**. **RE19** closes the scene format and event semantics
(`docs/vtmb/choreographed_scenes.md`); **PL9** supplies the corpus under `$ELYSIUM_EXPORT_ROOT/scenes/` and
`$ELYSIUM_EXPORT_ROOT/lip/`; **RE20** closes the flex chunks, `.lip` grammar, and
phoneme→controller tables, and **RE34** the eye system end to end
(`docs/vtmb/facial_animation.md`). **PL10** bakes the faces into the NPC
export — morph targets in each glb, the flex rig in `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, and
`$ELYSIUM_EXPORT_ROOT/expressions/`. **RE32 and RE33 remain open:** skeletal pose,
scene placement, secondary-motion/physics, and facial/lip runtime equivalence are
consumed here, while their capture-harness tasks, experiments, evidence gates, and current status live in
`docs/project/retail-capture-roadmap.md`. Confirmed behavior remains in
`docs/vtmb/animation_and_movers.md`, `docs/vtmb/choreographed_scenes.md`, and
`docs/vtmb/facial_animation.md`.

**RE33 verifies 12.3–12.5; it does not gate them.** RE20 closed the facial format end to end
and PL10 exported every input it names, so the face is built from that specification and
captured against retail only where the build diverges — the rule the retail tracker states as
"capture is the oracle, not the gate". None of the three carries RE33 as a dependency below.

**RE34 makes 12.4 a plain reproduction.** VtMB's eyes are a complete engine-side system and
all three of its layers are recovered: `StudioEyeball` records on all 301 character models
(two each, at `StudioModel`+192/+196), a renderer pass that aims the iris and writes the
eyelid flexdescs back, and a server-side gaze/fidget/blink behaviour whose every constant
comes from `vdata/System/DispositionTable.txt` — a file the export already carries. Nothing in
12.4 is an addition. Two pieces are inert in retail and are the only owner calls left there:
head turn drives bone controllers no model declares, and `LookAtEntityCenter` aims at the eye
rather than the centre. Full specification: `docs/vtmb/facial_animation.md`.

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
    and confirms all five cast members enter frame outside opaque fades. Both of VtMB's skeletal
    composition stages now run over the blended pose as skeletal controls — split inheritance
    (`split_bones` runtime application is **enabled**) then axis interpolation, from the rule table
    the model export writes. The generated inputs to the removed
    post-crossfade experiment already carried the discarded fixed-quaternion
    rewrite, so its quarter-turn discontinuities do not adjudicate the recovered
    retail rule. `sp_theatre` is exported and baked.

    **Multi-actor cinematic banks resolve per actor, and that path is verified against retail.**
    A cinematic bank is one skeleton carrying several complete actors, so binding by plain bone
    name would hand an actor another's chain — 491 source units and 119° wrong. The export splits
    each cinematic model into one bank per `BipNN` root and the runtime binds it through the scene
    actor's own `bonerename` pair, which joins to the retail capture at 180,812 agreeing and 0
    disagreeing contributions.
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
- [ ] **12.2b Scene mixahead calibration** *(carve-out of 12.2, blocks lipsync precision)* — the
  runtime applies VtMB's `snd_mixahead` default of **0.100 s** to every `speak` event, measured
  consistent to one frame across two independent scheduling anchors on `courtroom_scene_bip2`.
  Our own audible latency is **~21–61 ms** — 48 kHz, a 1024-frame callback with one buffer
  queued gives 21.3 ms, behind a WASAPI stream that requested a 1920-frame endpoint buffer on a
  480-frame device period. **So dialogue is heard 40–80 ms early against every authored cue** —
  lipsync, expressions, gestures and camera cuts alike. The constant is Source's mixer's lead
  inherited while running Unreal's. One term is still unmeasured: `ScheduledAudioClock` is
  stamped at submit, before the async mp3 decode, so a slow decode pushes the audible start
  later invisibly. *Acceptance:* the lead matches the measured path, with the residual stated.
  *Deps:* none — it is a constant and a measurement.
- [ ] **12.3 Facial flex track** — the morph targets are baked (PL10 [x]); what is left is the
  three layers above them, which are runtime evaluation: 44 flex controllers → 60 RPN flex rules
  → 65 flexdesc weights → the per-flex target ramp → the morph weight. All four inputs are in
  `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`, index-aligned with the glb's morph targets;
  `UElysiumBodyAnimInstance` grows a morph-track player over the body animation. Two load
  contracts the bake fixes: a morph that spans two materials arrives as one same-named piece per
  primitive, so the skeletal-mesh config must set `MorphTargetsDuplicateStrategy::Merge`, and a
  morph target is one *flex record*, not one flexdesc — the eyelid pairs hinge a single flexdesc
  into two ramps. Spec: `docs/vtmb/facial_animation.md`. *Acceptance:* a flex authored in the model
  moves the face in-game. *Deps:* 8.5, RE20 [x], PL10 [x] — all met.
  - **Built, pending an unobstructed visual.** `FElysiumFacialRig` reads the sidecar and evaluates
    all three layers; the anim instance emits morph-target curves over the body pose; the rig is
    cached per stem and answers null for a model with no face.
    `elysium.npc.flex`/`flex_reset`/`flex_dump` and a Cog Facial tab drive it. Headless proof runs
    on the real `nines` mesh through `USkeletalMeshComponent::MorphTargetWeights` — a blink drives
    4 of 53 targets, rest is zero everywhere, and 6 of 53 come back merged across primitives, which
    is the `Merge` contract failing loudly if it regresses. Live, 19 rigged bodies resolve
    controller → flexdesc → morph end to end. What is missing is a close-up: the shot was blocked
    by scene geometry on every angle, and the green-room harness cannot isolate a body because it
    is passed empty `-GreenRoomAnimSet=`/`-GreenRoomBoneRoot=`.
  - **Three spec corrections came out of the build**, all now in `docs/vtmb/facial_animation.md`:
    `FETCH2`'s operand is a **flexdesc** index into the array the rules are filling, not a
    controller index, so rules evaluate in file order; the `DIV` guard is required rather than
    defensive (`1 / right_open`, zero at rest); and the four eyelid rules connect to no eyelid
    morph in the shipped rig. `mstudioeyeball_t` is the bridge and it is authored on every
    character, so applying it — and the ordering it forces, rules before the eye pass — is
    12.4's work rather than this one's.
- [ ] **12.4 Eyes and eyelids** — a reproduction throughout, against the specification RE34
  closed (`docs/vtmb/facial_animation.md` → Eyes). Five pieces, each independently checkable:

  1. **Export the `StudioEyeball` records.** `StudioModel`+192/+196, 140 B, two per character;
     `StudioMesh`+24/+28 flags which meshes are eyes. Nothing downstream can start without it.
  2. **The lid bridge.** The eye pass writes flexdescs 0/4/8/12 from the record's
     `upper/lowerflexdesc` triples and `upper/lowertarget` offsets — which are linear, read
     through `asin(t / radius)`, not radians. The order is rules first, then the eye pass.
  3. **Blink.** A 0.3 s envelope, `w = 2·√(cos(π·u/2))` folded about 1 — closed in 48 ms,
     reopening over 252 ms — on a `RandomFloat(2.5, 6.0)` cadence.
  4. **Gaze.** The priority cascade, the ±30° cone off the head bone, the three-step keypad
     saccade grid, and the 0.1 s fixed-step integrator, with every rate and interval read from
     the exported `vdata/System/DispositionTable.txt` rather than tuned.
  5. **The iris.** The eye basis and the two UV planes, plus the `Eyes` shader's `$vampire`
     variant (12 shipped materials) whose iris ignores scene lighting.

  **Three owner calls, all made, all reproduce.** Head turn is integrated every think and
  applied through bone controllers that no shipped model declares, so it reaches nothing —
  visible head movement in VtMB dialogue is animation, not this path; the dead path is
  reproduced, filter and `> 360 → 0` guard included, and drives nothing.
  `LookAtEntityCenter` pushes the `Eye` constant, so all 10 authored firings aim at the eye
  rather than the centre — the defect is reproduced, and the input stays separately registered
  so the divergence is visible rather than implied. The glint is the one Presentation-layer
  divergence: no glint node, the highlight comes from UE specular off the flattened eye normal.
  All three are recorded beside the faithful behaviour in `docs/vtmb/facial_animation.md`.

  *Acceptance:* actors blink on their own cadence, their lids shape with the gaze, their eyes
  select and track targets through the theatre scene, and `Prince1.LookAtEntityEye` aims
  LaCroix at the player where `sp_theatre` fires it. *Deps:* 12.3.
  - **Built and green through the gaze layer.** The pipeline exports the records to
    `$ELYSIUM_EXPORT_ROOT/npc/eyes/<stem>.json`; `M_Eyes` is a generated master carrying both UV
    planes and the `$vampire` lerp; `FElysiumEyeRig` + `ElysiumEyes::BuildState` solve the basis
    per eye per frame from `AElysiumMapActor::PostMoveTick`; the lid write-back runs between the
    rule pass and the ramps, with `FElysiumFlexLid` kept as the per-flexdesc fallback for
    sidecars exported before the record existed. `FElysiumCombatCharacter` holds the gaze state
    at the recovered datamap offsets and runs the cascade, the ±30° cone, the keypad saccade and
    the fixed-0.1 s integrator; the four `LookAtEntity*` inputs are live. One world point per
    character per frame crosses `IElysiumEmbodiment::SetViewTarget`, which is the hop retail
    networks as `m_viewtarget`. Live on `sp_tutorial_1`: lids close, the blink fires on the
    disposition's own cadence at peak 0.96 and 7.25 % duty against 7.06 % predicted.
  - **What is left.** The debug surface: `elysium.npc.gaze`/`blink` verbs beside the existing
    `eyes_dump`, and a Cog **Eyes** tab reporting the resolved basis, both planes, blink phase
    and the lid flexdesc weights per body. Then the acceptance run itself, which is a theatre
    scene rather than a tutorial NPC.
  - **Three cascade arms have nothing to read and are marked in the code where they belong** —
    `enemy` needs the combat layer (P13), `navigation goal` needs a move-goal accessor on
    `FElysiumNpc`, and `heard sound` needs a sound record. Each falls through to the autonomous
    scan, which is what retail does when those arms find nothing, so the gap changes behaviour
    only where the missing system would have supplied a subject. The scan's own candidate filter
    tests a `CBaseEntity` field at `+0x94` that the decompilation does not name; the recovered
    `FL_CLIENT` half is exact and the rest is a stated divergence.
  - **A dependency defect blocked the whole thing and is fixed.** glTFRuntime built morph-target
    deltas with the index-buffer base used as a vertex-buffer offset, so on any multi-primitive
    mesh every primitive after the first addressed vertices that were not its own — on
    `smiling_jack`, indices 10299-11680 into a 4737-vertex buffer. No facial morph in the game
    deformed anything, while every weight, curve and delta magnitude measured correct. The
    vendored patch carries the fix and `Elysium.Content.FacialMorphTargets` now asserts that every
    delta lands inside the LOD vertex buffer and inside a section its morph target declares.
    Submitted upstream as `rdeioris/glTFRuntime#131`.
- [ ] **12.5 Lipsync** — `.lip` phoneme tracks (RE20 [x]; 9.3c already logs the scripts' `.lip`
  probes as a named divergence) driving mouth flexes against 12.2's line audio; the 7,136 files
  are on disk in `$ELYSIUM_EXPORT_ROOT/lip/` (PL9 [x]), keyed by the line's own sound path. A **three-file join
  per line**: the `.lip` for phoneme timing, `expressions/<model stem>_phonemes.txt` for the
  phoneme→controller weights (249 tables, chosen by the actor's model basename), and
  `mstudiomouth_t` for the amplitude-driven jaw that runs alongside. Key on the phoneme
  *string* — the `.lip` numeric code is not stable across the corpus. All three inputs are on
  disk: `$ELYSIUM_EXPORT_ROOT/lip/`, `$ELYSIUM_EXPORT_ROOT/expressions/` (the 249 `.txt` tables, PL10 [x]) and `mouths` in
  `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json`. Spec: `docs/vtmb/facial_animation.md`. *Acceptance:* mouths move
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
  (`ClearActiveDisciplines` and friends become real). Faithful behavior and remaining RE:
  `docs/vtmb/disciplines.md`. *Acceptance:* the tutorial's discipline
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
| PL12 | The current merged install mirrors `particles/*.txt` (**1,698**) + `particles/*.tga` (**318**) verbatim and patch-first into `$ELYSIUM_EXPORT_ROOT/particles/`, wired into export orchestration; the `sm_hub_1` closure is compiled strictly for 7.9. Per-map 2048² R16 top-down height maps use corrected exported geometry bounds; `sm_hub_1` is accepted, but grid-wide height-map generation/acceptance has not run, so PL12 remains open. Format: `docs/vtmb/weather.md` | 7.9 |
| PL13 [x] | All 56 player bodies are exported from the clan table; animation/facial implications live in `docs/vtmb/animation_and_movers.md` and `docs/vtmb/facial_animation.md`. | 8.11 |
| PL15 [x] | The Masquerade meter is a sub-rectangle of `cm_topbar`; no asset is missing. → `docs/vtmb/vtmb-ui.md`. | 8.9 |
| PL14 | **Export the first-person hand viewmodels.** `clandoc000.txt` also names `M_Hands`/`F_Hands` per clan — the patch-restored per-clan viewmodels under `models/hands/**` (21 in the merged install) — and PL13 deliberately left them out: they are the first-person half of the body and 8.11a's acceptance is the third-person boom. Same seed function, one more key pair; none carries a flex rig | 8.11a |
| PL16 [x] | Cinematic animation sets are exported and split into actor-addressable banks. → `docs/vtmb/choreographed_scenes.md`. | 12.1 |
| PL17 | Build the patch-first audio catalog + typed sidecars: codec/channel/rate/frame/duration metadata, complete static reference closure, parsed map + entity sound schemes, sentences/surfaces, item/discipline events, radio/news, case collisions and missing refs. Raw game audio remains gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`. → `docs/vtmb/audio_pipeline.md`, `docs/architecture/audio-architecture.md`. | 6.5–6.8, 9.2, 12.2 |
| PL18 [x] | The three structured animated-prop warnings are resolved and only the absent generic Night Watchman doppleganger model remains. **The premise was wrong: `bottleb`, `bottlec` and `stage_light` are not animated props at all.** Each declares exactly one **single-frame** `idle` while authoring `LoopSequence`, as do `lampfloor`, `glassa` and `junkyardcraneb` — six models of static dressing wearing an animation keyvalue. The seed now requires a sequence carrying more than one frame (`npc_export.has_animation`), which drops all six and leaves them on their decoded static `model_mesh`, so the theatre's stage lights are correctly still. Two real defects were found on the way and fixed: `mdl_skel._rle_channel` read `struct.unpack_from(f"<{valid}h", …)` without clamping to the remaining buffer (and indexed an empty tuple when `valid == 0`), and the actual decode failure was `read_skin` hardcoding a 44-byte stride over the 12- and 8-byte compact vertex formats those three models use, which `decode_skinned` handles for positions but not for skin. → `docs/vtmb/animation_and_movers.md`. | 8.5, 12.1 |
| PL19 [x] | Verified per-asset Unreal bake caching across all seven stages. Normal exports retain coarse content-addressed stage planning, then compare canonical semantic recipes for every desired texture, material, world/sky chunk, prop/skin asset, particle asset, and level; only dirty assets author/save and pruning is namespace-owned. Frozen-input, pending-inventory, commandlet, save/prune, and independent-verifier failures promote nothing; schema v1 establishes receipts through one conservative full rebuild and `--force` bypasses both cache layers. Acceptance on the current 2,100-asset `sp_tutorial_1` inventory: final no-op 10.246s with no Unreal launch; a one-pixel asphalt edit built 1/880 textures, changed one package, passed verification, and took 47.932s end to end (3.767s commandlet script, 12.220s verifier); a combined material/world/prop/particle/placement edit and its restore each changed exactly the five expected packages. → `docs/architecture/uasset-bake-spike.md`. | 0.9 |
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
| RE20 | MDL facial data, flex rules, and the `.lip` format are recovered; the eye system is RE34. → `docs/vtmb/facial_animation.md`. | 12.3–12.5 | [x] |
| RE21 | Player commands run before the think/event pass; the full frame order is recovered. → `docs/vtmb/game_runtime.md`. | 11.1, 11.11, 4.7 | [x] |
| RE22 | Player hull/view constants and the absence of ladder movement are recovered. → `docs/vtmb/source_movement.md`. | 4.7, 11.6 | [x] |
| RE23 | **The particle format + the wetness channel** — VtMB's weather is Troika-custom, not Source; the versioned `sm_hub_1` closure now resolves particle dependencies, rates, Unreal units, sprites, and collision relations, and its VMT triples prove that `GlobalWetness` drives the three `$envmaptint` channels. The authored timer caller and `env_particle` I/O surface are known. Original-retail evidence still must settle interpolation/ramp/audio time units, density, `attach_type=11`, `bounds`, brush-volume sampling, lifetime/keyframe units, and sprite blend semantics. The community FGD defines both particle classnames but marks their special fields untested. Full: `docs/vtmb/weather.md` | 7.9, PL12 | [ ] |
| RE24 | Sheet storage, feat/XP math, and health semantics are recovered. → `docs/vtmb/game_runtime.md` §3. | 9.4b–c | [x] |
| RE25 | Chargen pools, costs, sentinel behavior, and trait-effect source are recovered. → `docs/vtmb/game_runtime.md`. | 9.4f | [x] |
| RE26 | Trait-effect accumulation and dialogue sex gates are recovered. → `docs/vtmb/game_runtime.md`, `docs/vtmb/python_bridge.md`. | 9.4c–f, B4 | [x] |
| RE27 | Quest addressing, journal replacement, and award ordering are recovered. → `docs/vtmb/game_runtime.md`. | 9.4d | [x] |
| RE28 | Chargen close unpauses and teleports into the authored genesis exit. → `docs/vtmb/game_runtime.md`, `docs/vtmb/level_transitions.md`. | 9.4g | [x] |
| RE29 | Entity-name matching is case-insensitive with final-`*` prefix semantics. → `docs/vtmb/entity_io.md`. | entity I/O | [x] |
| RE30 | Recover `trigger_environmental_audio` touch behavior and the precedence/interpolation among its `room_type`, SoundScheme `RoomDSP`, and the player's networked `m_sndRoomDSP`/`m_sndPlayerDSP`. → `docs/vtmb/audio_pipeline.md`. | 6.7 | [ ] |
| RE31 | Recover the SoundScheme RandomSound frequency scheduler/distribution and transition edge cases; the current approximate curve is not a faithful baseline. → `docs/vtmb/audio_pipeline.md`. | 6.7 | [ ] |
| RE32 | Capture one source-attributed `sp_theatre` run from pre-map resource loads through actors/models/skeletons, every fired skeletal contribution, pose-build stages, and final render matrices in one queryable database. Join observed owner/sequence/animation identities to exact patch-first bytes and current export/decoder output, then trace only selected mismatches through decoding, blends/remaps, scene placement, root/entity motion, procedural work, hierarchy, and render handoff. Detailed status and experiments: `docs/project/retail-capture-roadmap.md`; facts: `docs/vtmb/animation_and_movers.md`, `docs/vtmb/mdl_v2531.md`, `docs/vtmb/choreographed_scenes.md`, and `docs/vtmb/vtmb-animation-reverse-engineering.md`. | 8.5, 8.11, 12.1 | [~] |
| RE33 | Trace expression, VCD/audio, and `.lip` resources from source bytes through runtime objects, controller mixing, flex rules/ramps, eyelids, amplitude mouth, vertex deformation, and render submission. **It verifies 12.3–12.5 rather than gating them** — RE20 closed the facial format and PL10 exported every input it names, so the face is built from that specification and captured only where the build diverges. Detailed status and experiments: `docs/project/retail-capture-roadmap.md`; facts: `docs/vtmb/facial_animation.md`. | 12.3–12.5 (as verification) | [~] |
| RE34 | **VtMB's eye system is recovered end to end** — the `StudioEyeball` record and its true `StudioModel`+192/+196 slot, the `StudioMesh` eye-mesh flags, the renderer's iris/glint math and its eyelid write-back, the `Eyes` shader family including the `$vampire` variant, the server's gaze/fidget/blink behaviour and its `vdata/System/DispositionTable.txt` tuning, the four `LookAtEntity*` inputs, and the networked hop between them. Head turn (applied through bone controllers no model declares) and `LookAtEntityCenter` (pushes the `Eye` constant) are inert or defective in retail and are recorded as such. → `docs/vtmb/facial_animation.md`, `docs/vtmb/mdl_v2531.md`, `docs/vtmb/animation_and_movers.md`. | 12.4 | [x] |
| RE35 | **The prop and trigger entity surface is recovered end to end.** `CDynamicProp`'s chain (`CBreakableProp → CBaseAnimating → CBaseToggle → CBaseEntity`, so every animating entity inherits the mover) with its complete 9 outputs / 25 inputs; `CDynamicProp::Activate` and the `SelectWeightedSequence(ACT_IDLE)` held-pose rest state; server-side `StudioFrameAdvance` versus the `m_bClientSideAnimation`-gated client path; the real `CBaseTrigger` spawnflag table including the absence of an allow-all fallback and the per-leaf reinterpretations of `0x2`/`0x10`/`0x20`/`0x80`; `filtername` resolution and the filter classes; `CTriggerHurt`'s datamap and its `×0.5`/`×3.0` cadence; `CPropSwitch`, `CBaseLockableEnt`/`CBaseVampireSkillEntity`, `CItemContainer`, and the static datamap surface of `CBaseTerminal`/`CPropHacking`; the `EF_NOSHADOW`/`EF_NODRAW`/`EF_NORECEIVESHADOW` enum shift; `solid` → `VPhysicsInitStatic`. Proven-dead FGD keys: `demo_sequence`, `climbable`, `locksnd`, `npc_opaque`, `diceroll`, `actsnd`/`deactsnd`. The skin crossfade renders but is unreachable, so snapping is faithful. Extends RE1. Open: no consumer of `EF_NORECEIVESHADOW` found in `client.dll` (`engine.dll` unchecked); `rendermode`/`renderfx` per-value semantics undecoded. Terminal lifecycle, command execution, email and numbered-output production are separated into RE39. → `docs/vtmb/entity_io.md`, `docs/vtmb/entity_visuals.md`, `docs/vtmb/phy_vphysics.md`, `docs/vtmb/animation_and_movers.md` B.0. | 4.11, 8.4a | [x] |
| RE36 | The **melee block verb and `+wpn_secondaryatk` semantics are recovered**. `+attack2` owns the ordinary attack2 button only. `+wpn_secondaryatk` is a held composite: its client handler asserts a dedicated button packed as `0x08000000`, then forwards into `+attack2`; release clears both. The server block predicate is the only direct `player+0x2088` test of that dedicated bit and additionally requires ground contact plus an active weapon capability in `0x18000`, then returns player compact code `13` and `ACT_PREBLOCK`. Remaining RE36 scope is the control/UI one-frame `vhotkey` deferral; the compiled Discipline index table and server dispatch moved to RE41. → `docs/vtmb/controls.md`, `research/cases/animation-pose/specs/input_actions.json`. | 10.6, 13.3 | [~] |
| RE37 | **The player/NPC gameplay-action selection chain is recovered for the pinned binary and current 22-map corpus**, from realized state or AI task through base activity, player/NPC/form/weapon translation, weighted or exact-label sequence resolution, pose parameters, autolayers/combat layers, transitions and interruption. The closed evidence surface includes the 4,460-entry activity registry; 17 player compact codes (13 reachable and four dormant), all 15 genuine player `+0x704` producers and both discipline `Player_Anim` rows; all nine paired modes and their producer/continuation policies; 77 NPC descendants, 691 schedules, 4,139 task invocations, 29 StartTask and 24 RunTask bodies, and 49 custom handlers; 169 weapon subclasses with 9,214 ordered translation rows; 1,872 sequence events and their complete server/client dispatch surfaces; 685 model autolayer bindings plus combat-layer and transition order; and every authored producer in the 22 exported maps and their Python/dialogue surface. The two current-corpus content misses remain explicit data facts (`item_w_sw_m64` and `npc_BaseVampAI`), not unresolved resolver rules. One-shot completion is exact: unchanged activities reuse their sequence until `StudioFrameAdvance` marks it finished, after which the next request reselects and restarts it; sustained unarmed crouch therefore repeats sequence 8. Working specification: `research/cases/animation-pose/specs/gameplay_actions.json`; facts: `docs/vtmb/animation_and_movers.md` A.3; remake contract and detailed work: `docs/architecture/animation-architecture.md` §3, `docs/project/animation-roadmap.md` ANM4 for catalog export, and `docs/project/three-cs-roadmap.md` CCC4 for the resolver that reads it. | 8.5, 8.11b, 13.3 | [x] |
| RE38 | **The inventory ownership and transfer model is recovered end to end.** Ordinary carried entries are full item entities in a 224-handle combat-character inventory; collected keys are logical records in one carried keyring entity; item data controls stack/drop/permanence/ammo policy; pickup, grant, destructive removal and drop have distinct lifetime effects; firearm `AmmoCount` returns loaded magazine while `GiveAmmo` adds reserve; containers reuse the combat-character inventory and server barter commands authoritatively take/give/buy/sell; `trigger_inventory_check` evaluates ordinary slots plus keyring on accepted player entry. Decoded tutorial saves corroborate entity ownership/slot state, and the complete patch-first `sp_tutorial_1` lockpick/key/safe/tire-iron/`.38` graph is joined. Price calculation remains 9.10, UI presentation remains 8.6, and runtime implementation remains 9.8. → `docs/vtmb/inventory.md`, `docs/vtmb/sp_tutorial_1-event-surface.md`. | 9.8, 9.10 | [x] |
| RE39 | Recover the **computer-terminal interaction end to end**. The static datamaps, content grammar, save fields, sound vocabulary, current-map demand and `sp_tutorial_1` `tuthack` transaction are consolidated. The server session vtables now join enabled/current-user/screen-facing eligibility to entry, active input and exit; the client is a model-bound 36×24 character texture with local editing and `hackcmd` transport; input flag bits are decoded; and Function execution is confirmed as dependency → runtext → enqueue `OnTriggerN` → synchronously run `runscript` → prompt. Ordinary output target delivery follows the script in the queue pass and retains the active-user provenance. `CPropKeypad` is the separate `keypad_strings` consumer atop the shared terminal base. Still open: player-dispatch/icon selection, forced-cancel and player-mode details, complete built-in grammar, difficulty/skill attempts, surrounding sound order, screensaver state and email/local-global persistence. → `docs/vtmb/computer-terminals.md`, `research/cases/computer-terminals/`. | hacking minigame, 4.11, 6.8 | [~] |
| RE40 | Recover the **core mechanics chain from player/script verb through runtime check, combat damage and health commit**. The shared seams are established: `CalcFeat` is a rating rather than a roll; dialogue, lockables, feeding, defense and soak own distinct check policies; item `Dmg` parses into the 17-word `CVDmg_t`; the common apply callback, mortal/Kindred soak selection, ranged lethality/defense/direct formula, blood shield, aggravated tracking and the retail unkillable cap are joined. The melee slice is joined from held primary/secondary weapon requests through ordinary/air/heavy activity, the base-Brawl/Melee-ranked automatic `2COMBO`, weighted sequence selection, opposed record, held/facing block, normal/heavy-block and attacker reactions, and final damage commit. The firearm slice is joined from primary/secondary intent through data-driven attack, zoom and mode-toggle behavior; press-edge or held `allow_autofire`; `Attack_Rate` scheduling; sequence-event shot commit; distinct `Ammo_Cost` and ray/pellet `Ammo_Fired`; view kick; dry fire; and bulk or `reload_single` reserve transactions. Feeding is joined statically from the `+feed`/`-feed` button transport through the first-press start and second-press release action, target/check policy, paired mode/activity selection, MDL bite/release events, accelerating server-timed blood/health pulses and idempotent victim teardown. Ranged capability cannot enter the melee block action, and no firearm-specific stagger band is established. The verb taxonomy keeps command, usercmd, gameplay action, effect and script/entity invocation distinct. Still open: wider player eligibility above the weapon controller, exact spread/crosshair math, `BurstMin`/`BurstMax` consumers, `SkillRequirement`, ranged multiplier decomposition, the complete generic firearm-flinch caller chain, post-soak filter consumption and special immunities, exceptional feed trait/outcome/output semantics, terminal skill attempts, and live feed/ranged/melee timing/formula captures. Discipline transactions are separated into RE41. The current Unreal damage entry is scalar-only and its unkillable one-HP floor diverges from retail's literal Health-damage cap of 75. → `docs/vtmb/skills-and-checks.md`, `docs/vtmb/combat-and-damage.md`, `docs/vtmb/gameplay-verbs.md`, `docs/vtmb/feeding.md`, `research/cases/core-mechanics/`. | 9.6–9.8, 13.3 | [~] |
| RE41 | The **Discipline authority, data interpreter and power catalog are recovered as a separate behavior surface**. The client maps visible learned powers onto thirteen compiled slots; `vdiscipline_int` and `vdiscipline_last` converge on one server authority; non-instant Animalism/Dementation/Dominate/Thaumaturgy use ordered `DisciplineTgt` target/filter/hit graphs with adjusted blood cost, one-time payment, projectiles, nested helper casts and explicit interruption; instant/passive powers use `Active_Disciplines`, trait actions and shared timed events; `vdiscipline_endall` and `ClearActiveDisciplines` share owned-event/effect teardown. Potence and Blood Shield join the recovered combat path, and Discipline `Player_Anim` joins the animation resolver. Still open: Blood Healing, Celerity, Obfuscate and Protean native consumers; Presence pulse/radius reconciliation; overt-zone-witness Masquerade policy; upper-tier client handoff; exact cooldown restore/order; and controlled retail casts. → `docs/vtmb/disciplines.md`, `research/cases/disciplines/`. | 13.2 | [~] |
| RE42 | Recover the **first-person viewmodel body**. The weapon camera class is closed and needs no further work: `camera_class` is authored per item, parsed by a case-sensitive `memcmp` ladder in the item-record vdata parser (`client.dll` `0x101a5394`, `vampire.dll` `0x1025aa65`) to `ranged` `0x02`, `thrown` `0x04`, `force_1st` `0x08`, `melee`/`force_3rd` `0x10`, everything else `0`; bit `0x01` is dead; and the bitmask is read off the **equipped item's** record via `0x1007b160` (`mov ax,[player+0x95e]` → `call 0x101a4770` → `mov esi,[rec+0x2440]`), not the player record. What remains open is the viewmodel itself: **(a)** how the per-clan hands model (42 bones rooted at `Camera01`, 154 sequences over 12 firearm families plus `v_lockpicks_*`) composes with the 17 packed per-weapon `v_` models — the asset layout reads as two complementary slots, arms from one and geometry from the other, matching `m_hViewModel[]` being an array beside `m_pViewWeapon`, but this is inference and is not traced; **(b)** whether the weapon is a bodygroup on the hands model or a separately attached model; **(c)** the `Camera01`-rooted rig's pose convention, which is a second skeleton and cannot inherit the character bake's "poses are baked native" result; **(d)** how `viewmodel_fov 54` composes with the camera's own FOV; **(e)** the sequence-event surface that drives the shot from a viewmodel clip rather than the world model's. → `docs/vtmb/camera-view-modes.md`, `docs/vtmb/animation_and_movers.md`. | 8.11a, PL14, `docs/project/three-cs-roadmap.md` CCC10.1 | [ ] |
| RE43 | Recover the **`sp_tutorial_1` event-resolution transaction** from authored row through trigger/use/VCD producer, queue, Python and receiver. Closed statically: the six consumed output fields and inert tail; reverse repeated-row firing; equal-time FIFO and breadth-first recursive drain; named-I/O → field-5 Python → direct-handle service order; no retail gameplay budget and zero-delay starvation; `ScheduleTask` on the same queue; synchronous Python reflected inputs; trigger admission, wait/once/refire blockers; exact `point_teleport` guards and absence of clearance/ground/velocity correction; maker-to-child output cloning; `prop_sign` use lifecycle; tutorial's main/Basic porch distances, landing overlaps and fade/VCD action order. **Still open:** the old-contact-end/new-contact-begin callback order inside retail `engine.dll`, the no-output `trigger_autosave` save transaction, and controlled retail/Unreal acceptance. Runtime gaps are tracked at 1.4 and 4.10. → `docs/vtmb/entity_io.md`, `docs/vtmb/game_runtime.md`, `docs/vtmb/python_bridge.md`, `docs/vtmb/choreographed_scenes.md`, `docs/vtmb/sp_tutorial_1-event-surface.md`, `research/cases/tutorial-event-resolution/`. | 1.4, 4.5, 4.10, 12.1 | [~] |
| RE44 | Expand RE43 across **every map currently exported** and diff its entity, Python, special-target and referenced-VCD surfaces against `sp_tutorial_1`. Closed statically for the 23-map manifest: 96 new entity classes, 127 authored producer pairs, 207 resolved receiver pairs, 113 static-unresolved target/input pairs and 209 Python identifiers are inventoried; runtime-late dynamic targets are separated from proven dangling wires; all seven novel trigger leaves are recovered (`trigger_teleport`, `trigger_push`, `trigger_player_activity_level`, `trigger_discipline_context`, `trigger_checkvolume`, `trigger_bomb_site`, `trigger_electric_bugaloo`); authored `trigger_player_activity_level.OnTrigger` is proven invalid/inert; `prop_sign.OnReadBegin` is corrected and recovered; known changelevel/maker/camera/mover/damage producers are joined to their owning facts; and VCD `expression`/`gesture` add actor-local work but no new entity-I/O or Python producer. **Still open:** producer-time lifetime classification for the residual static-unresolved rows; leaf-by-leaf producer RE for newly exposed exact NPC perception/lifecycle guards, landmark/interesting-place transitions, visibility, security-camera, HUD timer, slash and pickup families; expansion from the current 23 exports to all patch-first maps; the `engine.dll` collision/partition boundaries; and controlled retail/Unreal acceptance. → `docs/vtmb/exported-map-event-surface.md`, `docs/vtmb/entity_io.md`, `docs/vtmb/python_bridge.md`, `docs/vtmb/choreographed_scenes.md`, `research/cases/exported-event-resolution/`. | 1.4, 4.5, 4.10, 12.1 | [~] |
| RE45 | The **server-side trigger touch dispatch and the script recursion bound are recovered**. `PhysicsMarkEntityAsTouched` owns the begin decision; `PhysicsStartTouch` is gated only by `EFL_KILLME`; `CBaseTrigger::StartTouch`/`EndTouch` carry no wait, removal or disabled gate; the `wait == -1` branch's `SetTouch(NULL)` gates only `MultiTouch`, so `OnStartTouch`/`OnEndTouch` edges keep firing in the 0.1 s removal window; and a destructing trigger frees its own links without firing its own `OnEndTouch`. The synchronous reflected-input path has no server-side depth guard — the only bound is embedded CPython 2.1.2's recursion limit (1000), which raises, prints and resolves false at the innermost level. The runtime reproduces the window rule and the destruction asymmetry (`Elysium.Substrate.WaitMinusOne`, `DyingTriggerEndTouch`). **Still open:** whether retail fires the leaf's `OnEndTouch` on a `Disable`-driven link removal under a standing occupant (engine-side link teardown; the current leaf swallows it), and the reachable C-stack depth before native overflow. → `docs/vtmb/entity_io.md`, `docs/vtmb/python_bridge.md`, `research/cases/tutorial-event-resolution/`. | 1.4, 4.5, 12.1 | [~] |
| RE46 | Recover the **dialogue opener and camera boundary**. Closed statically: the ordinary, remote and unforced inputs are distinct handlers; the authored tutorial `Remote 256` handler does not read `256`; none of the opener handlers writes player/NPC transform, velocity, camera pose or input mode; the patch-first `default_camera` demand and normalization forms are inventoried; and Jack selects the authored `Jack` shot with its FOV/tracking/`DialogPOV` fields. **Still open:** client camera weights/lifetime, controller-versus-real-player visibility, exact view restoration, and any engine-side placement/facing behavior require the hash-gated capture. → `docs/vtmb/game_runtime.md`, `docs/vtmb/camera-view-modes.md`, `docs/vtmb/sp_tutorial_1-event-surface.md`, `research/cases/dialogue-camera/`. | 11.13d, 11.13f | [~] |
| RE47 | Recover the **retail/UP/Plus `sp_tutorial_1` character bootstrap**. Closed statically: original and UP BSP/script pairs are separately pinned; direct characters and disabled maker templates are classified; UP's seven added Society actors, extra maker and ten shared-character changes are joined; Jack's authored relocation/yaw/equipment delta is exact; native `LevelInit` construct/keyvalue/Spawn, parent deferral, `ServerActivate`, concrete `npc_VVampire` Spawn/Activate and concrete `NPCInitThink` order are recovered; and Plus selection is proven to mutate already-live wildcard cohorts while `IsIdling()` remains player-only. Jack's traced bootstrap contains no face-player write or direct activity-selection call. **Still open:** exact first-render sequence, first AI schedule/motor turn, initial engine ground/contact reconciliation and any pre-trigger gaze change require controlled capture. → `docs/vtmb/npc-ai-reverse-engineering.md`, `docs/vtmb/python_bridge.md`, `docs/vtmb/sp_tutorial_1-event-surface.md`, `research/cases/tutorial-npc-bootstrap/`. | 8.11b, 9.2, 11.13f | [~] |
| RE48 | Recover **what an NPC's enemy is** — the selection chain behind the current-enemy handle at `+0x5ce0`. The handle itself, the senses component at `+0x5cdc` and the shape of the pass are recorded (senses detect through sight/sound/damage → relationship rules classify hate `D_HT` / fear `D_FR` / neutral / friendly → eligibility and priority pick a candidate → the choice is stored → `GetEnemy` resolves the handle), and enemy memory is known to be separate state from line of sight, so an assignment survives losing sight. **Still open:** the relationship table and its authored source, the senses component's detection rules and ranges, the eligibility/priority comparison, and the script/input assignment surface. Consequence while this is open: nothing assigns an enemy, so `GetEnemy` is unconditionally null — which makes the `_NE` ("no enemy") variants of the door-obstruction schedules the faithful branch rather than a fallback, and leaves `COND_ENEMY_UNREACHABLE` (`0x59`) never set. → `docs/vtmb/npc-ai-reverse-engineering.md`. | 10.7, 13.3, door obstruction | [ ] |
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
