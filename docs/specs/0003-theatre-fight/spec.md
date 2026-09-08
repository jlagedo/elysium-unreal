# 0003 theatre-fight — the sheriff-versus-Sabbat choreography in the theatre plays clean: scene timeline, gestures, face, eyes, lipsync

## Witness
The `sp_theatre` choreographed scene (the sheriff / Sabbat execution) plays start to finish with
correct actor motion, facial flex, eyes and lipsync, then the following Jack scene starts. Proven
by the retail execution oracle's played-scenario shape: a normal/male run starting 10 of `sp_theatre`'s
12 scene entities and dispatching 113 VCD events, a complementary normal/homo and male/female run
covering the other two; both `sp_tutorial_1`'s alley-fight scenes reaching `OnCompletion`. No dev
shortcut is acceptance evidence. Owner-reported defects driving this spec: visible bugs in the
first cutscene, possibly NPC-AI or movement related — the choreo-scene rewire below is the
diagnosis and the fix.

## Scope
- Roadmap rows absorbed: 12.1 Choreographed scenes (remaining: staging/acceptance),
  12.3 Facial flex track (remaining: unobstructed visual), 12.4 Eyes and eyelids (remaining: debug
  surface + theatre acceptance), 12.5 Lipsync (remaining: theatre/scene half + acceptance),
  LIFE7 The cinematic path and gestures (the choreo-scene rewire hard slice, the gesture
  un-collapse, paired actions), LIFE10 The weapon layer composition gap / animation overlay
  subsystem (absorbed per assignment; not a witness dependency of the theatre scene — see note in
  Requirements).
- Out of scope (belongs to another spec or is parked): scene line audio, subtitles and the
  mixahead lead (AUD3, spec 0002); the camera itself is landed (SC1–SC9,
  RG-A–RG-E, spec 0001) and not re-specified here — only the *coupling* of the legacy shot stack
  vs. `UElysiumCameraService`'s reserved `Sequence` kind for cutscenes is this spec's open owner
  call; LIFE5's montage mechanism (dependency, specified elsewhere); LIFE8 played acceptance and
  LIFE9 secondary-motion calibration (parked, other specs).

## Requirements
1. **Staging must be re-established before any montage migration.** Actor placement at marks, the
   scene camera and the triggers that start scenes regressed under CCC2 and LIFE0–4 and have not
   been exercised since. `ElysiumSceneData` (the `.vcd` reader) and `ElysiumScenePlayer` (the
   clock) are unchanged and every symbol `ElysiumChoreoScene` calls still resolves — the contract
   around them broke, not the scene code.
2. **Body-publish contract**: every body now publishes a selection every tick, and a locomotion
   publish on the base channel calls `StopClip()` on the pinned cinematic player. Only the
   Scene-band claim inside `PlayCinematicClip` holds the pose; a body routed to no driver
   (`SubmitBodyAnimRequest` answers 0 off an `AElysiumNpcBody` motor or the player visual) holds no
   claim at all — the green-room "proven ground" uses exactly such bare components, so it is not
   the real path.
3. **Player-body visibility** must go through the `npc_VPlayerController` stand-in for a cutscene;
   `PlayCinematicClip` currently calls `SetVisibility(true, true)` unconditionally, bypassing the
   rewritten rule.
4. **Cast-creation timing**: the NPC-maker interlock, controller stand-in flow and map-epoch handle
   semantics moved after the scene code was last touched; `RebaseSavedHandle` needs revisiting
   against the new epoch boundary.
5. **Silent funnels to make loud first** (each independently reproduces the reported symptoms with
   nothing logged): `BindActors` resolves by exact name once per `Start` while retail re-resolves
   every frame (ruled out for the initial-cast race on the normal/male path by the retail oracle;
   still needed for a later rename/replacement); a cinematic bank miss is warned but
   `SeekCinematicClip` can report success without proving it is seeking the fallback clip; a
   partial `export characters` corpus leaves named cast bodiless (current corpus is not partial:
   15 anim sets / 48 exact roots); `elysium.SceneActors 0` and `elysium.NpcBodies 0` reproduce the
   whole symptom.
6. **Confirmed defects to fix:**
   - The authored `scene_over_relay` kills `courtroom_scene_bip2` at scene time 151.14 while its
     final line runs to 155.814529; retail enters scene cancellation, Elysium's base `Kill()` only
     makes the scene inert. The cleanup lives only in `InputCancel`, which can strand Prince1
     frozen or scene-owned immediately before the walk-out and escort.
   - `PlayCinematicClip`'s fallback calls `USkeletalMeshComponent::PlayAnimation` for a body with
     no `UElysiumBipedAnimInstance`, switching the component to single-node mode and destroying its
     anim graph for the rest of the map. `PlayNpcClip` already guards this; the cinematic path does
     not.
   - Three claim-lifecycle leaks: `ReleaseActorClips` early-returns when `elysium.SceneActors` is
     0 (leaks the claim, permanently parks the base channel on toggle); `ApplyPositionEnd` carries
     the identical guard (skips `position_end`, strands a restore-scene's cast at the mark); a body
     destroyed without a stop leaves its inert `CinematicClaims` entry unswept; `ReleaseActorClips`
     stops a clip only while `DrivesActor` holds, so a `scripted_sequence` stealing an actor
     mid-scene leaves the Scene claim standing.
   - Placement writes origin and angles separately, firing two motor teleports with the first
     carrying stale angles — `SetRuntimeTransform` exists to avoid this and should be used.
7. **Semantic drift to reconcile:** the scene stamps entity-level `ScriptOwner` but never calls
   `ClaimScriptBody`, so the mind's body-owner token never reads `Sequence` for a scene's cast.
   `SetBodyFrozen` is a no-op on the player chain, so a scene binding `!player` with
   `position_start 1` teleports the pawn and then does not hold it.
8. **Missing wiring:** the camera has two composition systems on one component — the legacy shot
   stack cutscenes use and win the frame with, and `UElysiumCameraService`'s `Sequence` request
   kind, reserved for cutscenes and unproduced; they arbitrate only by layering accident. Nothing
   pushes `ElysiumInput::Priority::Cinematic`, so live look input integrates through a shot and the
   base rig snaps to it when the shot's weight ramps out.
9. **Test blindness to close:** the scene suite's actor is a bodiless `logic_relay` with no
   positive assertion on play/seek/freeze/angles/camera/arbitration; the theatre camera test counts
   stub pushes rather than a live manager frame. `ScriptedSequenceBodyClaim` is the shape the scene
   path needs and lacks.
10. **Gesture/sequence un-collapse.** A gesture is an overlay in the same four-slot
    `CBaseAnimatingOverlay` array as an `ACT_*_LAYER_*` selection, composed at a flat, retail
    constant `0.1` weight (`docs/vtmb/animation_and_movers.md` A.4c / §A.3), rate-scaled at start,
    then free-running, auto-killed at its end. A second scene-time-pinned player would reproduce
    timing retail does not have. The autolayer's `1.0` constant is already matched — no stand-in
    owed there. No banked witness exists for the gesture value (all five capture databases are
    `sp_theatre`); the only shipped file composing a gesture over a running sequence is
    `prince_beckett_dialog` on `la_ventruetower_1` (`prince2` at `17.493→30.827` and
    `37.213→47.407`), which is outside both `sp_theatre` acceptance runs and is where the
    un-collapse must be proven or is not proven.
11. **Paired actions** (owner call, made): a paired action is two bodies posed against each other —
    the cinematic path's problem, not the reaction channel's. Owed: the recovered role/size/side
    variant arithmetic (policy over LIFE2's catalog, no new graph machinery) and a claim shape
    holding both participants' base channels for one transaction. The feed transaction is the
    shipped consumer; no scene on either acceptance map reaches it, so it is unwitnessed by this
    spec's acceptance runs.
12. **`Prince_Escort_Male` cluster** — mostly attributed, residue is a measuring artefact, not a
    bake one. The characterization report's baseline lerp differs from retail's own
    `FUN_100889f0`/`FUN_100892c0` → `FUN_1010a0b0` hemisphere-corrected normalized component lerp;
    on the retail rule the over-band count for `sm_hub_1` drops from 693/7,198 to 18 (all
    translation, rotation column exactly zero), and `move_and_ranged`'s 1,318 drops to 0 with both
    the frame and cell rule applied. Decode, bake and remap are ruled out directly (zero route
    disagreements, bind error ~2.4e-5, no bone over band); the only actor-discriminating signal is
    the `bone_name` join failing 3,599/3,599 on Lacroix vs. 4/3,599 on brujah with a 72-vs-78
    bone-count asymmetry. Next step: an offline bone-name diff from the existing capture database,
    no new capture or bake. No pipeline row falls out of this; not gated on it.
13. **Facial flex** (12.3): `FElysiumFacialRig` evaluates controllers → RPN rules → flexdescs →
    ramps → morph weights; proven headless on the real `nines` mesh, 19 rigged bodies resolve end
    to end live. Remaining: an unobstructed close-up visual (every angle blocked by scene geometry
    to date) and fixing the green-room harness, which is passed empty
    `-GreenRoomAnimSet=`/`-GreenRoomBoneRoot=` and cannot isolate a body.
14. **Eyes and eyelids** (12.4): exported `StudioEyeball` records, `M_Eyes` with both UV planes
    and the `$vampire` lerp, per-frame eye basis, lid write-back between rules and ramps, the gaze
    cascade/cone/saccade/integrator at recovered offsets, the four `LookAtEntity*` inputs are
    built and green; live blink measured at 7.25% duty against 7.06% predicted. Remaining: the
    debug surface (`elysium.npc.gaze`/`blink` verbs, a Cog Eyes tab reporting basis, planes, blink
    phase, lid weights), then the theatre-scene acceptance run itself — actors blink on their own
    cadence, lids shape with the gaze, eyes select and track targets, `Prince1.LookAtEntityEye`
    aims LaCroix at the player. Three gaze-cascade arms (enemy, navigation goal, heard sound) have
    nothing to read until their systems land (P13 combat, a move-goal accessor, a sound record) and
    fall through to the autonomous scan — retail's own fallback, not a gap. Deps: 12.3.
    Closed by RE34 (the eye system end to end, `docs/vtmb/facial_animation.md`).
15. **Lipsync** (12.5): `.lip` phoneme tracks drive mouth flexes against AUD3's line audio, a
    three-file join per line: `.lip` for timing,
    `expressions/<model stem>_phonemes.txt` for phoneme→controller weights (249 tables keyed by
    actor model basename), `mstudiomouth_t` for the amplitude jaw. Key on the phoneme *string* —
    the numeric code is not stable across the corpus. All inputs are on disk
    (`$ELYSIUM_EXPORT_ROOT/lip/`, `/expressions/`, `mouths` in the facial sidecar). The dialogue
    half is landed (`ElysiumEntityWorldDialogue.cpp` `BeginDialogueLipsync`); the theatre/scene half
    still reads AUD3's scheduled line clock. Deps: AUD3 (spec 0002), 12.3.
16. **LIFE10 note (bundled, not a witness dependency):** an armed body's four-slot
    `FElysiumOverlayStack` composes a strict subset of retail's channels — 1,113 of 1,718 captured
    frames (65%) are short at least one channel, none arms an extra one. Every `_attack_layer`,
    `_attack_delta` and `_reload_layer` across all five weapon families is unreachable from a
    gait's autolayer closure (`move_and_ranged.mdl`); they are authored by `CBaseAnimatingOverlay`,
    Source's game-pushed overlay stack, which this runtime has no counterpart for. Full contract in
    `docs/vtmb/animation_rig_resolution.md` → "The overlay contract"; runtime composition in
. This is combat/weapon content, unrelated to the
    theatre scene's witness; it is carried here only because the assignment places its plan section
    (LIFE10) in this spec's input set.

## Design
The scene player (`ElysiumSceneData`/`ElysiumScenePlayer`) stays on its verified absolute-time seek
path; LIFE7 deliberately keeps the theatre off any montage-position migration until the stack under
it is re-proven. The rewire order is: (1) make the five funnel classes loud (bank-miss report,
`SeekCinematicClip`'s no-seek report, unresolved-actor diagnostic, guard `PlayAnimation`'s
single-node fallback); (2) route a playing scene's `Kill` through cancellation cleanup, proven by a
body-backed Prince1 hand-off test; (3) fix the three claim-lifecycle leaks and add a
`CinematicClaims` sweep on body destruction; (4) settle actor re-binding (restore retail's
per-frame re-resolution, or add a re-bind hook) and collapse the double origin/angle transform
write to `SetRuntimeTransform`; (5) the camera owner call — keep cutscenes on the legacy shot stack
or move them onto `UElysiumCameraService`'s reserved `Sequence` kind — plus push
`ElysiumInput::Priority::Cinematic` for a track's duration either way; (6) a body-backed scene test
in the `ScriptedSequenceBodyClaim` mould so the path cannot silently regress again.

For LIFE10 (bundled, see Requirement 16): four explicit graph closure branches, one per overlay
slot, each carrying its own clip/mask/additive/aim-grid lifetime and envelope, composed in order —
base pose, overlay slots by index, a global autoplay pass, the flinch stack — rather than raising
the old one-overlay/one-additive fields on `FElysiumResolvedAnimation`. Every shipped additive
post-multiplies (`flags@8 & 0x10`) via `FAnimNode_ElysiumPostAdditive`, then crosses
`FAnimNode_ElysiumBankRemap` once. `m_Flinch` is a separate three-slot stack, worth checking against
the existing reaction stream but not to be folded into this work. The overlay does not use montage
lifetime.

## Seams
- Consumes: LIFE5's montage mechanism (spec unknown); AUD3's scheduled line clock for lipsync
  (spec 0002); the landed scripted-camera subsystem SC1–SC9/RG-A–RG-E (spec 0001) for the shot
  stack the camera owner call chooses between; LIFE3's resolver and LIFE4's wielded body (for
  LIFE10, spec unknown).
- Provides: the restaged choreo-scene path (actor placement, claims, camera coupling, triggers)
  that 12.1/12.3/12.4/12.5's acceptance runs against; the theatre plan's 12.x rows consume LIFE7's
  result. LIFE10 provides the four-slot overlay composition other combat/weapon specs consume
  (unrelated to this spec's witness).

## Tasks
- [~] **12.1 Choreographed scenes** — reader, timeline, nine event handlers, camera-track path,
  PC body stand-in, per-actor cinematic bank binding all built [x]. Open: staging (owned by
  LIFE7's rewire), residual RE32 material/remap work, and the final live acceptance
  (`newgame_ttd` clean of actor/clip/camera/NPC/prop diagnostics; `hide_ents` stays behind
  `elysium.SceneHideEnts` default 0; the missing authored `controls` target stays a single
  non-fatal diagnostic).
- [ ] **LIFE7 The choreo-scene rewire (hard slice)** — funnels loud, `Kill`→cancellation cleanup,
  claim-lifecycle leak fixes + `CinematicClaims` sweep, actor re-bind + transform-write collapse,
  camera owner call + `Cinematic` input priority, body-backed scene test. Acceptance: played
  scenario runs on both maps/every reachable branch per Requirement's played-scenario shape above.
- [ ] **LIFE7 Gesture/sequence un-collapse** — overlay in the four-slot array at flat `0.1` weight,
  proven or disproven only via `prince_beckett_dialog` on `la_ventruetower_1` (outside this spec's
  `sp_theatre`/`sp_tutorial_1` acceptance runs).
- [ ] **LIFE7 Paired actions** — role/size/side variant arithmetic, dual-participant claim shape;
  shipped consumer (feed transaction) unwitnessed by this spec's acceptance runs.
- [~] **LIFE7 `Prince_Escort_Male` cluster** — attribution done; open: offline bone-name diff to
  close the residual translation question (no new capture/bake).
- [~] **12.3 Facial flex track** — rig built and proven headless [x]. Open: unobstructed close-up
  visual; fix green-room harness's empty anim-set/bone-root args.
- [~] **12.4 Eyes and eyelids** — gaze layer built and green [x] (RE34 closes the eye system end to
  end). Open: debug surface (`elysium.npc.gaze`/`blink`, Cog Eyes tab), theatre-scene acceptance
  run.
- [~] **12.5 Lipsync** — dialogue-half landed [x] (`BeginDialogueLipsync`). Open: theatre/scene half
  against AUD3's line clock, theatre acceptance.
- [~] **LIFE10 Animation overlay subsystem (bundled, not a witness dependency)** — four-slot
  substrate, per-slot envelope/lifecycle, player/cast producers, five graph closures, post-multiply
  additive and shared-bank remap all land and match the reference compositor at 0.009 cm median
  (both a differing-bind Tremere and an all-copy Malkavian control) [x]. `BakedCharacterParity`,
  `OracleIdentity`, `RigRetarget`, `RigPose`, `RigLayers`, `FanDuration` green (225 gait fans/84
  owners, 207 within 0.0010 s) [x]. Open: previous-sequence cross-fades, event look-ahead, played
  two-body acceptance; `RigCompose` red at 3.168 cm control / 1.358 cm layered (legs first; cause
  named — T-C7, the cross-fade chain; not a bake gap). Gaps
  G2–G11 (fan cycle duration, event look-ahead, combat-stance stamps, `AddGesture` re-use, envelope
  arithmetic `if`/`else if` divergence, overlay per-layer event dispatch, player `aim_pitch` slew,
  previous-sequence cross-fade, between-key interpolation, two named instrument defects) — see
  `docs/vtmb/animation_rig_resolution.md` / for
  detail; none has an assigned instrument except G3 (`Elysium.Substrate.AnimEvents`), G5/G6
  (`Elysium.Substrate.OverlayStack`), G9 (`Elysium.Content.RigCompose`).

## Open questions
- The camera owner call (Requirement 8 / Design step 5): keep cutscenes on the legacy shot stack,
  or move them onto `UElysiumCameraService`'s reserved `Sequence` kind. Unsettled in the plan.
- Whether the overlay/gesture composite (Requirement 10) wants a montage slot or the weapon
  layers' overlay treatment, and whether a scene's clip change regains a crossfade — both stated
  as this rung's design call, unsettled.
- Whether the blend-profile bake already carries the per-bone weight list the overlay mask needs,
  or a second mask source must be invented (LIFE10, Requirement 16 contract item) — unspecified.
- `m_Flinch`'s three-slot stack "very likely" corresponds to the existing reaction stream — stated
  as worth checking, not confirmed, in the plan.
