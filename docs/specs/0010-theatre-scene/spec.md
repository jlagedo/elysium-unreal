# 0010 theatre-scene — the sheriff-versus-Sabbat choreography in the theatre plays clean: scene timeline, gestures, face, eyes, lipsync

## Witness
The `sp_theatre` choreographed scene (the sheriff / Sabbat execution) plays start to finish with
correct actor motion, facial flex, eyes and lipsync, then the following Jack scene starts. Played
against retail's execution oracle: a normal/male run starting 10 of `sp_theatre`'s 12 scene
entities and dispatching 113 VCD events, a complementary normal/homo and male/female run covering
the other two; both `sp_tutorial_1`'s alley-fight scenes reaching `OnCompletion`. The scene
plays today; this spec is its faithfulness and polish, after the tutorial is beatable.

## Scope
Choreographed scenes (`.vcd` reader, timeline, event handlers, cast staging, claims, the camera
track), gestures, paired actions, facial flex, eyes and eyelids, the scene half of lipsync.
Owned elsewhere and consumed here: the `scripted_sequence` beats the walk-out and the escort run
on, and the beat-versus-scene claim — **0003**; scene line audio, subtitles and the mixahead
lead — **0011**; the camera itself and the owner call on cutscene composition (legacy shot stack
vs the `Sequence` request kind) — **0001** / **0012**; the weapon overlay layers (LIFE10) —
**0015**; the montage mechanism (LIFE5) — **0005**.

## Sources
- Oracle: `docs/vtmb/choreographed_scenes.md`, `docs/vtmb/facial_animation.md` (RE34),
  `docs/vtmb/animation_and_movers.md` A.3 / A.4c, `docs/vtmb/animation_rig_resolution.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `scenes/` (`sp_theatre`'s
  twelve, `prince_beckett_dialog`), `maps/sp_theatre.entities.glb`, `expression-tables/`,
  the facial sidecar (`mouths`), `lip/`.

## Witness data
- `ElysiumSceneData` (the `.vcd` reader) and `ElysiumScenePlayer` (the clock) are unchanged
  since the staging regressed under CCC2 and LIFE0–4; every symbol `ElysiumChoreoScene` calls
  still resolves — the contract around them broke, not the scene code.
- Body publish: every body publishes a selection every tick, and a locomotion publish on the
  base channel calls `StopClip()` on the pinned cinematic player. Only the Scene-band claim inside
  `PlayCinematicClip` holds the pose; a body routed to no driver holds no claim.
- Retail re-resolves a scene's actors by name every frame; the port's `BindActors` resolves
  once per `Start` (ruled out for the initial-cast race by the retail oracle; still needed for a
  later rename or replacement).
- Retail enters scene cancellation when a playing scene is killed (`scene_over_relay` kills
  `courtroom_scene_bip2` at 151.14 while its last line runs to 155.81); the port's `Kill` now
  routes through cancellation (landed 2026-09-12, `871e93c0`).
- Confirmed defects: `PlayCinematicClip`'s fallback calls `USkeletalMeshComponent::PlayAnimation`
  on a body with no `UElysiumBipedAnimInstance`, destroying its anim graph for the map
  (`PlayNpcClip` guards this); `ReleaseActorClips` early-returns under `elysium.SceneActors 0`
  (leaks the claim); `ApplyPositionEnd` carries the same guard (skips `position_end`); a body
  destroyed without a stop leaves its `CinematicClaims` entry unswept; `ReleaseActorClips` stops
  a clip only while `DrivesActor` holds; placement writes origin and angles separately, two
  motor teleports with stale angles on the first (`SetRuntimeTransform` exists).
- The scene stamps entity-level `ScriptOwner` but never claims the arbiter's `Sequence` owner;
  `SetBodyFrozen` is a no-op on the player chain, so a scene binding `!player` with
  `position_start 1` teleports the pawn and does not hold it. `PlayCinematicClip` sets visibility
  unconditionally, bypassing the `npc_VPlayerController` stand-in's rule. The NPC-maker interlock,
  the stand-in flow and map-epoch handle semantics moved after the scene code was last touched
  (`RebaseSavedHandle`).
- Gesture: an overlay in the same four-slot `CBaseAnimatingOverlay` array as an `ACT_*_LAYER_*`
  selection, composed at a flat retail `0.1` weight (`animation_and_movers.md` A.4c / A.3),
  rate-scaled at start, then free-running, auto-killed at its end; the autolayer's `1.0` is
  already matched. No banked witness for the gesture value (all five capture databases are
  `sp_theatre`); the only shipped file composing a gesture over a running sequence is
  `prince_beckett_dialog` on `la_ventruetower_1` (`prince2` at `17.493→30.827`, `37.213→47.407`).
- Paired actions (owner call, made): two bodies posed against each other — the cinematic path's
  problem, not the reaction channel's; owed: the role/size/side variant arithmetic over LIFE2's
  catalog and a claim holding both base channels for one transaction; the feed transaction is the
  shipped consumer, unwitnessed by either acceptance map.
- `Prince_Escort_Male`: the characterization report's baseline lerp differs from retail's
  `FUN_100889f0` / `FUN_100892c0` → `FUN_1010a0b0` hemisphere-corrected normalized lerp; on the
  retail rule the over-band count for `sm_hub_1` drops from 693/7,198 to 18 (translation only)
  and `move_and_ranged`'s 1,318 to 0; the only actor-discriminating signal is the `bone_name`
  join failing 3,599/3,599 on Lacroix vs 4/3,599 on brujah with a 72-vs-78 bone-count asymmetry.
- Facial flex: `FElysiumFacialRig` (controllers → RPN rules → flexdescs → ramps → morph
  weights) proven headless on `nines`, 19 rigged bodies live. Eyes: `StudioEyeball` records,
  `M_Eyes` with both UV planes and the `$vampire` lerp, the per-frame eye basis, lid write-back,
  the gaze cascade/cone/saccade/integrator at recovered offsets, the four `LookAtEntity*` inputs;
  live blink 7.25 % duty against 7.06 % predicted (RE34). Three gaze arms (enemy, navigation
  goal, heard sound) fall through to the autonomous scan until their producers exist — retail's
  own fallback. Lipsync: `.lip` timing, `expressions/<model stem>_phonemes.txt` weights (249
  tables keyed by actor model basename, keyed on the phoneme *string*), `mstudiomouth_t` for the
  jaw; the dialogue half landed (`BeginDialogueLipsync`), the scene half reads 0011's line clock.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. Choreographed scenes** (was 12.1): reader, timeline, nine event handlers, the
  camera-track path, the PC body stand-in, per-actor cinematic bank binding. Oracle:
  `choreographed_scenes.md`.
- [x] **2. A killed scene cancels** (landed 2026-09-12): `Kill` → cancellation cleanup; the
  finish walk restores its own cast (`bHeldByAnotherScript`). Oracle: `choreographed_scenes.md`.
- [x] **3. Facial flex** (was 12.3) and **eyes** (was 12.4): the rig and the gaze layer, RE34.
  Oracle: `facial_animation.md`.
- [ ] **4. The funnels made loud.**
  Job: the bank-miss report; `SeekCinematicClip`'s no-seek report; the unresolved-actor
  diagnostic; the `PlayAnimation` single-node fallback guarded as `PlayNpcClip` guards it.
  Size: S. Effort: Sonnet / medium.
- [ ] **5. The claim lifecycle.**
  Job: the `elysium.SceneActors 0` early returns in `ReleaseActorClips` and `ApplyPositionEnd`
  removed; a `CinematicClaims` sweep on body destruction; `ReleaseActorClips` stopping the clip
  it claimed regardless of `DrivesActor`.
  Size: S. Effort: Sonnet / high.
- [ ] **6. The scene's cast as the arbiter's `Sequence` owner.**
  Retail: a beat possessing an actor a scene holds — precedence UNRECOVERED (the 0003/0010
  seam); the scene's own hold on the player is `position_start`.
  Job: the scene claims through the same possess-shaped claim 0003/3 defines, so the mind reads
  `Sequence` for a scene's cast; `SetBodyFrozen` real on the player chain; the precedence
  recovered before either side is chosen.
  Consumes: 0003/3.
  Oracle: `choreographed_scenes.md` § "Claims" (new); entity_io § "Scripted sequences".
  Size: M. Effort: Opus / high; corpus pass on the precedence first.
- [ ] **7. Actor re-binding and the transform write.**
  Retail: per-frame re-resolution by name.
  Job: re-resolution restored (or a re-bind hook at rename/replacement); the origin/angle
  double write collapsed to `SetRuntimeTransform`; `RebaseSavedHandle` re-read against the
  epoch boundary; player-body visibility through the stand-in's rule.
  Oracle: `choreographed_scenes.md`.
  Size: M. Effort: Sonnet / high.
- [ ] **8. Gesture and sequence un-collapsed.**
  Retail: the overlay at flat `0.1`, rate-scaled at start, free-running, auto-killed.
  Job: the gesture as an overlay in the four-slot array, proven on `prince_beckett_dialog`.
  Consumes: 0015's four-slot substrate.
  Oracle: `animation_and_movers.md` A.4c.
  Size: M. Effort: Opus / high.
- [ ] **9. Paired actions.**
  Job: the role/size/side variant arithmetic over the catalog; the dual-participant claim shape.
  Consumes: 0005's reaction and death pose machinery.
  Oracle: `animation_and_movers.md` § "Paired actions".
  Size: M. Effort: Opus / high.
- [ ] **10. The `Prince_Escort_Male` residual.**
  Job: an offline bone-name diff from the existing capture database; no new capture or bake.
  Oracle: `animation_rig_resolution.md`.
  Size: S. Effort: Sonnet / medium.
- [ ] **11. Facial flex, seen.**
  Job: the green-room harness's empty `-GreenRoomAnimSet=` / `-GreenRoomBoneRoot=` arguments
  fixed so one body can be isolated for the close-up.
  Size: XS. Effort: Sonnet / low.
- [ ] **12. The eyes' debug surface.**
  Job: `elysium.npc.gaze` / `blink` verbs and a Cog Eyes tab reporting basis, planes, blink
  phase and lid weights; `Prince1.LookAtEntityEye` aiming LaCroix at the player in the scene.
  Oracle: `facial_animation.md`.
  Size: S. Effort: Sonnet / medium.
- [ ] **13. Lipsync, the scene half** (was 12.5).
  Retail: the three-file join per line against the scheduled line clock.
  Job: `.vcd` `speak` lines through the same join the dialogue half uses, on 0011's clock.
  Consumes: 0011/5.
  Oracle: `facial_animation.md` § "Lipsync".
  Size: S. Effort: Sonnet / high.

## Seams
- Provides: the restaged scene path (placement, claims, camera coupling, triggers) that 0003's
  walk-out and 0011's line service play against; the montage-slot run and its claim to 0003.
- Consumes: 0003/3's claim shape (6); 0005's pose machinery (9); 0011's line clock (13); 0015's
  overlay substrate (8); 0012's decision on cutscene composition.
- Open recoveries: the beat-versus-scene precedence (6); whether the blend-profile bake carries
  the per-bone weight list the overlay mask needs (8, with 0015); whether `m_Flinch`'s three-slot
  stack is the existing reaction stream (with 0015).
