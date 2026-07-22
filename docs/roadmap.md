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

- [ ] **0.1 Profiling baseline** *(was L0.1)* — standalone `play.bat` (not PIE): `stat unit`,
  `stat GPU`, `ProfileGPU` on `sp_tutorial_1`; record Lumen/MegaLights/ShadowDepths/VSM ms.
  *Acceptance:* baseline table committed below (appendix); window title `PCD3D_SM6`. *Deps:* none.
- [ ] **0.2 MegaLights engagement check** *(was L0.2)* — `r.MegaLights.Debug 1` + pass
  comparison (`rendering-perf.md` checklist). The catastrophic failure mode is silent VSM
  fallback. *Acceptance:* MegaLights pass dominates, VSM ≈ 0 for local lights; if not, 3.1
  becomes the immediate next task. *Deps:* 0.1.
- [ ] **0.3 Export a second map** *(was X1)* — `sm_pawnshop_1` (the travel target) + one hub
  via `export_all.py`. Unblocks texlights (3.4), travel (4.6), calibration (10.1), A/B (7.8),
  and kills the tutorial-only bias. *Acceptance:* both maps load and walk. *Deps:* none.
- [ ] **0.4 Sidecar space audit** *(was X2 + engine-core P1 step 1)* — verify every sidecar the
  next phases consume is emitted by `UE_bsp_to_scene.py` in Unreal space: `.ents` (origins
  **and** entity-local hulls), `.sprites`, `.spawn`, `.water`, `_decals.obj`. The contract
  table in `rebuild-strategy.md` still marks `.sprites` Godot-metres and `.spawn` Source
  coords — reconcile table vs code, fix stragglers in `tools/bsp.py` terms, update the table.
  *Acceptance:* contract table accurate; all consumed sidecars read verbatim. *Deps:* none.
- [ ] **0.5 Cog 5.8 compile spike** — vendor [Cog](https://github.com/arnaud-jamin/Cog) into
  `Plugins/`, compile against 5.8, open one stock window in PIE. De-risks the P2 dependency
  early (known patches: ImPlot `INFINITY` MSVC error). Full integration is 2.1.
  Upstream requires UE 5.5+ and is actively maintained (issue activity Nov 2025; no
  engine-version compile reports on the tracker), but 5.8 is unconfirmed — hence the spike.
  *Acceptance:* Cog inspector opens in PIE and standalone. *Deps:* none.
- [ ] **0.6 `ent_survey` count reconciliation** *(RE, small)* — 16,125 vs 16,214 outputs /
  1,591 vs 1,621 Python calls across doc snapshots; re-run and pin the numbers in
  `entity_io.md`. *Deps:* none.
- [ ] **0.7 Repo hygiene** — keep `tools/ghidra*/` and `tools/re/` out of the repo
  (gitignore; they stay local RE references). *Deps:* none.

## P1 — Entity substrate *(design: `engine-core.md` — read it; steps here are the tracker)*

- [ ] **1.1 Currency types + persistent state** — `FElysiumVariant`, `FElysiumEntityHandle`
  (stable `.ents` index + epoch), `FElysiumGameClock`; `UElysiumGameStateSubsystem` with the
  `G` store (variant-valued, **default 0 on miss**) and quest map. *Deps:* none.
- [ ] **1.2 `.ents` defs parser** — `FElysiumEntityDefs` (immutable records incl. 7-field
  outputs, hulls, `start_hidden`). *Deps:* 0.4.
- [ ] **1.3 Class registry + base entity** — per-classname descriptor (input table + field
  table, case-folded, base-chain walk); `FElysiumEntity` with `Kill`/`ScriptHide`/
  `ScriptUnhide` and one-switch dormancy (solid+think+draw); inert-record fallback for
  unhandled classnames. *Deps:* 1.1.
- [ ] **1.4 Entity world + event queue + chokepoints** — storage/indices/spawn/teardown,
  `AcceptInput` + queue `Add` as the only two paths, `times` countdown, think servicing,
  zero-delay drain with loop guard; **sinks + 1,000-entry ring buffer + `LogElysiumIO` +
  VLOG land in this same step** (the chokepoints are never uninstrumented);
  `FElysiumNullScriptHost` behind `IElysiumScriptHost` logs field-6 payloads. *Deps:* 1.2, 1.3.
- [ ] **1.5 Brush bodies** — `UElysiumBrushComponent` per brush entity (convex `UBodySetup`
  from def hulls, handle attached, overlap routing, dormancy-gated). *Deps:* 1.4.
- [ ] **1.6 Starter classes** — `logic_auto`, `logic_relay`, `trigger_multiple`/`trigger_once`.
  *Deps:* 1.4, 1.5.
- [ ] **1.7 Labels & debug strings** — `#if WITH_EDITOR` labels/folders on all spawn paths
  (bodies + existing map/light/prop actors); canonical `#<idx> <name>(<class>)` in every log.
  *Deps:* 1.5.

**Slice acceptance:** loading `sp_tutorial_1` fires the `logic_auto` chains through real
queue entries; walking through a trigger logs timestamped I/O lines; Python payloads appear
as script-host log lines; ring buffer holds the session history — observable with logs only.

## P2 — Debug layer *(design: `debug-tooling.md` Layers 1–2)*

- [ ] **2.1 Cog integration** — world subsystem, window toggle, dev-only posture (out of
  Shipping). *Deps:* 0.5.
- [ ] **2.2 Entity windows** — browser (filter/histogram/dormancy), inspector (keyvalues,
  live fields via field tables, 7-field outputs, fire-input buttons), event-queue window
  (pending + history + pause/step). *Deps:* 2.1, 1.4.
- [ ] **2.3 `ent_*` verbs** — `ent_fire` (real queue), `ent_dump`/`ent_info` (off the
  tables), `ent_pause`/`ent_step`, `ent_break`, `ent_text`/`ent_bbox`/`ent_messages`
  bitmask overlays; **picker fallback** (no arg = under crosshair via body handles;
  nearest-origin for bodiless logic ents). *Deps:* 1.4–1.6.
- [ ] **2.4 World visualization** — `elysium.showtriggers` (wireframe hulls by class/state),
  fading I/O beam arrows on fire, entity gizmos (port the Godot viewer UX: color-keyed
  boxes + labels, off/visible/all cycle, cone pick). *Deps:* 1.5.
- [ ] **2.5 Maps/Lights windows + `elysium.reload` + `UCheatManager` subclass** — absorb the
  Canvas HUD panels (HUD keeps FPS/position); reload = re-`Travel` current map for the
  export→reload hot loop. *Deps:* 2.1.

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

- [ ] **4.1 Mover base** — `LinearMove`/`AngularMove` (constant velocity, snap, MoveDone) +
  the shared 4-state toggle machine on kinematic brush bodies; prototype one door **first**
  to de-risk Chaos kinematic sweeps/blocking (deal `dmg`, reverse, `OnBlockedClosing`).
  *Deps:* 1.5, 1.6.
- [ ] **4.2 `func_button`** — press/latch/spring-back per decompiled spawnflags; `OnPressed`
  + `OnIn`/`OnOut` reticle arming; `StartHidden` disarm/arm. *Deps:* 4.1.
- [ ] **4.3 `func_door` / `func_door_rotating`** — full spawnflag table (B.5), `wait -1`,
  locked path + `OnLockedUse`, `linked_door`, hinge = origin. *Deps:* 4.1.
- [ ] **4.4 `+use` verb + use-icon HUD** — camera trace on a use-only channel against the
  13 usable classnames; `E` → `Use` input; icon from `use_icon`/`locked_icon`
  (72-entry enum). *Pipeline:* **PL3 use-icon atlas export**. *Deps:* 4.2, PL3.
- [ ] **4.5 Tutorial logic classes** — `math_counter`, `logic_timer`, `logic_case`,
  `env_fade`, `func_brush`, `point_teleport`, remaining trigger variants
  (`trigger_hurt`/`trigger_autosave`/…) — climb the tutorial histogram with the P2 tools
  watching; **RE1 trigger spawnflag filters** decompiled when filtering first matters.
  *Deps:* 1.6.
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

- [ ] **5.1 PL2: copy scripts + dialogue** — pipeline copies loose `.py` → `out/scripts/`,
  `.dlg` → `out/dlg/` (plain text in the install). *Deps:* none.
- [ ] **5.2 Expression evaluator** — recursive-descent over calls, attribute access,
  literals, comparisons, and/or; `G.<flag>` (default-0), entity lookup by targetname →
  the P1 input/field tables (one namespace — no new dispatch). *Deps:* 1.3, 1.4.
- [ ] **5.3 Native bindings** — the 11 globals + 24 Character methods (stubs logging where
  the backing system doesn't exist yet); `!activator`/`!self`; case-insensitive stat names.
  *Deps:* 5.2.
- [ ] **5.4 Field-6 + `logic_pythoncheck` + `ScheduleTask` live** — real host replaces the
  null host behind `IElysiumScriptHost`; deferred source strings ride the event queue
  (serializable). *Deps:* 5.2, 5.3.
- [ ] **5.5 Level-script decision** *(de-risk early)* — survey `tutorial.py` + the 27 loose
  scripts (8,954 lines, restricted Python 2.1 subset), then **decide**: (a) extend the
  mini-interpreter, (b) offline transpile to IR, or (c) embed CPython 2.x (fallback).
  Fallback (c) is viable on modern toolchains: maintained CPython 2.7.18 forks build with
  VS2019+ (e.g. `qnox/python-2.7`, active Dec 2025) — official 2.7 needs VS2008-era MSVC.
  Log the decision below; implementation is 9.3. *Deps:* 5.1, 5.2.

**Slice acceptance:** the tutorial's field-6 calls and `logic_pythoncheck` gates actually
execute (e.g. `FindPlayer().ClearActiveDisciplines()` runs, `OnTrue`/`OnFalse` fire);
`G` flags flip visibly in the debug layer.

## P6 — Audio foundation *(design: `audio_pipeline.md`; parallel with P5/P7)*

- [ ] **6.1 MS-ADPCM decode** — in C++ into `USoundWaveProcedural`/PCM (~92% of SFX; no
  offline transcode). Library: vendor single-header **`dr_wav`** (public domain; decodes
  MS-ADPCM 0x02 and IMA ADPCM 0x11 natively; upstream issue #295 — int overflow on crafted
  MS-ADPCM — is benign for trusted game data). *Deps:* none.
- [ ] **6.2 MP3 decode** — dialogue/music path. Library: vendor single-header
  **`dr_mp3`/`minimp3`** (public domain; MP3 patents expired — no licensing exposure).
  *Deps:* none.
- [ ] **6.3 `ambient_generic` + SoundSchemes** — entity-driven ambience (`PlaySound` ×618 is
  the 6th most-wired input) + `sound/schemes/*.txt`; **PL5a: scheme file copies**.
  *Deps:* 1.6, 6.1.
- [ ] **6.4 Mover sounds** — `soundgroup` sets on doors/buttons + button explicit WAVs.
  *Deps:* 4.1–4.3, 6.1.

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

- [ ] **8.1 PL1: entity-model export** — export models referenced by `.ents`
  (`prop_dynamic`/`prop_physics` `model` keys — only GAME_LUMP props export today).
  *Deps:* none.
- [ ] **8.2 glTFRuntime adoption spike** — vendor the plugin, load the one exported test
  NPC `.glb` (mesh+skeleton+anim) at runtime; **decision point** on the skeletal path.
  Upstream is actively maintained (Jan 2026 release supports UE 5.7; 5.8 not yet listed —
  the maintainer historically follows new engine versions within weeks) and runtime skeletal
  mesh + animation loading is a core documented feature. *Deps:* none.
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
| PL1 | Export `.ents`-referenced models (`prop_dynamic`/`prop_physics`) | 8.1 |
| PL2 | Copy loose `.py` → `out/scripts/`, `.dlg` → `out/dlg/` | 5.1 |
| PL3 | Use-icon atlas export (72-entry enum) | 4.4 |
| PL4 | Batch NPC export + include-model resolution in `mdl_skel.py` | 8.5 |
| PL5 | Copy sound schemes (a) + `vdata/system/*.txt` (b) | 6.3, 9.4 |
| PL6 | Texlight merge in exporter | 3.4 |
| PL7 | Sidecar space fixes surfaced by the audit | 0.4 |

## RE backlog (reverse-engineering work; each cited where consumed)

| ID | Question | Consumed by | Status |
|---|---|---|---|
| RE1 | Trigger/button spawnflag filter bits (un-decompiled; don't assume stock Source) | 4.2, 4.5 | [~] |
| RE2 | Retail queue-vs-think service order (our queue-first is a recorded choice) | revisit if a chain misbehaves | [P] |
| RE3 | `__setattr__` write path + error-to-false **confirmed**; `G` default-0 pending | 5.2, 9.1 | [~] |
| RE4 | Ghidra datamap JSON export (validate our input/field tables vs retail) | 4.5+ (optional, valuable) | [P] |
| RE5 | Dice-system vroll golden test | 9.6 | [ ] |
| RE6 | `ent_survey` count reconciliation | 0.6 | [ ] |
| RE7 | Retail `.sav` block wire format | 10.7 (only for importing retail saves) | [P] |

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
- **`G` default-0 — still pending.** Seeds: G console cmds `FUN_1019aad0`(`gclearall`) /
  `FUN_1019ab40`(`CC_GClearAll`, string `"G.ClearAll()"` @`0x10591ab0`) reach the G global object →
  decompile its type's `tp_getattr` and confirm the miss path returns `PyInt_FromLong(0)`. (Hard-
  coded story pokes like `"__main__.G.Story_State=666"` @`0x105b4f08` run through `FUN_100ce8a0`.)

**RE1 — partial; spawnflag map still open.** Recovered the `func_button` object layout + vftable
(`0x1045293c`) and the use-icon/locked logic above, but the observed spawnflags (33=`0x21`,
256=`0x100`, 1024=`0x400`, 1056=`0x420`, 1057=`0x421`, 8193=`0x2001`, 9217=`0x2401`) are tested in
the **Use/Touch/Spawn handlers**, not the getters. Next seeds: xref the button trace strings
`CBaseButtonButtonUse`@`0x1055c98c`, `CBaseButtonTriggerAndWait`@`0x1055c9e4`,
`CBaseButtonButtonTouch`@`0x1055ca20` → decompile each → read the `m_spawnflags & <bit>` tests;
repeat for `trigger_multiple` (classname string, then its `Spawn`/`PassesTriggerFilters`). Deliver
a bit→behavior table into `entity_io.md` (which currently flags these bits as unsettled).

**RE4 — datamap JSON export (planned; needs a new script).** Write `tools/ghidra/DumpDatamap.java`:
for each `CBaseEntity`-derived vftable, resolve `GetDataDescMap` (slot `+0x148`) to the static
`datamap_t*`, walk `baseMap` (@`0xC`) up the class chain, and for each 44-byte record emit
`{fieldType@0, flags@0x12, fieldOffset@8, externalName@0x14→str, inputFunc@0x1C→thunk-target}` as
JSON per classname. The runtime walker `FUN_10195940` already proves the exact record layout and
chain traversal to mirror. Output validates the P1 class registry's input/field tables (1.3)
against retail — de-risks all of P1; run it before 4.5.

**RE2 / RE5 (Ghidra-drivable, still parked/open):** RE2 — decompile the `CEventQueue` service +
entity think dispatch to confirm the recorded queue-first order (prior dumps `q_think_asm`,
`x_prethink`); unpark only if a chain misbehaves. RE5 — optionally static-recover the vroll
resolver in `vampire.dll` to cross-check `recovered/dice-system.md` alongside the running-game
golden test.

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
| Cog fails to build on 5.8 | debug layer slips | 0.5 spike before P2 commits (risk lowered: upstream active, UE 5.5+ supported, no version-compat issues filed); fallback = VesCodes/ImGui + hand-rolled windows |
| Level scripts exceed the mini-interpreter subset | scripting rework | 5.5 survey + decision before building; CPython embed is the acknowledged fallback |
| Chaos kinematic movers push/block poorly | doors feel wrong | 4.1 prototypes one door first |
| Floor perf unproven (no 3060 on hand) | late surprise | look-gates until 10.3; lump-8 bake parked as contingency; Lumen Lite noted as a cheaper option (see Options) |
| Tutorial-only calibration bias | rework on other maps | 0.3 second map early; all calibration provisional until 10.1 |
| Sidecar space drift (Godot-era leftovers) | subtle geometry/logic bugs | 0.4 audit before any new consumer |
| Save determinism erodes | broken saves late | standing rule since P1: no engine timers, own serializable structs |
| Legal posture | project-ending | bring-your-own-game holds; nothing game-sourced committed — standing constraint on every task |

## Decision log (append-only)

- **2026-07** — Cog (MIT, vendored, dev-only) adopted as the debug UI shell after tooling
  research; Slate console superseded. (`debug-tooling.md`)
- **2026-07** — Entity object model adopted: plain-C++ entities with optional Unreal bodies;
  one name table per class (I/O + Python + keyvalues + saves + inspector); generation-checked
  handles on stable `.ents` indices; one clock + one queue, no `FTimerManager`; two
  instrumented chokepoints; dormancy as one switch. (`engine-core.md`)
- **2026-07** — Queue-serviced-before-thinks tick order chosen (retail order unknown, RE2).
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

## Appendix — profiling baseline (0.1 fills this in)

| Pass | ms @1080p (dev GPU) | Notes |
|---|---|---|
| Lumen GI | — | |
| Lumen reflections | — | |
| MegaLights | — | |
| ShadowDepths / VSM | — | |
| Total GPU | — | |
