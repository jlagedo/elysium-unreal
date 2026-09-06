# Elysium-Unreal — Master Roadmap

**This document is the sole status and sequencing surface.** Every task is one line here: a
status mark, its ID and name, a half-line gloss, and — while it is open — a link to its
specification in `docs/project/plans/`. Design detail lives in `docs/architecture/`, VtMB facts
in `docs/vtmb/`, and the as-built record in git history. There is no other tracker, no as-built
archive and no decision log.

## How to use this doc

- Status marks: `[ ]` open · `[~]` in progress/partial · `[x]` done (verified) · `[P]` parked
  (deliberately deferred — revisit trigger stated in its plan entry).
- **One fact, one surface.** This file carries status and order and nothing else — no
  acceptance criteria, no dependency prose, no landed narrative, no VtMB facts. A plan file
  carries open-task specifications and **no status marks**. A design/vtmb doc carries durable
  facts and no status.
- **Landing a task is a deletion:** delete its entry from the plan file, write the durable
  facts into the owning design/`docs/vtmb/` doc, flip the row here and cut its gloss to the
  owning-doc pointer. A done task is one line in this file and nothing anywhere else.
- Old plan IDs (M*, L*, X*, retired CAP and P6 numbering) are resolved by git history
  (`git log -S "<id>"`), not by a mapping table.

## North star

Rebuild VtMB as a playable game — modernized — on UE 5.8 + C++ from this repo's own exported
intermediates; bring-your-own-game holds (`docs/project/rebuild-strategy.md`). The world's look
is baked offline into the gitignored `/ElysiumBaked` mount; collision, entities, scripting,
audio and NPCs stay runtime-built. Everything is proven on `sp_tutorial_1` (1,226 entities, 75
classnames), then scaled across ~100 maps. Direction — the presentation/feel/logic layers, the
three adjudication tests, default-to-reproduce: `docs/project/reconstruction-direction.md`.

## The playable path (PP0–PP6) — the master sequence

**Owner call.** One path to a real, played game drives all sequencing: **menu boot → New Game
(genesis chargen) → the theatre cinematic → land on the tutorial with Jack → complete the
tutorial.** Everything on the path lands first. Three standing rules:

1. **Logic and interactions first.** Gameplay systems outrank everything else.
2. **Graphics and performance are frozen.** No look polish or perf tuning until the path lands;
   the only exceptions are rendering bugs that block gameplay.
3. **Acceptance is played, not injected.** A rung completes when its beats run from real input
   in the built game, beat-scripted in the Play tier (11.10); dev shortcuts are never
   acceptance evidence.

| Rung | Delivers | Tasks (in order) |
|---|---|---|
| **PP0 — the core refactor** | the runtime spine | 11.10 *(11.0–11.8 [x])* |
| **PP1 — New Game & genesis [x]** | chargen for real, `sp_genesisdevice_1` played | 9.4 [x] |
| **PP2 — the theatre cinematic** | the intro plays start to finish — eyes and lipsync block | AUD0, AUD1, AUD3, LIFE7 *(the choreo-scene rewire 12.1–12.5 all stage on)*, 12.1–12.5, 8.11a [x] |
| **PP3 — land the tutorial** | Jack's first conversation with sound and reactions | AUD2, 9.2, 9.9 |
| **PP4 — core mechanics** | faithful movement, modern camera, gait, feeding, items, dice, HUD foundation + observer presentation seam | CCC0–CCC9, LIFE0–LIFE3, B6 [x], 9.8, 9.6 [x], 8.9 |
| **PP5 — persistence [x]** | save / quick / autosave + load mid-run | 11.9 [x] |
| **PP6 — complete the tutorial** | stealth authority, disciplines, firearms, combat AI, hacking — every retail beat | 13.1–13.5 → `uv run elysium test Play` |

**PP4's feel stack runs ahead of PP2/PP3 — owner call**, recovered rather than preferential:
the animation is the movement's speed authority, so the mover cannot close behind it (the CCC
ladder, [plans/three-cs.md](plans/three-cs.md), and the LIFE programme,
[plans/animation.md](plans/animation.md)). **After PP6 (the thaw):** 9.10, 8.8, 8.10, the
P3/P7 look lanes, 10.1–10.5 and asset enhancement — re-sequenced then.

**Map priority — owner call:** `sp_theatre` → `sp_tutorial_1` → `sm_pawnshop_1` → `sm_hub_1`.
Focused export, bake, and acceptance work targets maps in this order; a broader scope needs its
own owner call.

## Now — the unblocked front

**Owner call: played acceptance is the current priority.** The entity-gameplay ladder — typed
damage, weapons, the sound-event bus, senses, conditions, the enemy transaction, combat
schedules, `aiscripted_schedule`, disciplines, stealth, the player law channels and NPC
witnessing — carries headless Substrate/Content coverage and no played evidence. Playable-path
rule 3 governs what happens next:

1. **LIFE10 — the animation overlay subsystem is missing** — an armed body composes fewer
   channels than retail does, measured against a live retail session: 1,113 of 1,718 captured
   frames are short at least one channel and none arms a channel retail did not. The autolayer
   closure from a gait reaches aim and bobble only, so every `_attack_layer`, `_attack_delta` and
   `_reload_layer` is armed by `CBaseAnimatingOverlay` — Source's game-pushed layer stack, which
   this runtime has no counterpart for. A drawn gun never plays its attack or reload pose over
   the gait. The contract is recovered and the player arm is one slot fed by a current/next
   activity queue → [plans/animation.md](plans/animation.md).
2. **11.10** — the Play test tier, the beat-script harness the whole landed stack is accepted
   through; PP0's finish → [plans/spine.md](plans/spine.md).
3. **The 3 C's slice** — the controls/camera co-tune, then CCC8's owner-played acceptance →
   [plans/three-cs.md](plans/three-cs.md).
4. **LIFE7's choreo-scene rewire, then the 12.1 remainder** — the staging, camera coupling and
   triggers come back first ([plans/animation.md](plans/animation.md)), because the theatre's
   residual RE32 material/remap work and its final live acceptance cannot be run until a scene
   stages its cast again → [plans/theatre.md](plans/theatre.md).
5. **13.1–13.5 and 9.8 / 9.9 remainders** — the played tutorial lessons plus stealth-kill, the
   per-rank discipline consumers, frenzy, terminals, barter and the dialogue reaction consumer →
   [plans/gameplay.md](plans/gameplay.md).
6. **LIFE3 → LIFE4** — one resolver for the whole cast over the committed action tables, then a
   drawn weapon visible in the hand through a swing → [plans/animation.md](plans/animation.md).

**The audio programme is unstarted and sits under two rungs**: PP2 cannot finish without AUD3's
scene lines and PP3 cannot finish without AUD2's event surface, and both stand on AUD0's
catalog. Its place in the six items above is an open owner call →
[plans/audio.md](plans/audio.md).

## P0 — Ground truth & de-risk

- [x] **0.1 Profiling baseline** — `PCD3D_SM6` confirmed → `docs/architecture/rendering-perf.md`.
- [x] **0.2 MegaLights engagement check** → `docs/architecture/rendering-perf.md`.
- [x] **0.3 Second map exported** — `sm_hub_1` + `sm_pawnshop_1`.
- [x] **0.4 Sidecar space audit** — all consumed sidecars already Unreal cm.
- [x] **0.5 Cog 5.8 compile spike** — vendored under `Plugins/External/Cog/`.
- [x] **0.6 `ent_survey` count reconciliation** — 16,125 outputs / 1,591 Python pinned.
- [x] **0.7 Repo hygiene** — Ghidra/reference-source under `$ELYSIUM_WORK_ROOT`, untracked.
- [x] **0.8 `docs/vtmb/entity_io.md` re-based on the patch map set** — 108 maps / 71,096 entities.
- [x] **0.9 The uasset-bake architecture** → `docs/architecture/uasset-bake-spike.md`.
- [x] **0.10 Retail animation RE instrument** — built and run to closure; the capture corpus is
  banked under `$ELYSIUM_WORK_ROOT/research` and returns as an escalation oracle on a named
  divergence → `docs/vtmb/vtmb-animation-reverse-engineering.md`. The CAP programme is retired;
  its landed rows live under the LIFE programme.

## P1 — Entity substrate *(design: `docs/architecture/engine-core.md`)*

- [x] **1.1 Currency types + persistent state** — variant, handle, clock, `G` store.
- [x] **1.2 `.ents` defs parser** — immutable defs; `elysium.ents` round-trip.
- [x] **1.3 Class registry + base entity** — unregistered classnames stay inert records.
- [x] **1.4 Entity world + event queue + chokepoints** — the delivery contract asserted; the
  10,000-event cap is the recorded divergence.
- [x] **1.5 Brush bodies** — physical trigger state, deterministic containment diff.
- [x] **1.6 Starter classes** — `logic_auto`/`logic_relay`/triggers on the `CBaseTrigger` chain.
- [x] **1.7 Labels & debug strings.**

## P2 — Debug layer *(design: `docs/architecture/debug-tooling.md`)*

- [x] **2.1 Cog integration** · **2.2 Entity windows** · **2.3 `ent_*` verbs** ·
  **2.4 World visualization** · **2.5 Maps/Lights + `elysium.reload` + cheat manager**.
- [x] **2.7 Agent-facing MCP surface** — 20 `elysium_*` tools.
- [x] **2.8 Automation tests** — `Substrate` (`-nullrhi`) + `Content` (self-skipping) tiers.
- [x] **2.9 Screenshot-regression harness** — `-ElysiumShots` + `shots_diff.py`.

Deferred, tracked: console autocomplete, Gameplay Debugger category, Remote Control, NetImgui;
a refused green-room `gr_stand` tears the standing body off the stage and `gr_status` still names
it, so the readout claims a body the stage does not have.

## P3 — Lighting lane — **FROZEN** *(playable-path rule 2; detail: [plans/world.md](plans/world.md))*

- [ ] **[3.1 Pin MegaLights per-light](plans/world.md)** · **[3.2 Shadow curation](plans/world.md)** ·
  **[3.3 Attenuation-radius audit](plans/world.md)** · **[3.4 Texlight clustering](plans/world.md)**
- [x] **3.5 Sky IBL onto the SkyLight** → `docs/vtmb/sky-ambience.md`.
- [ ] **[3.6 Pinned exposure](plans/world.md)** · **[3.7 Grade/tonemapper fidelity](plans/world.md)** ·
  **[3.8 Texture prewarm](plans/world.md)** · **[3.9 Lightstyle clock pin](plans/world.md)** ·
  **[3.10 Volumetric fog calibration](plans/world.md)** ·
  **[3.12 `sm_hub_1` fill adjudication](plans/world.md)**
- [x] **3.13 Decal fog: accept or extend** — closed as **extend**: `UElysiumDecalSubsystem` owns
  one MID per decal from map load, so `ApplySceneFog` reaches every decal live, baked or laid
  (`docs/architecture/seam_map_material.md` → "Decal fog and wetness homes (R5.3)").
- [x] **3.11 Verify `elysium.LumenDiffuseBoost`** — retired, not wired: R4.5
  (`docs/project/seam_migration.md`) deleted the cvar A/B with the rest of the light-leak
  scaffolding; a bake-time value is R5.6's.

## P4 — Interaction *(design: `docs/architecture/engine-core.md`, `docs/vtmb/animation_and_movers.md` B)*

- [x] **4.1 Mover base** — swept kinematic movers + the door state machine.
- [x] **4.2 `func_button`** · **4.3 `func_door` / `func_door_rotating`** — brush meshes travel
  with their hulls.
- [x] **4.4 `+use` foundation + use-icon HUD** — the modern Feel divergence recorded in
  `docs/vtmb/entity_io.md`.
- [x] **4.5 Tutorial logic classes** — the logic/point/brush/trigger leaves.
- [x] **4.6 `trigger_changelevel` + landmark travel.**
- [x] **4.7 Source movement component** → `docs/architecture/movement-architecture.md`; the
  speed authority and sited courses closed under CCC.
- [ ] **[4.8 Rotating/linear/elevator family](plans/gameplay.md)** — elevator landed;
  rotating/linear movers and the mover-push observable open.
- [x] **4.9 Event-bus classes** — `events_player`/`events_world` full faithful surfaces.
- [~] **[4.10 `game_sign` / `prop_sign`](plans/gameplay.md)** — both sign leaves landed;
  `NewspaperData`, `ClientCommand`, fade/pause details open.
- [~] **[4.11 Trigger and `+use`-prop I/O gaps](plans/gameplay.md)** — filters,
  `prop_switch` interaction, lockables, doorknob sequences and the `trigger_hurt` cadence landed;
  the switch `soundgroup` and `reset_state` details remain open.
- [~] **[4.12 Door faithfulness gaps](plans/gameplay.md)** — the doorknob lock authority landed: a
  knob owns its own lock and the door consults its nearest one (`IsUseRefused`), instead of the door
  writing its state onto every knob, which had opened every key- and lockpick-gated door whose door
  entity carried no `LOCKED` spawnflag. The remaining retail divergences are recovered and recorded
  but not yet reproduced: the full `Use` guard-chain reorder and admission set, `MoveDone` arrival
  binding, input-level outputs/admission, `close`-at-arrival plus the `CloseWhenUnblocked` retry
  think, and the blocked family. The runtime visible-rotation observation is open and reproduced;
  the swing blocked-latch inversion stays held on a live capture (the mid-motion self-heal arguments
  are closed).

*Slice acceptance (met):* the tutorial elevator chain works; walking out loads `sm_pawnshop_1`.

## P5 — Scripting foundation *(design: `docs/vtmb/python_bridge.md`)*

- [x] **5.1 Scripts + dialogue mirrored** · **5.2 Expression evaluator** — error-to-false ·
  **5.3 Native bindings** — one `GNativeBindings` table · **5.4 Field-6 +
  `logic_pythoncheck` + `ScheduleTask` live** · **5.5 Embedded CPython 2.7**.

## The audio programme (AUD) — detail: [plans/audio.md](plans/audio.md)

Every authored sound heard — one catalog, one service, one request ledger, and VtMB's mix,
ambience and rooms on the Unreal Audio Mixer. Governing design:
`docs/architecture/audio-architecture.md`. One cumulative ladder in three tiers; absorbs the
retired P6 rows, PL17, 12.2/12.2b and the audio halves of 9.2, 9.8, 13.4 and 13.5 (landed rows
keep their historical IDs).

Landed foundation:

- [x] **6.1 MS-ADPCM decode** · **6.2 MP3 decode** · **6.3 `ambient_generic` + SoundSchemes** ·
  **6.4 Mover sounds** — the decoders, the scheme runtime and the mover `soundgroup` path.
- [x] **The request ledger** — the request/owner/handle model, map-epoch retirement, the
  gameplay-noise bus and the output-latency measurement →
  `docs/architecture/audio-architecture.md`.

**Tier 0 — it plays.** Every authored sound reaches the mixer through one path, and every I/O
call, script verb and scheme event that names a sound is conformed to it.

- [ ] **[AUD0 The catalog](plans/audio.md)** — the offline patch-first audio catalog and typed
  sidecars.
- [ ] **[AUD1 The service](plans/audio.md)** — one resolver, worker decode, streaming, handles,
  owner/epoch cancellation.
- [ ] **[AUD2 The event surface](plans/audio.md)** — typed domain events, NPC voices, footsteps,
  weapons, containers, terminals, radio/news and the AI-hearing producer.
- [ ] **[AUD3 The spoken line](plans/audio.md)** — one line service for dialogue and scenes,
  subtitles, and the calibrated mixahead lead.
- [ ] **[AUD4 Authored ambience plays](plans/audio.md)** — every `ambient_generic` flag, envelope
  and lifetime path; the scheme bed and its emitters.

**Tier 1 — it sounds like VtMB.** Levels, ambience behaviour, music state and room DSP.

- [ ] **[AUD5 The mix](plans/audio.md)** — classes, submixes, buses, user sliders, ducking,
  concurrency, occlusion.
- [ ] **[AUD6 Ambience, schemes and music](plans/audio.md)** — deterministic transitions, the
  random-emitter scheduler, the six `events_world` music states.
- [ ] **[AUD7 The listener zone](plans/audio.md)** — one resolver over
  `trigger_environmental_audio`, scheme `RoomDSP` and scripted overrides.
- [ ] **[AUD8 Heard — played acceptance](plans/audio.md)** — the programme's owner-played finish
  line.

**Tier 2 — beyond VtMB.** Enhancements the original never had.

- [P] **[AUD9 Enhancement](plans/audio.md)** — spatialization, zone reverb, loudness, detail
  layers, adaptive music, accessibility; behind the presentation freeze, each its own owner call
  at the thaw.

## P7 — Dressing & parity — **open tasks FROZEN** *(detail: [plans/world.md](plans/world.md))*

- [ ] **[7.1 Coronas](plans/world.md)**
- [x] **7.2 Decals** — deferred `UDecalComponent`s through the bake, on the V2 lane since R7.2:
  `M_V2_Decal` is the `MD_DeferredDecal` projector master, every projected unit stages a shared
  `MI_<unit>_Decal` twin the bake and `UElysiumDecalSubsystem::Lay` both bind by name, `$decal`
  world faces draw as mesh decals, and the legacy `M_Decal` and its per-map packages are retired.
- [x] **7.3 Water** [R7.1] — the V2 lane: `M_V2_Water` is a Single Layer Water master (SLW's own
  refraction, Lumen's reflection and absorption/scattering answer the two 2004 render-target
  passes and the in-volume fog in one place), `water.volumes[]` is a new map-stage product joining
  every real `LEAFWATERDATA` row to its `CONTENTS_WATER` brushes, one bake-placed
  `AElysiumWaterVolumes` actor per map classifies the player's feet/waist/eyes pre-move and is the
  map's underwater post-process volume, and the camera's `cl_waterdist` clearance offset is
  transcribed. `sm_hub_1`, `sm_pier_1` and `sp_soc_3` are converted. **Settled by the
  water-complete pass**: a 26-gap decoded-datum census of `sm_pier_1` and `sm_hub_1` was
  dispositioned in full — the underside became a per-face `_Underside` twin, the sewer's 29-frame
  DUDV animates on the Refraction pin, the master transcribes `Water_Old`'s three passes (black
  base, the cheap cube overlay as emissive), face lightstyles animate as a per-primitive brightness
  on CPD slot 6, and water raises its events (entry splash off the water-level transition,
  level-keyed footstep sounds, buoyancy from the authored `fluid` block). `%compilewater` selects
  the master and a `%compilenodraw` water face draws nothing, as in VtMB (the pier's ocean is the
  `blackwater` card). Design: `docs/architecture/water-architecture.md`; VtMB facts and evidence:
  `docs/vtmb/water.md`; migration log: `docs/project/seam_migration.md` → "R7.1". Player water
  *movement* (`WaterMove`, swim/tread, the camera water band) is out of that pass by owner call and
  stays on the substrate tier.
- [x] **7.4 Master-material set** — the generated surface masters.
- [ ] **[7.5 Real reflections](plans/world.md)** → `docs/vtmb/reflections.md` ·
  **[7.6 Bloom/glow tuning](plans/world.md)** · **[7.7 Shadow quality](plans/world.md)** ·
  **[7.8 A/B capture harness](plans/world.md)** · **[7.9 Weather & wetness](plans/world.md)**

*Slice acceptance:* side-by-side A/B match with the original's reference captures.

## P8 — Characters & UI *(detail: [plans/characters-ui.md](plans/characters-ui.md))*

- [x] **8.1 Entity-model export** · **8.2 Native skeletal adoption** — ESKM → baked assets ·
  **8.3 Dynamic props** · **8.4 Physics props** — Chaos over `.phy`.
- [~] **[8.4a `prop_dynamic` divergences](plans/characters-ui.md)** — animation half done;
  `solid` and `disableshadows` unread.
- [x] **8.4b Placed-model rest-pose closure** — every `.ents` and GAME_LUMP MDL resolves and
  installs an authored rest pose before visibility; proven-equivalent placements remain static →
  `docs/architecture/animation-architecture.md`.
- [x] **8.5 NPC presence + native locomotion + `scripted_sequence` minimal** — the animation
  half is the LIFE programme's.
- [x] **8.6a New Game context + story entry.**
- [~] **[8.6 UI foundation — design system + shell](plans/characters-ui.md)** — landed; device
  navigation acceptance and the New Game click path open →
  `docs/architecture/ui-architecture.md`.
- [x] **8.7 Ropes** — `UCableComponent` over the RE'd rest-length arithmetic.
- [~] **[8.8 Sign / popup panels](plans/characters-ui.md)** — screen landed; format features open.
- [~] **[8.9 HUD on the UI foundation](plans/characters-ui.md)** — vitals, standings, the
  equipment/inventory selectors and the stealth slot landed; the disciplines selector, the stealth
  producers and subtitles open.
- [ ] **[8.10 Accessibility & options backing](plans/characters-ui.md)**
- [x] **8.11a The player body** — clan/sex/slot resolution, dithered fade, choreography-ready.

## The character life programme (LIFE) — detail: [plans/animation.md](plans/animation.md)

Every character alive — the cast and the player through one resolver, weapons in hands that
swing them, bodies that react and die. Governing decision — bake native, let Unreal run it:
`docs/architecture/animation-architecture.md`. One cumulative ladder; absorbs the retired ANM
and CAP programmes and the CCC animation rungs (landed rows keep their historical IDs).

Landed foundation:

- [x] **CAP0–CAP4 The retail capture instrument** — exact-build hooks, three reproducible
  theatre captures, decode/index/inspect closed (byte-coverage missing **0**; the shipped
  decoder is clean); the corpus is banked under `$ELYSIUM_WORK_ROOT/research`.
- [x] **CAP5.3 / CAP5.7 / CAP5.8** — blend grids exported and baked; the shipped multi-biped
  path modelled (180,812 agreeing / 0 disagreeing); the include-remap route (6,927 → 2,452)
  → `docs/vtmb/animation_and_movers.md`.
- [x] **CAP7.1 / CAP7.2 The two composition rules in Unreal** — byte-exact rule table; both
  stages in retail's slot.
- [x] **ANM3 Bake the blend spaces** — parity-asserted `UBlendSpace` per declared grid.
- [x] **ANM4a Gameplay action selection extracted** *(= RE37)* →
  `docs/vtmb/animation_and_movers.md` A.3.
- [x] **CCC4 Intent, resolver, selection record** · **CCC5 The player animation graph** ·
  **CCC6 The green room drives it** · **CCC10 The weapon rung (third person)**.

The ladder:

- [x] **LIFE0 The composition seam holds, loudly** — the zero-length looping montage, the
  sequence-or-blend-space pair on every locomotion state, table-driven layer hosts with named
  misses, a grid label that stands as a blend space or names its miss, the aim-grid neutral
  adjudicated correct at its owner and guarded at rest, and the standing corpus verified current
  (166 models, 527 blend spaces, 0 orphans).
- [x] **LIFE1 The bake closes** — the cardinality invariant is proved
  offline before the bake (7,142 bank packages over 136 banks, exactly what the mount carries);
  the seven wield mounts keep their channels on every body and bank clip, verifier-asserted per
  family; the orphan census runs in the same preflight, routing every bank clip and grid over the
  include DAG (3,522 of 3,755 clips across 55 body banks reachable, the 233 rest all shadowed
  duplicates) and refusing a declaring host no body reaches or a reached grid that stands as no
  blend space; retail's between-key read adjudicated LINEAR against the banked corpus and closed
  as a fact, no divergence; the `A_dance01` seam adjudicated at
  a graph-less measurement rather than a bake defect (`e347ebb`),
  the clip's blamed bones carrying Joy's own bind translations and skin weights.
- [x] **LIFE2 The action catalog as project source** — the recovered weapon
  translation tables are committed source, generated from the pinned binary by owner-run
  archaeology: 9,214 ordered rows over 61 classes stored as 1,565 units (18 base sequences, 110
  block headers, 595 exceptions, 57 `required` flags, 3 renames, 8 substitute bases) and
  digest-matched back to the retail row stream, with the fallback order held by conformance against
  the corpus (1,739 of 4,580 requests resolving, 38% of them at rung 2 or later, no rewrite kind
  dead, both 0% families named). The player action rules are committed source beside them, the
  ordinary selector stored as ordered predicate rows: 17 compact codes with exactly four dormant, an
  8-row gait ladder ahead of the dispatch, 41 arm rows over 10 codes, the three pose writes, the two
  player-side translations and both effective `Player_Anim` fields, every activity carrying its
  registered ID; of the 35 activities that surface can request, 29 resolve in the corpus directly,
  five through the weapon tables, and one — `ACT_LAND_CROUCH` — nowhere, which is the absence
  retail's own capture recorded. The NPC translation surface is committed source beside them: 77
  `CAI_BaseNPC` descendants collapsing to 10 pre-translation, 5 class-translation and 2+2 delegate
  bodies plus the one paired-action tail, stored as 69 ordered rules over an 18-predicate vocabulary
  with the inherited body chained before or after each — every address, inheritor count and owning
  class re-decoded from the pinned RTTI walk at generation time; 63 entity classnames resolved
  most-derived; 100 task policies over 111 task routes with zero exact-label and zero overlay-layer
  routes; and the 232 paired-action variants generated from 29 registered bases and the `+1`…`+8`
  arithmetic rather than enumerated, four zombie-feeding families registering their attacker and
  victim halves the other way round. Two movement-policy rows name a request family the RE never
  enumerated and report themselves unresolved rather than guess it. Nothing about actions is
  exported or baked. Per-model event timelines ride in the `npc/blends/<stem>.json` sidecar
  beside the grids and autolayer binding — 48 owners, 631 sequences, 979 records, every cycle in
  range and every type 0 — and the autolayer census confirms the two `move_and_ranged` banks alone
  carry bindings (`{1:237, 2:224}`), with the one inverted host named. The transition graph is
  closed as unauthored: `NumTransitions`@336 is 0 on all 4,445 models and `entrynode`/`exitnode`@624/628
  are 0 on all 14,012 sequence descriptors, and `AdvanceToIdealActivity`'s traversal
  (`FindTransitionSequence` → the `entrynode`/`exitnode`/matrix lookup at `0x10428ad0`) is
  decompilation-confirmed dead code against shipped content — no sidecar block is emitted.
- [x] **LIFE3 One resolver for the whole cast** — the translation tables, the graph state, the base
  pose's ownership and the speed authority are landed. The resolver walks the committed weapon
  ladders, the two `CBasePlayer` rows and the recovered NPC class bodies in their witnessed orders,
  availability-probing each rung against the body's own vocabulary, so a glock-armed body reaches
  `pistol_relaxed_walk` at rung 2 and an idle armed cast member walks relaxed instead of weapon-up
  (a body in combat answers neither arm, the divergence recorded in
  `docs/vtmb/animation_and_movers.md`). The graph state rides on the selection record as
  `GraphState`, projected once in the resolver's step 6 and read by the anim instance, Cog, the
  `act_state` channel and the MCP surface, so a readout and a pose cannot disagree. The speed is one
  number: the gait tables resolve under the same source, classname and state the pose walks, the
  gait classifies off the graph state, and the NPC mover is commanded each pass with the cell the
  record publishes — the discrete key carries the actor classname, and a body reaches its character
  directly rather than through an owner `Possess` overwrites. Both producers emit one trace schema
  off the sample the driver ticked (`ElysiumLocomotionTrace` is the columns and the writer), and the
  evidence holds the claim: the intra-row no-slide predicate is evaluated in the run itself, the
  harness fails loudly — refused filters, missing bodies, unarmed orders and empty writes are
  non-zero exits plus a `run.failed` marker the comparator refuses — the coverage sweep drives 3,420
  cast requests keyed on authored classname, weapon and `ActorState` with
  `GraphState == StateForActivity(requested)` asserted and sequence-zero split from a fallback that
  named a clip, and the acceptance patrol is `sm_hub_1`'s own cop route walked armed and unarmed at
  the cells its tables author with the selection record in the log
  (`uv run elysium debug cast --sited`; worst intra-row slide under 1 u/s). The base pose has one
  owner at a time: every body with a mover publishes a locomotion selection each anim tick, LIFE4's
  arbitration slot decides ownership by priority, and the bind loads the asset the record names,
  warning once per `(stem, request, outcome)` when a request binds nothing.
- [~] **[LIFE4 Weapons in hands — third person](plans/animation.md)** — corpus, masters and
  `DA_WieldModels` baked; tracking through the prop bone landed: the wield bake re-skins a
  ref-pose-overridden model's geometry into the override frame, and `elysium.gr_wield_check`
  passes mapping, tracking and placement on animated bases through locomotion and a swing, melee
  and firearm, both sexes; the channel arbitration slot landed: the driver holds one request slot
  per channel, arbitrates the base by a typed priority order each tick, and every holder releases
  on every stop path, with the verdict on the selection record, the Cog row and the MCP surfaces;
  the equip funnels landed: `wieldmodel_m`/`wieldmodel_f`/`anim_prefix` parse onto
  `FElysiumItemDef` and one transaction path attaches on equip and detaches on holster for NPC
  and player alike, selecting on the wielder's `IsMale`; visibility landed: the pawn suppresses
  the drawn weapon's rendering with the body's own camera draw policy, never touching the
  attachment; weapon-state animation landed: the player's grounded stand/gait walks the committed
  retail ladder live with the combat-stance query, per-weapon idle/walk translation and the grip
  autolayers arm through the table-declared hosts, and the wield check gates the mount's local
  frame and the two-handed off hand; real-input draw works today through the command bus
  (`elysium.cmd invnext`/`slot*`/`holster` reach `SetActiveWeapon`); the owner-played acceptance
  sweep remains, and rides LIFE5's sweep now that a real attack input exists.
- [x] **Sequence-blend fidelity** — the owner call resolved restore-faithful; the mechanism, its
  one duration authority and the three named divergences of the reproduction →
  `docs/architecture/animation-architecture.md`.
- [~] **[LIFE5 Reactions and combat actions](plans/animation.md)** — the body-kind resolver fork,
  the one activity door (`PlayNpcActivity`/`PickActivityClip` retired), the `2COMBO` substitution
  and the reload chain landed, and a struck body flinches on the `hit_yaw` grid off its own
  save-persisted `Reaction` stream. The sequence-event carrier is complete across its three
  slices — the core window rule plus the process census; the weapon band route, where the combat
  character forwards 3000–3999, ranged 3030–3044 commits at the authored instant, the melee
  swallow set holds, thrown/discipline/armor take the no-route body and `ContactEventCycle`
  survives only as the degraded fallback outside melee; and the live phase arms, every biped
  publishing its Base-channel phase from per-producer armed records carrying `AnchorCycle`. The
  blocked/stagger family landed whole: the clip `reach_cm`/`blocked_reaction` columns, the
  authored reaction fade and the one `PlayReactionActivity` producer under it; `WasMeleeBlocked`,
  the defender's `ACT_BLOCK`/`ACT_BLOCK_HEAVY`, the attacker's authored blocked reaction with its
  `ACT_BLOCKED_REACTION_RIGHT` fallback, base-channel holds on both sides, the flinch yield, the
  player's `wpn_secondaryatk` block intent, and save schemas 26/27 with the snapshot leaf-schema
  threading. The remaining action families landed: the player attack producer turns a real
  `+attack`/`+attack2` press into a weapon transaction off the combat button field behind retail's
  `ItemPostFrame` refusal order, the primary a press edge melee included with a firearm mode's
  `allow_autofire` the one held exception; the melee contact walk replaced `ContactEventCycle` for melee entirely — authored
  per-clip swing windows tick-batched into 100 Hz sub-steps, swept bone segments through the
  embodiment seam, per-record hit-once groups and one opposed roll staged at swing start off
  retail's 60-unit/0.7 query, serial-scoped — while ranged commit stays on its authored 3030–3044
  events and 3047 is claimed and inert as retail's NPC swing trigger; the combo family selects its
  entry on the authored direction-keyed button masks (exact-first, no draw) on the player arm,
  which refuses rather than drawing when no candidate masks — so the `2COMBO` substitution is
  offered and then refused on the player exactly as retail's is — and walks the
  busy/chain path on the authored `w_open`/`w_close`/`w_hold` windows, successor at the same rate,
  deadline never re-pushed, `2COMBO` terminating, dangling targets authored and reported, NPCs
  never chaining; NPC retaliation feeds damage into a 5-second derived enemy memory that stated
  relationships supersede, so a struck neutral reaches Combat and swings back through the same
  weapon transaction; grounded knockback runs the verified retail gate — margin-band entry,
  alive ∧ ¬`Disallow_Knockbacks` eligibility, the asymmetric direction bands and the NPC-only yaw
  snap, zero RNG in the gate path — with the single `NORMAL_HIGH_{dir}` candidate and the omitted
  hit-buildup gate reported as named stand-ins; the NPC death family lands the `OnKilled`
  transaction (claims released, Mind Dead, frozen-not-hidden, collision off), the
  `TASK_PLAY_DEATH_SEQUENCE` ladder (arg → `ACT_DIESIMPLE` → `ACT_IDLE`), a handoff that holds the
  final pose until PHYS1 bakes a physics asset, and corpse state restored synchronously
  on load; the holdable reaction claim releases by condition
  (`ClipCompletion`/`Envelope`/`Predicate`), so the player's block pose loops for the whole held
  predicate and resumes after preemption with no re-draw and the flinch runs the recovered
  0.1 s-in/0.3 s-out envelope; and one `DefaultSlot` montage-slot mechanism serves
  `scripted_sequence` phases and interesting-place segments through `FElysiumClipSegment` bands,
  released on every stop path. The route-gated restart rule, `elysium.cmd.tap`, the
  Inertialization-node removal and the reaction-branch loop pins landed with them, and the clip
  sidecars carry the `swings` and `combo` columns the families read. Open: the owner-played
  acceptance sweep (which also closes LIFE4's), the flying knockback chain (RE-unblocked — launch
  formula and land terminator recovered), consuming the authored per-attack knockback table now in
  the sidecars, the **NPC melee sequence selector**
  (`ChooseMeleeAttackSequence`, the cast arm of the owner's slot-331 fork — geometric candidate
  scoring against the enemy, where the runtime draws by weight today), and the four named residuals
  the plan lists (the NPC-side reaction-claim release on a mid-hold body swap, the montage route's
  inert restart rule, a restored scripted beat's un-retaken segment claim, and
  `QuerySwingContacts`' live-only coverage). Paired actions moved to LIFE7 — owner call, made.
- [ ] **[LIFE6 The first-person viewmodel](plans/animation.md)** — the 21+17 corpus export and
  the two-component body; ranged only.
- [ ] **[LIFE7 The cinematic path and gestures](plans/animation.md)** — the choreo-scene rewire
  hard slice (marks, camera, triggers — the retail execution oracle closes initial cast timing and
  exposes the authored active-scene `Kill` cleanup the runtime lacks; Elysium's played path remains
  untested, **12.1's staging and, through it, 12.3–12.5's acceptance runs all wait on this**, and the
  plan carries the ranked gap map), then the montage migration, the gesture
  un-collapse against the recovered `0.1` layer weight, paired actions (role/size/side variant
  arithmetic and the two-body claim — owner call, made), the `Prince_Escort_Male` cluster (its
  over-band residue attributed to the characterization evaluator, not to the decode or the bake, so
  no pipeline row falls out of it).
- [ ] **[LIFE8 Alive — played acceptance](plans/animation.md)** — the programme's owner-played
  finish line; owns the capture-tooling trim.
- [P] **[LIFE9 Secondary-motion calibration](plans/animation.md)** — hair/cloth fitting and
  numeric replays; presentation polish behind the graphics freeze, revisit at the thaw.
- [~] **[LIFE10 The animation overlay subsystem](plans/animation.md)** — the four-slot substrate,
  per-slot envelope and lifecycle, player/cast producers, five graph closures, post-multiply
  additive and per-closure shared-bank remap stand. The running graph matches the reference
  compositor at `0.009 cm` median on both a differing-bind Tremere and the all-copy Malkavian
  control; `BakedCharacterParity`, `OracleIdentity`, `RigRetarget`, `RigPose`, `RigLayers` and
  `FanDuration` are green (225 gait fans across 84 owners, 207 scored to within 0.0010 s).
  Previous-sequence cross-fades, event look-ahead and played two-body acceptance remain open;
  `RigCompose` is red at 3.168 cm control / 1.358 cm layered, legs first — the cause is named
  (T-C7, the cross-fade chain, `plans/animation-critical-path.md`) and is not a bake gap.

## P9 — Dialogue & persistence *(detail: [plans/gameplay.md](plans/gameplay.md))*

- [~] **[9.1 `.dlg` parser + dlgexpr](plans/gameplay.md)** — `GetStartingLine` fidelity open.
- [ ] **[9.2 Conversation UI](plans/gameplay.md)** — UI slice landed; presentation completion and
  live acceptance open. Line audio and subtitles are AUD3's.
- [x] **9.3a CPython default host + auto-load** · **9.3b `ccmd` + cfg aliases** ·
  **9.3c script filesystem**.
- [~] **[9.3 Level-script execution](plans/gameplay.md)** — core landed; delegated fills open.
- [x] **9.4 Quests/XP, sheet, chargen, genesis** — all seven substeps.
- [x] **9.5 Save/load** *(= 11.9)*.
- [x] **9.6 Dice resolver** → `docs/recovered/dice-system.md`.
- [x] **9.7 The script→engine action surface** → `docs/vtmb/script_api.md`.
- [~] **[9.8 Inventory & items](plans/gameplay.md)** — loose pickup and explicit CommonUI loot
  sessions landed; the plain-container lid mover, drop, barter, inventory-check and travel policy
  open. Its cue and the animated-container `soundgroup` path are AUD2's.
- [~] **[9.9 NPC disposition & reactions](plans/gameplay.md)** — talk/feed slice and the
  reaction-score calculator landed; `React`, expression policy and the dialogue consumer open.
- [ ] **[9.10 Economy](plans/gameplay.md)**

*Slice acceptance:* `sp_tutorial_1` completable as retail — dialogue, quests, save/load.

## P10 — Scale & ship-shape

- [ ] **[10.1 Horizontal scale-out](plans/world.md)** · **[10.2 Perf deepening](plans/world.md)** ·
  **[10.3 Floor validation](plans/world.md)** `[needs 4060]`
- [ ] **[10.4 Async travel state machine](plans/spine.md)** — trigger: when hitches matter.
- [ ] **[10.5 Packaged-build content path](plans/spine.md)**
- [~] **[10.6 Input path — Enhanced Input, remapping, gamepad](plans/input.md)** — gameplay pad
  layout and generated Xbox/DualSense glyph switching live; keyboard migration, Discipline radial,
  remapping/profile projection and physical Xbox/DualSense acceptance open.
- [P] **[10.7 Long tail](plans/gameplay.md)** — door-obstruction producers,
  follower/return-to-initial, vdata-driven systems; promote per item when reached.
- [x] **10.8 OpenLevel map lifecycle** → `docs/architecture/map-architecture.md`.
- [ ] **[10.9 Asset enhancement](plans/world.md)** — post-thaw.

## P11 — Runtime spine *(design: `docs/architecture/runtime-architecture.md`, rules S1–S12; detail: [plans/spine.md](plans/spine.md))*

- [x] **11.0 Adopt the spine** · **11.1 Frame + clock** · **11.2 World services** ·
  **11.3 App state machine** · **11.4 The player entity** · **11.5 Input scope stack** ·
  **11.6 Command registry + user command** · **11.7 Camera component** ·
  **11.8 Presentation seam** · **11.9 Save/load** · **11.11 Move-first frame order** ·
  **11.12 Map activation barrier**.
- [ ] **[11.10 Play test tier](plans/spine.md)** — the beat-script driver; PP0's finish.
- [~] **[11.13 Reconstruction camera director](plans/spine.md)** — a–c landed via CCC2; d–h open.
- [x] **11.14 The reachability query** → `docs/architecture/gameplay-systems-architecture.md` §5.5.4.
- [x] **11.15 The perception queries** → `docs/architecture/gameplay-systems-architecture.md` §5.5.3.

## The 3 C's slice (CCC) — detail: [plans/three-cs.md](plans/three-cs.md)

Camera, controls and the played movement feel; the animation rungs are the LIFE programme's.

- [~] **[CCC0 The instrument](plans/three-cs.md)** — gym + channels landed; three sited
  feature courses open.
- [x] **CCC1 The body sample** — one contract, two producers.
- [x] **CCC2 Camera service foundation + modern rig.**
- [~] **[CCC3 Controls response](plans/three-cs.md)** — curves and leniency landed; the
  co-tune open.
- [x] **CCC9 Scaffolding retired.**
- [ ] **[CCC8 Played acceptance](plans/three-cs.md)** — the slice's finish line, owner-played.

## P12 — The theatre *(the PP2 rung; detail: [plans/theatre.md](plans/theatre.md))*

- [~] **[12.1 Choreographed scenes](plans/theatre.md)** — the reader, the timeline and the event
  handlers stand; the staging around them does not. Actor placement, the camera coupling and the
  triggers regressed under CCC2 and LIFE0–4 and have not been exercised since, so **LIFE7's
  choreo-scene rewire owns them** and this row waits on it. Residual RE32 material/remap work
  and the final live acceptance stay here.
- [~] **[12.3 Facial flex track](plans/theatre.md)** — built; pending an unobstructed visual.
- [~] **[12.4 Eyes and eyelids](plans/theatre.md)** — built through the gaze layer; debug
  surface + theatre acceptance open.
- [ ] **[12.5 Lipsync](plans/theatre.md)** — reads AUD3's scheduled line clock.

Each of 12.3, 12.4 and 12.5 states its acceptance on a played theatre scene, so all three are
gated on the same staging LIFE7 restores. Their implementations are unaffected; only the run that
proves them is unreachable until the rewire lands.

Scene line audio, subtitles and the mixahead lead are AUD3 → [plans/audio.md](plans/audio.md).

*Slice acceptance:* New Game → the full theatre act plays — choreography, camera, audible
subtitled lines, live faces — and hands the player to the tutorial, unassisted.

## P13 — Tutorial mechanics *(the PP6 rung; detail: [plans/gameplay.md](plans/gameplay.md))*

- [~] **[13.1 Stealth](plans/gameplay.md)** — target surface, modifier volumes and the senses
  consumer landed headless; the played lessons and the stealth-kill transaction open.
- [~] **[13.2 Disciplines](plans/gameplay.md)** — activation, expiry, the targeted transaction
  and the teardown landed headless; the played lesson, frenzy and the per-rank native power
  consumers open.
- [~] **[13.3 Firearms & melee basics](plans/gameplay.md)** — the damage spine and the weapon
  controller landed headless; the played lessons and the numeric RE joins open.
- [~] **[13.4 Computer terminals & tutorial hacking](plans/gameplay.md)** — the parser, state
  machine, `hackcmd` path and the `tuthack` output transaction landed headless, and the console
  projects through the model's `screen` slot; session escape, camera framing, the idle screensaver,
  the CRT/type pass, email and the played acceptance open. Its cues are AUD2's.
- [~] **[13.5 Combat AI](plans/gameplay.md)** — bus, senses, conditions, the enemy transaction,
  the schedule families and `aiscripted_schedule` landed headless; the played beats and the flinch
  action family open. The footstep hearing producer is AUD2's.

*Slice acceptance:* `sp_tutorial_1` completable as retail on keyboard/mouse and gamepad, proven
by `uv run elysium test Play`.

## The physics substrate (PHYS) — detail: [plans/gameplay.md](plans/gameplay.md)

*Design: `docs/architecture/physics-architecture.md`; facts: `docs/vtmb/phy_vphysics.md`,
`docs/vtmb/physics-interaction.md`.* One owner for every simulated body — the prop (8.4, landed),
the corpse, the carried chair and the explosion kick.

- [ ] **[PHYS1 The ragdoll rig](plans/gameplay.md)** — deferred simulation-ready PhysicsAsset construction, solver calibration, ragdoll activation and gameplay handoff. Consumes R8's preserved GLB/cooked physics source data; none of these simulation features block R8 completion.
- [ ] **[PHYS2 The physics hands](plans/gameplay.md)** — `weapon_physcannon` is *Hands*: HL2's
  grab wired to `+use`, over the existing `WhileHeld` session and the exported `PhysicsHand` cursor.
- [ ] **[PHYS3 The rest of the physics world](plans/gameplay.md)** — `func_physbox`, the `phys_*`
  constraint family, `prop_ragdoll`, and `env_physimpact` / `env_physexplosion` over the impulse
  seam.

*Slice acceptance:* a killed NPC ragdolls off its killing blow and stays lootable where it died; the
tutorial's office chair carries and its sardine can throws, from real input.

## The first-beat path (B*) — all landed

- [x] **B1 `env_fade`** · **B2 real entity objects in CPython** · **B3 minimal NPC presence** ·
  **B4 `.dlg` parser + dialogue runner** · **B5 `ccmd` + cfg aliases** · **B6 feed
  interaction** — a played `sp_tutorial_1` feed acceptance run is not claimed →
  `docs/vtmb/feeding.md`.

## Pipeline backlog (PL)

- [~] **[R8 Characters on the GLB corpus](plans/pipeline.md#r8-characters-on-the-glb-corpus)** — development closed 2026-09-06 on the testable V2 path: build and Python suite green, the native import chain (materials, characters, catalogues, cook roots) published, `sp_tutorial_1` / `sm_pawnshop_1` / `sm_hub_1` re-baked with no legacy references (seam_migration.md → R8 → "Closure record"). Remaining: the owner-piloted rendered play pass on those three maps. Enhanced fidelity and expanded source coverage stay deferred in seam_migration.md's R8 fidelity ledger; physics simulation, calibration and ragdoll activation remain outside this milestone; the mount's legacy folders and the 106 unconverted maps are R9.

- [x] **PL1–PL5d** — entity models, scripts/dialogue, use-icon atlas, NPC banks,
  schemes/vdata/cfg mirrors.
- [ ] **[PL6 Texlight merge in exporter](plans/pipeline.md)** ·
  **[PL11 Remove the dead card path](plans/pipeline.md)** ·
  **[PL12 Particle mirror + weather height maps](plans/pipeline.md)**

The audio catalog and its typed sidecars are AUD0 → [plans/audio.md](plans/audio.md).

The first-person model corpus (old PL14) is LIFE6's first half →
[plans/animation.md](plans/animation.md).
- [x] **PL7–PL10, PL13, PL15, PL16, PL18–PL20** — space audit, scenes/`.lip`, facial data, UI
  export, player bodies, Masquerade meter, cinematic banks, animated-prop closure, bake caching,
  the wield-model corpus and its bake.

## RE backlog

Findings live only in the owning doc each row names; a row here is question · status · pointer.

| ID | Question | Owner / consumer | Status |
|---|---|---|---|
| RE1–RE3 | spawnflags; think order; error-to-false | `docs/vtmb/entity_io.md`, `game_runtime.md`, `python_bridge.md` | [x] |
| RE4 | Ghidra datamap export vs retail tables | optional, valuable | [~] |
| RE5, RE6, RE8–RE16 | dice; survey counts; fades; the sky chain | owning docs; rows in git history | [x] |
| RE7 | retail `.sav` wire format | 10.7 import (non-goal) | [P] |
| RE18–RE22 | script API; scenes; facial formats; frame order; hulls | owning docs | [x] |
| RE23 | particle format — the runtime is decoded from `engine.dll` (`frames`/`fps`, `v(n)`, the mode-8 blend, the emitter basis, collision, the 19-value attach enum; `docs/vtmb/effects.md` §2.4); open: whether `0x200d3840` is the `lighting` light sample, mode 3's per-segment tint, and the wetness time units | `docs/vtmb/weather.md`; 7.9, PL12 | [ ] |
| RE24–RE29 | sheet; chargen; traits; quests; genesis exit; name matching | `docs/vtmb/game_runtime.md`, `entity_io.md` | [x] |
| RE30, RE31 | env-audio DSP precedence; RandomSound scheduler | `docs/vtmb/audio_pipeline.md`; AUD7, AUD6 | [ ] |
| RE32 | source-attributed `sp_theatre` run joined to bytes/export | `docs/vtmb/vtmb-animation-reverse-engineering.md`; 12.1 | [~] |
| RE33 | facial/lip runtime equivalence — verifies 12.3–12.5, never gates | `docs/vtmb/facial_animation.md`; 12.3–12.5 | [~] |
| RE34 | the eye system end to end | `docs/vtmb/facial_animation.md` | [x] |
| RE35 | the prop/trigger entity surface | `docs/vtmb/entity_io.md` + siblings | [x] |
| RE36 | melee block / `+wpn_secondaryatk`; open: the `vhotkey` deferral | `docs/vtmb/controls.md`; 10.6, 13.3 | [~] |
| RE37 | the gameplay-action selection chain | `docs/vtmb/animation_and_movers.md` A.3 | [x] |
| RE38 | inventory ownership and transfer | `docs/vtmb/inventory.md` | [x] |
| RE39 | computer terminals; open: TERM1 skill-entity join, TERM2 generic use outputs + teardown exits, TERM9 player mode fields | `docs/vtmb/computer-terminals.md`; 13.4 | [~] |
| RE40 | the core mechanics chain; open joins numeric | `docs/vtmb/combat-and-damage.md` + siblings; 13.3 | [~] |
| RE41 | discipline authority/interpreter; activity/witness admission plus Elysium/HUD world-area authority and Bloodbuff/`LockPick` exception closed; open: native power consumers and client disable presentation | `docs/vtmb/disciplines.md`; 13.2 | [~] |
| RE42 | first-person viewmodel; static composition/pose/projection/authority closed, and with it the two-entity creation with `m_hViewModel[2]` sized, the activity-translation selection chain and the `ACT_VM_*` registry (a `2` suffix is attack mode, not a hands counterpart), the idle/fidget think, deploy/holster with the switch-only `lower`, the per-frame placement transaction and its named ConVars and constants, the clan/sex hands rule with its two item suppressors, and the shell/clip eject events. Open: the alternate weapon-attachment placement source, the blend interface identity, `THAUMATURGY`'s and `ENFIELD`'s owning class, what retail does with a missing hands model, and the fourth live `viewmodel` entity. The ELGVM1 harness is built but 12 of its 13 scenarios have never run and the one that did finished partial on a timeout, so no viewmodel claim is capture-verified | `docs/vtmb/animation_and_movers.md`, `camera-view-modes.md`, `wielded_weapons.md`; LIFE6 | [~] |
| RE43 | the tutorial event-resolution transaction; engine contact order (new-begin before old-end, spatial enumeration, teleport defers) and the autosave txn recovered | `docs/vtmb/sp_tutorial_1-event-surface.md`, `entity_io.md` | [x] |
| RE44 | the exported-map event surface beyond the tutorial | `docs/vtmb/exported-map-event-surface.md` | [~] |
| RE45 | trigger touch dispatch recovered (synchronous new StartTouch, deferred old EndTouch at the post-think pass) | `docs/vtmb/entity_io.md`, `python_bridge.md` | [x] |
| RE46 | the dialogue opener and camera boundary | `docs/vtmb/camera-view-modes.md`; 11.13f | [~] |
| RE47 | the tutorial character bootstrap | `docs/vtmb/npc-ai-reverse-engineering.md` | [~] |
| RE48 | what an NPC's enemy is — the selection chain | `docs/vtmb/npc-ai-reverse-engineering.md`; 13.3, 13.5 | [x] |
| RE49 | the player-stealth observer and detection transaction | `docs/vtmb/stealth.md`; 8.9, 13.1, 13.5 | [x] |
| RE50 | stealth-kill victim selection and deaf-zone transaction | `docs/vtmb/stealth.md`; 13.1 | [x] |
| RE51 | the player entity and world relationship; lifecycle, world-area/verb policy and law/Masquerade/police/pursuit transactions closed; open: 277-field ledger, camera/travel, area save retention and live world teardown | `docs/vtmb/player-entity.md`; 9.8, LIFE6, 13.1–13.4 | [~] |
| RE53 | the discipline-magnitude retail/patch delta — Blood Buff's `Min 5` floor, Potence's flat Strength `+1` and Fortitude's single re-applied group are retail's authoring where the patch-first corpus scales all three; which one a remake reproduces is an open owner call, default retail | `docs/vtmb/disciplines.md`; 13.2 | [ ] |
| RE54 | the rigid-body world: the `.phy` ragdoll rig (bone-named solids + `ragdollconstraint` limits, 324 models, 289 on one 15/14 humanoid shape), the client-ragdoll death result (the corpse entity *is* the frozen NPC at the death origin; the flopping body is client-only) and its suppressors, the placed physics surface, and the player's object handling recovered whole — `weapon_physcannon` is the hidden *Hands* item every retail `StartingEquip` grants (the patch moves the grant into `vamputil.py`, it disables nothing), reached from `CBasePlayer::PlayerUse` via `Inventory_Find`, ahead of `FindUseEntity`, with the ray→hull→cone search, the eligibility predicate and the full ConVar table; `player_pickup`/`CPlayerPickupController` proven dead (zero callers). Open: the two eligibility limit constants (float args the decompiler dropped at `0x10411160`), the release/throw path and `player_throwforce`'s consumer, whether the light-object lob is real, whether a thrown object raises a sound NPCs hear, the solid-transform frame and the constraint axis identity | `docs/vtmb/physics-interaction.md`, `phy_vphysics.md`; PHYS1–PHYS3 | [~] |
| RE-K1–RE-K9 | knockback: the normal-hit/knockback callback pair and its place in the contact order, the authored per-attack candidate table's location and bucket rotation, the SMALL/NORMAL + HIGH/LOW selector, the confirmed body-goes token convention with its asymmetric bands and NPC-only yaw snap, the launch — a two-stage velocity assignment with recovered magnitude interpolation, direction and one-think delay — the `KnockbackPreventTime` consumer and the flying chain's land/wall terminator all recovered; the discipline path recovered — `HitInfo`'s `Knockback` key enters the shared chain as a boolean with its authored percent discarded, while `AI_Schedule` resolves a schedule by name and bypasses the chain, and the only two `TASK_SET_KNOCKBACK_ACTIVITY` schedules are its Blood Strike and Burrowing Beetle reactions, animation-only with no launch; `knockback_chance` recovered as a **dead field**, parsed by retail and read by nothing, and `COND_KNOCKBACK` as a **dead condition**, registered and read but produced nowhere, leaving its two selector branches and three schedule interrupts unreachable, joined by a third dead mechanism — a marker-scaled push vector computed on every melee hit into a global with no readers. Both remaining opens closed: `Major`/`MinorKnockbackDist` are **live**, read per fire mode by the victim's slot-330 override off shooter-to-victim distance, entering the shared chain with a family index into the direction/height/family table — major resolves `FLYING_INTO_*` and launches, minor resolves `SMALL_*` and does not, `NORMAL` is unreachable — so a gunshot is the second entry to the chain and the player's slot is a no-op; and hit-buildup counting is **per victim**, one scalar with four touch sites and no attacker keying, cleared by the body's own swing passing `melee_swing_completion_percent` (`0.8`). The eligibility gate is fully named — a `CNPC_VTzimisceRunner` class bypass, `Disallow_Knockbacks` (17 templates), and a dead-victim refusal on clamped `Health == Max_Health`, with the health commit ordered before the knockback entry so a killing blow is never knocked back. Victim `+0xA8` is behaviourally closed — the `CBasePlayer` self-pointer, verified; `+0x98` is its sibling, the NPC self-pointer | `docs/vtmb/combat-and-damage.md`, `animation_and_movers.md`; LIFE5 | [x] |
| RE-D1–RE-D5 | death: envelope/sequence, schedule, solid-body policy, script deferral, the two npctemplate keys | `docs/vtmb/combat-and-damage.md`, `npc-ai-reverse-engineering.md` | [x] |
| RE-R1–RE-R4 | the reaction channel: `DamageFlinch`'s fade envelope at `0x103229d0` recovered as 0.1 s in / 0.3 s out with no hold, retiring the hold-floor interim; `0x103302e0`'s reach to `DamageFlinch` on the damaging blocked path settled; `0x10345AB0`'s facing constant read as `0.0f`, so the frontal test is the open forward hemisphere with a `1e-4` coincident-origin bypass that allows the block; and the resume phase recovered as **no stored phase at all** — the apply path zeroes `m_flCycle` on a sequence change unless the incoming activity is a gait (the NPC path requires both sides to be one), `ResetSequenceInfo` never touches the cycle, so a base-channel reaction hands the resumed gait its own ending cycle | `docs/vtmb/combat-and-damage.md`, `animation_and_movers.md`; LIFE5 | [x] |
| RE-W1–RE-W6 | the whole-body corpus instrument: names from strings, datamap/RTTI class types, a decompiled corpus, the field ledger, the CLI + MCP query surface and subsystem closures | `research/tooling/ghidra/driver/README.md` | [x] |
| RE-W7 | corpus v2: the virtual call graph — VtMB dispatches its logic through vtables, so `callers` answered "none" for methods the game calls constantly. 291,937 vtable slots joined to 42,178 decompiled dispatch sites, reported as direct / virtual / possible and never merged; repair passes ahead of the dump (11,779 recovered signatures, 12 jump tables read from the image, 28 oversized bodies verified genuine); the datamap input surface recovered from the `DEFINE_INPUTFUNC` records the earlier pass silently discarded; slot-seeded function creation for code no CALL ever reaches; decompiler damage flagged by severity, globals indexed, disassembly queryable. 71,235 functions, unnamed down from 77.8 % to 46.7 % | `research/tooling/ghidra/driver/README.md` | [x] |

## Options — evaluated, not planned

Remote Control API, Gameplay Debugger category, NetImgui, console autocomplete, Slate console
(superseded), **Lumen Lite** (low-end contingency; revisit if 10.3 fails — VtMB's look is
bounce-dominated, so it risks the look).

## Risk register

| Risk | Mitigation |
|---|---|
| MegaLights silently disengages | the 0.2 check lives in the profiling routine; 3.1 pins it |
| Embedded VM fails in a packaged build | `MakePreferredScriptHost` falls back to expr, logged |
| Chaos movers push/block poorly | prototyped at 4.1; the push observable is 4.8's |
| Floor perf unproven (no 4060 on hand) | 10.3 gate; lump-8 bake parked; Lumen Lite noted |
| Tutorial-only calibration bias | second map at 0.3; all calibration provisional until 10.1 |
| Save determinism erodes | no engine timers; owned serializable structs |
| Legal posture | bring-your-own-game holds — standing constraint on every task |
| `GameInputWindows` beta + redist prerequisite | device configs are data; XInput fallback; redist in 10.5 |
| Asset enhancement drifts off-style | adjudication test + `elysium.EnhancedTextures` A/B |
| Modern UI loses VtMB's voice | 8.6 re-skins craft only; presentation test per screen |
| "Polish" leaks into the logic layer | RE first, owner call, divergence recorded once |
| A shots baseline invalidates across a re-bake | re-baseline after any bake/content change; A/B as two runs over one fixed asset set |
