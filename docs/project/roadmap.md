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
- Old plan IDs (M*, L*, X*, retired CAP numbering) are resolved by git history
  (`git log -S "<id>"`), not by a mapping table.

## North star

Rebuild VtMB as a playable game — remastered — on UE 5.8 + C++ from this repo's own exported
intermediates; bring-your-own-game holds (`docs/project/rebuild-strategy.md`). The world's look
is baked offline into the gitignored `/ElysiumBaked` mount; collision, entities, scripting,
audio and NPCs stay runtime-built. Everything is proven on `sp_tutorial_1` (1,226 entities, 75
classnames), then scaled across ~100 maps. Direction — the presentation/feel/logic layers, the
three adjudication tests, default-to-reproduce: `docs/project/remaster-direction.md`.

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
| **PP2 — the theatre cinematic** | the intro plays start to finish — eyes and lipsync block | 12.1–12.5, 8.11a [x] |
| **PP3 — land the tutorial** | Jack's first conversation with sound and reactions | 9.2, 9.9 |
| **PP4 — core mechanics** | faithful movement, modern camera, gait, feeding, items, dice, HUD foundation + observer presentation seam | CCC0–CCC9, B6 [x], 9.8, 9.6 [x], 8.9 |
| **PP5 — persistence [x]** | save / quick / autosave + load mid-run | 11.9 [x] |
| **PP6 — complete the tutorial** | stealth authority, disciplines, firearms, combat AI, hacking — every retail beat | 13.1–13.5 → `uv run elysium test Play` |

**PP4's feel stack runs ahead of PP2/PP3 — owner call**, recovered rather than preferential:
the animation is the movement's speed authority, so the mover cannot close behind it (the CCC
ladder, [plans/three-cs.md](plans/three-cs.md)). **After PP6 (the thaw):** 9.10, 8.8, 8.10, the
P3/P7 look lanes, 10.1–10.5 and asset enhancement — re-sequenced then.

**Map priority — owner call:** `sp_theatre` → `sp_tutorial_1` → `sm_pawnshop_1` → `sm_hub_1`.
Focused export, bake, and acceptance work targets maps in this order; a broader scope needs its
own owner call.

## Now — the unblocked front

**Owner call: the entity-gameplay ladder is the current priority.** Combat, damage and NPC AI
land as one sequenced slice over the existing substrate seams — descriptor-typed damage first,
weapons on it, the NPC mind's senses/enemy/combat stages next, then their consumers:

1. **13.3** — the damage spine (`FElysiumDmg`, the typed commit, the hurt cadence) and the
   weapon controller → [plans/gameplay.md](plans/gameplay.md).
2. **13.5** — combat AI: the sound-event bus, senses/memory, the enemy transaction, the
   alert/combat schedule families → [plans/gameplay.md](plans/gameplay.md); its 11.14/11.15
   engine queries → [plans/spine.md](plans/spine.md).
3. **9.8 / 9.9** — the inventory remainder; the reaction score and social presentation →
   [plans/gameplay.md](plans/gameplay.md).
4. **13.1 / 13.2** — stealth and disciplines, behind the two rungs above →
   [plans/gameplay.md](plans/gameplay.md).

Continuing fronts behind the ladder: **0.10 / RE32 / RE33 / 12.1** — the retail capture loop →
[plans/capture.md](plans/capture.md); **the 3 C's slice** — the co-tune, then CCC8's played
acceptance → [plans/three-cs.md](plans/three-cs.md); **11.10** — finish PP0 with the
played-input harness → [plans/spine.md](plans/spine.md).

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
- [~] **[0.10 Retail animation RE instrument](plans/capture.md)** — the exact-build probe and
  its capture → index → inspect → close loop; parent row of the CAP programme.

## The retail capture programme (CAP) — detail: [plans/capture.md](plans/capture.md)

- [x] **CAP0 Baseline** — exact-build access, hash-gated hooks, the one-run database boundary.
- [x] **CAP1 First theatre runs + calibration** — three reproducible captures, zero drops.
- [x] **CAP2.1–2.7 Complete the capture** — generations, censuses, attribution, byte spans,
  scene events, second acquisition.
- [ ] **[CAP2.8 The fourth pose-build frame](plans/capture.md)** — melee's unbracketed caller.
- [ ] **[CAP2.9 Complete the `sp_tutorial_1` corpus](plans/capture.md)** — deferred; blocks nothing.
- [x] **CAP3 Decode and index** — deduplicated, joinable, self-reporting database.
- [x] **CAP4.1–4.3 Inspect** — byte-coverage missing list **0**; the shipped decoder is clean;
  shipped-path residual 2,452.
- [ ] **[CAP4.4 Missing-work report](plans/capture.md)** — the cross-pass ranking; consolidates,
  no longer gates.
- [ ] **[CAP5.1 One mismatch at a time](plans/capture.md)** ·
  **[CAP5.2 Deeper stage capture on demand](plans/capture.md)**
- [x] **CAP5.3 Missing-data ranges closed at the exporter** — blend grids exported and baked
  → `docs/vtmb/animation_and_movers.md` A.3.
- [ ] **[CAP5.4 Adjudicate the persistent partial update](plans/capture.md)**
- [~] **[CAP5.5 Secondary motion](plans/capture.md)** — two-body AnimDynamics proof wired;
  numeric replays and calibration open
  → `docs/vtmb/secondary_motion.md`.
- [ ] **[CAP5.6 Theatre-corpus closure](plans/capture.md)**
- [x] **CAP5.7 The shipped multi-biped path modelled** — 180,812 agreeing / 0 disagreeing.
- [x] **CAP5.8 The include-remap route** — 6,927 → 2,452 → `docs/vtmb/animation_and_movers.md` A.4b.
- [ ] **[CAP5.9 The `Prince_Escort_Male` cluster](plans/capture.md)** — undiagnosed, named.
- [ ] **[CAP5.10 Frame interpolation](plans/capture.md)** — is the host's LINEAR retail's.
- [ ] **[CAP6 Face and lips](plans/capture.md)** — fires only on a named divergence.
- [x] **CAP7.1 / CAP7.2 The two composition rules in Unreal** — byte-exact rule table; both
  stages in retail's slot; visual isolation owed to the green-room row.
- [ ] **[CAP8.1 Final trim](plans/capture.md)**

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

Deferred, tracked: console autocomplete, Gameplay Debugger category, Remote Control, NetImgui.

## P3 — Lighting lane — **FROZEN** *(playable-path rule 2; detail: [plans/world.md](plans/world.md))*

- [ ] **[3.1 Pin MegaLights per-light](plans/world.md)** · **[3.2 Shadow curation](plans/world.md)** ·
  **[3.3 Attenuation-radius audit](plans/world.md)** · **[3.4 Texlight clustering](plans/world.md)**
- [x] **3.5 Sky IBL onto the SkyLight** → `docs/vtmb/sky-ambience.md`.
- [ ] **[3.6 Pinned exposure](plans/world.md)** · **[3.7 Grade/tonemapper fidelity](plans/world.md)** ·
  **[3.8 Texture prewarm](plans/world.md)** · **[3.9 Lightstyle clock pin](plans/world.md)** ·
  **[3.10 Volumetric fog calibration](plans/world.md)** ·
  **[3.11 Verify `elysium.LumenDiffuseBoost`](plans/world.md)** ·
  **[3.12 `sm_hub_1` fill adjudication](plans/world.md)** ·
  **[3.13 Decal fog: accept or extend](plans/world.md)**

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
  `prop_switch` interaction, lockables and doorknob sequences landed; hurt cadence plus switch
  sound/reset details remain open.

*Slice acceptance (met):* the tutorial elevator chain works; walking out loads `sm_pawnshop_1`.

## P5 — Scripting foundation *(design: `docs/vtmb/python_bridge.md`)*

- [x] **5.1 Scripts + dialogue mirrored** · **5.2 Expression evaluator** — error-to-false ·
  **5.3 Native bindings** — one `GNativeBindings` table · **5.4 Field-6 +
  `logic_pythoncheck` + `ScheduleTask` live** · **5.5 Embedded CPython 2.7**.

## P6 — Audio foundation *(detail: [plans/audio.md](plans/audio.md))*

- [x] **6.1 MS-ADPCM decode** · **6.2 MP3 decode** · **6.3 `ambient_generic` + SoundSchemes** ·
  **6.4 Mover sounds**.
- [ ] **[6.5 Final loose-audio core + catalog](plans/audio.md)** ·
  **[6.6 UE mixer graph + mix policy](plans/audio.md)** ·
  **[6.7 Map ambience, music + DSP closure](plans/audio.md)** ·
  **[6.8 Gameplay audio adapters](plans/audio.md)**

## P7 — Dressing & parity — **open tasks FROZEN** *(detail: [plans/world.md](plans/world.md))*

- [ ] **[7.1 Coronas](plans/world.md)**
- [x] **7.2 Decals** — deferred `UDecalComponent`s through the bake.
- [ ] **[7.3 Water](plans/world.md)**
- [x] **7.4 Master-material set** — the generated surface masters.
- [ ] **[7.5 Real reflections](plans/world.md)** → `docs/vtmb/reflections.md` ·
  **[7.6 Bloom/glow tuning](plans/world.md)** · **[7.7 Shadow quality](plans/world.md)** ·
  **[7.8 A/B capture harness](plans/world.md)** · **[7.9 Weather & wetness](plans/world.md)**

*Slice acceptance:* side-by-side A/B match with the original's reference captures (RE17).

## P8 — Characters & UI *(detail: [plans/characters-ui.md](plans/characters-ui.md))*

- [x] **8.1 Entity-model export** · **8.2 Native skeletal adoption** — ESKM → baked assets ·
  **8.3 Dynamic props** · **8.4 Physics props** — Chaos over `.phy`.
- [~] **[8.4a `prop_dynamic` divergences](plans/characters-ui.md)** — animation half done;
  `solid` and `disableshadows` unread.
- [x] **8.4b Placed-model rest-pose closure** — every `.ents` and GAME_LUMP MDL resolves and
  installs an authored rest pose before visibility; proven-equivalent placements remain static →
  `docs/architecture/animation-architecture.md`.
- [x] **8.5 NPC presence + native locomotion + `scripted_sequence` minimal** — the animation
  half is the ANM programme's.
- [x] **8.6a New Game context + story entry.**
- [~] **[8.6 UI foundation — design system + shell](plans/characters-ui.md)** — landed; device
  navigation acceptance and the New Game click path open →
  `docs/architecture/ui-architecture.md`.
- [x] **8.7 Ropes** — `UCableComponent` over the RE'd rest-length arithmetic.
- [~] **[8.8 Sign / popup panels](plans/characters-ui.md)** — screen landed; format features open.
- [~] **[8.9 HUD on the UI foundation](plans/characters-ui.md)** — vitals and queued item/quest
  notifications landed; selectors and subtitles open.
- [ ] **[8.10 Accessibility & options backing](plans/characters-ui.md)**
- [x] **8.11a The player body** — clan/sex/slot resolution, dithered fade, choreography-ready.
- [ ] **8.11b Locomotion** — the CCC slice's; assets are the ANM programme's.

## The skeletal animation programme (ANM) — detail: [plans/animation.md](plans/animation.md)

Governing decision — bake native, let Unreal run it: `docs/architecture/animation-architecture.md`.

- [ ] **[ANM1 Bake the character assets](plans/animation.md)** — shared banks packaged once across
  compatible rig families; no bank/body cross-product and no runtime glTF.
- [~] **[ANM2 The two discarded MDL fields](plans/animation.md)** — masks and bindings
  delivered; orphan census + the dispatcher weight open.
- [x] **ANM3 Bake the blend spaces** — parity-asserted `UBlendSpace` per declared grid.
- [x] **ANM4a Gameplay action selection extracted** *(= RE37)* →
  `docs/vtmb/animation_and_movers.md` A.3.
- [ ] **[ANM4b Export and bake the action catalog](plans/animation.md)**
- [ ] **[ANM6 Migrate the cinematic path](plans/animation.md)** — deliberately last; owns the
  gesture un-collapse.

## P9 — Dialogue & persistence *(detail: [plans/gameplay.md](plans/gameplay.md))*

- [~] **[9.1 `.dlg` parser + dlgexpr](plans/gameplay.md)** — `GetStartingLine` fidelity open.
- [ ] **[9.2 Conversation UI + audio-by-path](plans/gameplay.md)** — UI slice landed; audio open.
- [x] **9.3a CPython default host + auto-load** · **9.3b `ccmd` + cfg aliases** ·
  **9.3c script filesystem**.
- [~] **[9.3 Level-script execution](plans/gameplay.md)** — core landed; delegated fills open.
- [x] **9.4 Quests/XP, sheet, chargen, genesis** — all seven substeps.
- [x] **9.5 Save/load** *(= 11.9)*.
- [x] **9.6 Dice resolver** → `docs/recovered/dice-system.md`.
- [x] **9.7 The script→engine action surface** → `docs/vtmb/script_api.md`.
- [~] **[9.8 Inventory & items](plans/gameplay.md)** — loose pickup and explicit CommonUI loot
  sessions landed; plain-container lid/sound, drop, barter, inventory-check and travel policy open.
- [~] **[9.9 NPC disposition & reactions](plans/gameplay.md)** — talk/feed slice landed;
  reaction score and expression policy open; senses are 13.5's.
- [ ] **[9.10 Economy](plans/gameplay.md)**

*Slice acceptance:* `sp_tutorial_1` completable as retail — dialogue, quests, save/load.

## P10 — Scale & ship-shape

- [ ] **[10.1 Horizontal scale-out](plans/world.md)** · **[10.2 Perf deepening](plans/world.md)** ·
  **[10.3 Floor validation](plans/world.md)** `[needs 4060]`
- [ ] **[10.4 Async travel state machine](plans/spine.md)** — trigger: when hitches matter.
- [ ] **[10.5 Packaged-build content path](plans/spine.md)**
- [~] **[10.6 Input path — Enhanced Input, remapping, gamepad](plans/input.md)** — partial
  slice live; keyboard migration, pad layout, projection open.
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
- [~] **[11.13 Remaster camera director](plans/spine.md)** — a–c landed via CCC2; d–h open.
- [ ] **[11.14 The reachability query](plans/spine.md)** *(S11, S12)* — `ProjectToNavigable`
  behind the door-obstruction retreat.
- [ ] **[11.15 The perception queries](plans/spine.md)** *(S11, S12)* — line-of-sight and
  light-at-point behind the senses and the stealth surface.

## The 3 C's slice (CCC) — detail: [plans/three-cs.md](plans/three-cs.md)

- [~] **[CCC0 The instrument](plans/three-cs.md)** — gym + channels landed; three sited
  feature courses open.
- [x] **CCC1 The body sample** — one contract, two producers.
- [x] **CCC2 Camera service foundation + modern rig.**
- [~] **[CCC3 Controls response](plans/three-cs.md)** — curves and leniency landed; the
  co-tune open.
- [x] **CCC4 Intent, resolver, selection record** · **CCC5 The player animation graph** ·
  **CCC6 The green room drives it** · **CCC9 Scaffolding retired** ·
  **CCC10 The weapon rung (third person)**.
- [~] **[CCC7 `move_yaw` + the speed authority](plans/three-cs.md)** — the player producer
  landed; the NPC producer never took the push, so the cast travels on constants.
- [ ] **[CCC8 Played acceptance](plans/three-cs.md)** — the slice's finish line, owner-played.
- [ ] **[CCC10.1 The first-person viewmodel body](plans/three-cs.md)** — waits on RE42's live
  verifier, PL14, 11.13d and the semantic animation-intent seam.
- [ ] **[CCC11 Action families beyond locomotion](plans/three-cs.md)** — reactions, weapon
  actions, the sequence-event carrier.

## P12 — The theatre *(the PP2 rung; detail: [plans/theatre.md](plans/theatre.md))*

- [~] **[12.1 Choreographed scenes](plans/theatre.md)** — implemented and verified; residual
  RE32 material/remap work + final live acceptance.
- [ ] **[12.2 Scene audio + subtitles](plans/theatre.md)** ·
  **[12.2b Scene mixahead calibration](plans/theatre.md)**
- [~] **[12.3 Facial flex track](plans/theatre.md)** — built; pending an unobstructed visual.
- [~] **[12.4 Eyes and eyelids](plans/theatre.md)** — built through the gaze layer; debug
  surface + theatre acceptance open.
- [ ] **[12.5 Lipsync](plans/theatre.md)**

*Slice acceptance:* New Game → the full theatre act plays — choreography, camera, audible
subtitled lines, live faces — and hands the player to the tutorial, unassisted.

## P13 — Tutorial mechanics *(the PP6 rung; detail: [plans/gameplay.md](plans/gameplay.md))*

- [ ] **[13.1 Stealth](plans/gameplay.md)** · **[13.2 Disciplines](plans/gameplay.md)** ·
  **[13.3 Firearms & melee basics](plans/gameplay.md)** ·
  **[13.4 Computer terminals & tutorial hacking](plans/gameplay.md)** ·
  **[13.5 Combat AI](plans/gameplay.md)**

*Slice acceptance:* `sp_tutorial_1` completable as retail on keyboard/mouse and gamepad, proven
by `uv run elysium test Play`.

## The first-beat path (B*) — all landed

- [x] **B1 `env_fade`** · **B2 real entity objects in CPython** · **B3 minimal NPC presence** ·
  **B4 `.dlg` parser + dialogue runner** · **B5 `ccmd` + cfg aliases** · **B6 feed
  interaction** — a played `sp_tutorial_1` feed acceptance run is not claimed →
  `docs/vtmb/feeding.md`.

## Pipeline backlog (PL)

- [x] **PL1–PL5d** — entity models, scripts/dialogue, use-icon atlas, NPC banks,
  schemes/vdata/cfg mirrors.
- [ ] **[PL6 Texlight merge in exporter](plans/pipeline.md)** ·
  **[PL11 Remove the dead card path](plans/pipeline.md)** ·
  **[PL12 Particle mirror + weather height maps](plans/pipeline.md)** ·
  **[PL14 Export the complete first-person model corpus](plans/pipeline.md)** ·
  **[PL17 Patch-first audio catalog](plans/pipeline.md)**
- [x] **PL7–PL10, PL13, PL15, PL16, PL18, PL19** — space audit, scenes/`.lip`, facial data, UI
  export, player bodies, Masquerade meter, cinematic banks, animated-prop closure, bake caching.

## RE backlog

Findings live only in the owning doc each row names; a row here is question · status · pointer.

| ID | Question | Owner / consumer | Status |
|---|---|---|---|
| RE1–RE3 | spawnflags; think order; error-to-false | `docs/vtmb/entity_io.md`, `game_runtime.md`, `python_bridge.md` | [x] |
| RE4 | Ghidra datamap export vs retail tables | optional, valuable | [~] |
| RE5, RE6, RE8–RE16 | dice; survey counts; fades; the sky chain | owning docs; rows in git history | [x] |
| RE7 | retail `.sav` wire format | 10.7 import (non-goal) | [P] |
| RE17 | owner-run reference captures at the shared vantages | 3.6/3.7, 7.8; gate cleared, captures unrun | [ ] |
| RE18–RE22 | script API; scenes; facial formats; frame order; hulls | owning docs | [x] |
| RE23 | particle format + wetness — retail evidence for units/semantics | `docs/vtmb/weather.md`; 7.9, PL12 | [ ] |
| RE24–RE29 | sheet; chargen; traits; quests; genesis exit; name matching | `docs/vtmb/game_runtime.md`, `entity_io.md` | [x] |
| RE30, RE31 | env-audio DSP precedence; RandomSound scheduler | `docs/vtmb/audio_pipeline.md`; 6.7 | [ ] |
| RE32 | source-attributed `sp_theatre` run joined to bytes/export | [plans/capture.md](plans/capture.md) | [~] |
| RE33 | facial/lip runtime equivalence — verifies 12.3–12.5, never gates | [plans/capture.md](plans/capture.md) | [~] |
| RE34 | the eye system end to end | `docs/vtmb/facial_animation.md` | [x] |
| RE35 | the prop/trigger entity surface | `docs/vtmb/entity_io.md` + siblings | [x] |
| RE36 | melee block / `+wpn_secondaryatk`; open: the `vhotkey` deferral | `docs/vtmb/controls.md`; 10.6, 13.3 | [~] |
| RE37 | the gameplay-action selection chain | `docs/vtmb/animation_and_movers.md` A.3 | [x] |
| RE38 | inventory ownership and transfer | `docs/vtmb/inventory.md` | [x] |
| RE39 | computer terminals; open: TERM2/3/6–8 | `docs/vtmb/computer-terminals.md`; 13.4 | [~] |
| RE40 | the core mechanics chain; open joins numeric | `docs/vtmb/combat-and-damage.md` + siblings; 13.3 | [~] |
| RE41 | discipline authority/interpreter; activity/witness admission plus Elysium/HUD world-area authority and Bloodbuff/`LockPick` exception closed; open: native power consumers, client disable presentation and live cast matrix | `docs/vtmb/disciplines.md`; 13.2 | [~] |
| RE42 | first-person viewmodel; static composition/pose/projection/authority and ELGVM1 harness closed, controlled retail matrix open | `docs/vtmb/animation_and_movers.md`, `camera-view-modes.md`; CCC10.1, PL14 | [~] |
| RE43 | the tutorial event-resolution transaction; open: engine contact order, autosave txn | `docs/vtmb/sp_tutorial_1-event-surface.md` | [~] |
| RE44 | the exported-map event surface beyond the tutorial | `docs/vtmb/exported-map-event-surface.md` | [~] |
| RE45 | trigger touch dispatch + the script recursion bound | `docs/vtmb/entity_io.md`, `python_bridge.md` | [~] |
| RE46 | the dialogue opener and camera boundary | `docs/vtmb/camera-view-modes.md`; 11.13f | [~] |
| RE47 | the tutorial character bootstrap | `docs/vtmb/npc-ai-reverse-engineering.md` | [~] |
| RE48 | what an NPC's enemy is — the selection chain | `docs/vtmb/npc-ai-reverse-engineering.md`; 13.3, 13.5 | [x] |
| RE49 | the player-stealth observer and detection transaction | `docs/vtmb/stealth.md`; 8.9, 13.1, 13.5 | [x] |
| RE50 | stealth-kill victim selection and deaf-zone transaction | `docs/vtmb/stealth.md`; 13.1 | [ ] |
| RE51 | the player entity and world relationship; lifecycle, world-area/verb policy and law/Masquerade/police/pursuit transactions closed; open: 277-field ledger, camera/travel, area save retention and live world teardown | `docs/vtmb/player-entity.md`; 9.8, CCC10.1, 13.1–13.4 | [~] |

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
