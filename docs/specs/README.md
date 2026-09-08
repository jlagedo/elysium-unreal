# Specs

One folder per witness: `NNNN-<name>/spec.md`. Status lives inside the spec itself, not here.
A spec's folder is deleted when it lands — nothing else records that it landed. `ls docs/specs`
is the index; this file is only a readable copy of that listing plus the parked backlog.

## Open

- 0000-play-tier — the beat-script driver that plays every other spec's witness from real input
- 0001-scripted-camera — VtMB's scripted-camera subsystem: camera_cinematic, the override channel and its five witnesses
- 0002-theatre-audio — the intro cutscene plays with every sound starting, stopping and clearing as retail does
- 0003-theatre-fight — the sheriff-versus-Sabbat choreography in the theatre plays clean: scene timeline, gestures, face, eyes, lipsync
- 0004-jack-arrival — the Jack cutscene ends with Jack grounded and the first conversation runs on the conversation UI
- 0005-park-stealth — cross the tutorial park past the patrolling enemy unseen and reach Jack inside the building
- 0006-first-kill — Jack sends the player back to kill the park patrol: melee combat, the enemy transaction, death
- 0007-first-disciplines — downstairs in the tutorial the player activates disciplines for the first time
- 0008-tuna-distraction — grab and throw tuna cans to draw the guard off the door: the physics hands and the physics world the tutorial touches
- 0009-ranged-bottles — pick up the gun and shoot the beer bottles: firearms, the first-person viewmodel, pickup, breakables
- 0010-elevator-final — the elevator, the final fight, the last Jack conversation and the transition to sm_hub_1

## Parked

### Roadmap — lighting & world (P3/P7)

- 3.1 Pin MegaLights per-light — lighting lane frozen; per-light MegaLights pinning open
- 3.2 Shadow curation — frozen; shadow curation pass open
- 3.6 Pinned exposure — frozen; exposure pinning open
- 3.7 Grade/tonemapper fidelity — frozen; grade/tonemap match to retail open
- 7.1 Coronas — dressing/parity frozen; corona light-flare rendering open
- 7.5 Real reflections — frozen; reflection fidelity vs retail, `docs/vtmb/reflections.md`
- 10.1 Horizontal scale-out — post-thaw; scale beyond the proven three maps
- 10.2 Perf deepening — post-thaw performance pass
- 10.9 Asset enhancement — post-thaw asset-quality pass

### Roadmap — interaction & UI (P4/P8/P9/P10)

- 4.10 game_sign / prop_sign — both leaves landed; NewspaperData, ClientCommand, fade/pause details open
- 8.4a prop_dynamic divergences — animation half done; `solid` and `disableshadows` unread
- 8.6 UI foundation — landed; device-navigation acceptance and the New Game click path open
- 8.8 Sign / popup panels — screen landed; format features open
- 8.10 Accessibility & options backing — not started
- 9.10 Economy — not started
- 10.4 Async travel state machine — trigger: when hitches matter
- 10.5 Packaged-build content path — not started
- 10.7 Long tail — door-obstruction producers, follower/return-to-initial, vdata-driven systems; promote per item when reached

### Roadmap — audio programme (AUD)

- AUD5 The mix — classes, submixes, buses, user sliders, ducking, concurrency, occlusion
- AUD6 Ambience, schemes and music — deterministic transitions, random-emitter scheduler, the six events_world music states
- AUD7 The listener zone — one resolver over trigger_environmental_audio, scheme RoomDSP and scripted overrides
- AUD8 Heard — played acceptance — the programme's owner-played finish line
- AUD9 Enhancement — spatialization, zone reverb, loudness, adaptive music, accessibility; behind the graphics freeze

### Roadmap — animation & controls (LIFE/CCC)

- LIFE8 Alive — played acceptance — the programme's owner-played finish line; owns the capture-tooling trim
- LIFE9 Secondary-motion calibration — hair/cloth fitting and numeric replays; behind the graphics freeze
- CCC0 The instrument — gym + channels landed; three sited feature courses open
- CCC3 Controls response — curves and leniency landed; the co-tune open
- CCC8 Played acceptance — the 3 C's slice finish line, owner-played

### Roadmap — pipeline

- R8 (remaining) Characters on the GLB corpus — owner-piloted rendered play pass on sp_tutorial_1 / sm_pawnshop_1 / sm_hub_1; everything else in R8 closed 2026-09-06
- PL6 Texlight merge in exporter

### Animation & character dynamics

- Priority-table ranking beyond ambient-vs-locomotion and melee-vs-scripted-tie is unverified project capture, not yet labelled retail behaviour
- Hair-dynamics `AnimDynamics` node has no ground/spherical collision — head/shoulder intersection unaddressed
- Breast jiggle recipe unbuilt — AnimDynamics single-body vs Spring Controller vs RigidBody+PhysicsAsset vs Kawaii Physics undecided
- Cone clamp value for the loose-90° hair group, and whether it needs a firmer spring, undecided
- Whether player-armour sag ships in the first breast slice undecided
- Whether one-bone hair-curl records ship with the same single-body primitive as breasts, or wait, undecided
- Whether a still (unsimulated) chest on a modernized close-up is acceptable if the clamped-cone look is rejected, undecided
- No breast selection rule, corpus allow-list, or conversation-cast rollout slice implemented or scheduled
- Live own-clip motion under a wearer (lockpick wiggle, handleclaws) needs a runtime anim graph blend against a copy-pose branch — not built
- Static-mesh optimization for the clip-constant majority of melee wield models — future bake-side change, not implemented
- Exact retail emission site for the PLAYER_FOOTSTEP_SNEAK/WALK/RUN hearing stimulus still unrecovered
- `CGameMovement::ShouldPlayStepSound` (+0x3c) recovered but deliberately not ported, unlisted elsewhere
- Unnamed player animation-state enum (values 8/10/11) the footstep hearing producer depends on is unrecovered
- Terminal-session (monitor/keypad/hacking) muting of NPC footsteps deferred pending 13.4's interaction-session state
- `event_frame` for the leniency-bracket gym lanes deferred (moves with speed); only `jumps_taken` recorded
- Runtime prop's `SetModel` should derive its stem via `PropModelStem`, not basename (same defect already fixed item-side)
- Authored MDL animation events need to carry through the character bake so `OnFeedAnimEvent`/weapon contact-commit bind real notifies
- Seductive/rat/zombie feeding modes and presentation unbuilt, only marked as seams

### Camera & input

- Weapon-class camera arbitration (player+0x2440 forced-third/forced-first/feed latches) unbuilt, waiting on bitmask RE
- Input layer has no exposure of the weapon-class bitmask needed by `LT`'s melee/ranged split and `camera_prefs` arbitration
- Combat aim assist (`sv_aim`) off by default and unimplemented; no gamepad ranged-aim baseline owner call
- Retail `client.dll` `CInput` mouse arithmetic beyond the ConVar surface unrecovered; `m_customaccel*` never scanned for

### UI & save

- Character screen's Info tab still a framed placeholder with no content
- Save thumbnail capture for the load menu deferred pending the load-screen UI
- Decal records (`FElysiumDecalRecord`/`DECALLIST`) not wired into the save `Maps` block
- `G.morgue` has no runtime writer or reader; nothing models the story's death record
- Snapshot size to be re-measured at the first ten-map run

### Audio

- Exact VtMB precedence between brush `room_type`, scheme `RoomDSP` and networked player room fields is an open RE gate
- Whether VtMB's `OneOfSet` counter is a per-call RNG or a frame counter is unresolved

### Effects & runtime data

- Precipitation gate (particle should die when the BSP leaf lacks the sky-visible bit) staged as a flag, no implementation, no roadmap row
- `runtime-data-compilation.md`'s compiled binary runtime pack for `.ents` plus partitioned generated `UDataAsset`s for character metadata is a contract proposal, no roadmap row
- `SpawnParticleRoot` for the impact table/animation-event bus, keyed off Niagara Data Channels, is described but not built

### Pipeline / bake follow-ups

- Props LOD policy undecided (VTX LOD chains up to 7 deep, `switchPoints`, per-placement fade distances)
- Bodygroups have no seam vocabulary (14 multi-submodel models; static props bake submodel 0)
- No `TEXCOORD_1` in the mesh corpus — Lumen-only vs generated lightmap UVs undecided
- Full 3,661-unit props corpus run beyond the 414-model test corpus, pending separate approval
- Green room parses `sp_theatre.ents` directly; must move to the entity asset before the `.ents` readers die
- `Elysium.Substrate.LightRig` needs re-home to bake verification; the seven "Enhanced rain" literals need a settings home
- Seven taste literals behind cvars (MenuScrim, JawSpeechLevel, JawSmoothing, CameraCutSeconds, MusicCrossfade, SchemeRandomBase, LoadingScreenMinTime) owe settings pages
- Validator bugs: `vtmb:missing-material:` sentinel invisible to closure checks; index keeps only the first reason per label
- 108-map `map_sidecar_diff` producer-parity run (R3.1–R3.5) not run; `--legacy-root` comparison owed; `la_hub_1` short-circuit needs narrowing
- Delete `.ents`/`.hulls`/`.dispcol`/`.env`/`.sky`/`.spawn` sidecar readers and `UE_bsp_to_scene.py` (R9)
- R2.1 shot baseline re-save for `sp_tutorial_1` (stale/blurred)
- `MapEnvironmentParityTests` hardcodes its resolver assertion to `"asset"`; `MapCollision.*` has no resolver assertion
- `NANITE_CAPABLE_MASTERS` vs `make_v2_materials.py` `nanite=True` is hand-maintained, no shared constant/test
- `bake_verify.py`'s legacy `.mtl` glass/prop alpha checks fail every converted map, need a V2-aware rewrite
- Cog Lights viewer / `ElysiumLightProbe` read a baked light's `Mag`, which the bake consumes to 0
- `light_dynamic` brightness convention has no validating map on the working corpus
- Plain sprites ride the corona occlusion query; `$spriteorigin` unapplied; `MI_DetailSway_*`/`MI_Sprite_*` children never pruned
- Dithered alpha ramp over the `cl_detailfade` band not reproduced, only a hard cutoff
- `ui/strings.json` and `signs/*.txt` remain loose reads; six legacy world materials kept alive until R9.2
- `make_root_systems.py` owed for the four water FX roots, ten masters regenerated (`GRAPH_VERSION` 11), maps re-staged/re-baked
- `water.Scrape` has no caller; U7/U8 witnesses still needed
- 36 unmatched `infodecal` entities need a wider plane-distance search; `sp_soc_4` hand-placed decal; `r_decals` default unread
- R7.5 `func_areaportalwindow` owner call (open-window vs re-mesh backing brush)
- R7.6 detail-sway amplitude owner call (Source default vs tuned vs true-VtMB 0)
- R7.7 depth-tested plain sprites owner call (`M_V2_SpriteZ` twin vs whole-card fade)
- R7.8 menu-seal source-art owner call (clan-symbol stand-in vs `particles/*.tga` as a texture source)
- Sub-4x4 cubemap block rotation orientation is wrong (round-trip still byte-exact) — fix exporter or document the bound
- "What happens to resources that can't become Unreal assets" (scripts, `.dlg`, `cfg/`, `vdata/`) undecided resource by resource
- Sky follow-ups beyond the testable migration (HDR/face/mip/build comparisons, per-map baselines, GPU radiance equivalence)
- `scripts/**` stays on the legacy tree/reader; the dialogue-corpus "legacy reads that remain" list has no migration row
- R7.3 effects backlog: 8 of 12 Niagara archetypes unauthored (A5, A8, A2, A3, A6, A9, A10, A7); generator modules, map-bake wiring, contact sheet, the explosion bundle
- R7.4 runtime material binds entirely open: `textconsole` render-target screens, `breakablesurface` + `$crackmaterial` shatter, `shadow`, `playerproximity`/`playerposition`/`playerspeed`, `lessorequal` chains

### Characters & effects backlog

- LOD import for skinned/character meshes deferred past R8 (517 units >1 LOD, 146 a shadow row; morphs LOD0 only)
- Armor-equip to body-swap wire is a pre-existing gap, filed separately, no roadmap row
- Cinematic per-actor skeleton slices ("four skeletons from one unit") left as a later owner call (OC5)
- Bank-remap `@host` fan-out modernization (compose the additive overlay in the AnimGraph layered blend instead of baking it) has no roadmap row

### NPC AI (npc-ai.md — no spec owns these)

- TaskFail + TASK_SET_ACTIVITY-miss — schedule-kernel failure gaps, not owned by any of the ten witnessed specs
- Troika reduced-think mode (the VM clock cadence) — feeds followers' tests but is not itself named in any spec
- Followers (`m_hFollowerBoss`) — no follower/companion spec exists among the ten
- Squads (`CAI_Squad`) — no squad spec exists among the ten

### Animation programme infra/rules/closure (animation-critical-path.md — no spec owns these)

- Programme infrastructure: the retail frida capture corpus, `.eskm`/`.json` export format, automation-test inventory, headless compose harness, offline scratch tooling, vtmb-corpus RE notes — referenced by tasks, not spec-owned
- Rules of engagement and the T1–T26 traps postmortem — cross-cutting procedural rules, no roadmap row
- Generic anim-driver rules unmatched to a spec: T-C1 events, T-C5 slot timelines, T-C4 envelope
- Phase D closure: T-D1 owner-piloted played acceptance and T-D2 docs/register cleanup for the whole animation programme

### Doc debt (seam doc corrections owed)

- block-rotation claim needs the sub-4x4 correction; menu-seal owner call may add `particles/*.tga` as a source kind
- false claims to correct: no header KeyValues `prop_data` region; static units do carry one reference-pose animation
- the `decalmodulate` → translucent-under-DBuffer divergence
- the boxed-no-`.phy`/both-flags fade rulings in sync with R6.4/R7.5 if the areaportalwindow ruling changes brush counts
- vdata seam doc (owning topic unspecified) → promote the hunter-mode variant-swap divergence note once the vdata migration lands
- R8's §10 doc-debt batch — already tracked as R8's own closure list

## Deferred by rule

Graphics-frozen by the playable-path rule; not resequenced until the presentation thaw:

- 3.1, 3.2, 3.6, 3.7 (lighting lane, P3)
- 7.1, 7.5 (dressing & parity open tasks, P7)
- 10.1, 10.2, 10.9 (scale/perf/asset enhancement, P10)
- LIFE9 (secondary-motion calibration)
