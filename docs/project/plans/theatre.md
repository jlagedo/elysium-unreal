# Theatre plan — open-task specifications

Specifications for **open** P12 tasks. Status lives solely in `docs/project/roadmap.md`; a task
that lands is deleted here. No status marks in this file. The intro cinematic (`sp_theatre` —
embrace + trial) as VtMB plays it; the fidelity bar is an owner call: the scene is not done
until the faces are alive, **eyes and lipsync included**. Facts:
`docs/vtmb/choreographed_scenes.md`, `docs/vtmb/facial_animation.md`,
`docs/vtmb/animation_and_movers.md`. RE33 verifies 12.3–12.5 rather than gating them; RE34
makes 12.4 a plain reproduction.

### 12.1 Choreographed scenes — remaining

The reader/timeline/entity, cameras, PC body, controller transfer and the per-actor cinematic
bank binding are implemented and verified. **Remaining:** the residual RE32 material/remap work
and the final live acceptance pass — `newgame_ttd` with no unexpected actor, clip, camera, NPC
or prop diagnostics (deferred scene audio/facial/lip work belongs to 12.2–12.5). `hide_ents`
stays behind `elysium.SceneHideEnts` (default 0); the authored missing `controls` target stays
a single non-fatal diagnostic.

### 12.2 Scene audio + subtitles

Per-line audio through 6.5/6.6's shared line service (the `PlayDialogFile` resolution rules)
synced to scene time; a subtitle surface on the view state (11.8). *Acceptance:* the scene's
lines are audible and subtitled in sync. *Deps:* 12.1, 6.5, 6.6, 11.8.

### 12.2b Scene mixahead calibration *(blocks lipsync precision)*

The runtime applies VtMB's `snd_mixahead` 0.100 s to every `speak` event; our own audible
latency is ~21–61 ms, so dialogue is heard 40–80 ms early against every authored cue — lipsync,
expressions, gestures and camera cuts alike. One term unmeasured: `ScheduledAudioClock` is
stamped at submit, before the async mp3 decode. *Acceptance:* the lead matches the measured
path, with the residual stated. *Deps:* none — a constant and a measurement.

### 12.3 Facial flex track — remaining

Built: `FElysiumFacialRig` evaluates controllers → RPN rules → flexdescs → ramps → morph
weights; headless proof on the real `nines` mesh; 19 rigged bodies resolve end to end live.
**Remaining: an unobstructed close-up visual** — every angle was blocked by scene geometry, and
the green-room harness cannot isolate a body because it is passed empty `-GreenRoomAnimSet=`/
`-GreenRoomBoneRoot=` (the CAP critical path's row 10). *Acceptance:* a flex authored in the
model moves the face in-game, seen clean.

### 12.4 Eyes and eyelids — remaining

Built and green through the gaze layer: exported `StudioEyeball` records, `M_Eyes` with both UV
planes and the `$vampire` lerp, the per-frame eye basis, the lid write-back between rules and
ramps, the gaze cascade/cone/saccade/integrator at the recovered offsets, the four
`LookAtEntity*` inputs; live blink at 7.25% duty against 7.06% predicted. **Remaining:** the
debug surface (`elysium.npc.gaze`/`blink` verbs, a Cog Eyes tab reporting basis, planes, blink
phase, lid weights), then the acceptance run — a theatre scene, not a tutorial NPC: actors
blink on their own cadence, lids shape with the gaze, eyes select and track targets, and
`Prince1.LookAtEntityEye` aims LaCroix at the player. Three cascade arms have nothing to read
until their systems land (enemy → P13 combat, navigation goal → a move-goal accessor, heard
sound → a sound record); each falls through to the autonomous scan, which is retail's own
fallback. *Deps:* 12.3.

### 12.5 Lipsync

`.lip` phoneme tracks driving mouth flexes against 12.2's line audio — a three-file join per
line: the `.lip` for timing, `expressions/<model stem>_phonemes.txt` for phoneme→controller
weights (249 tables, keyed by the actor's model basename), and `mstudiomouth_t` for the
amplitude jaw alongside. Key on the phoneme *string* — the numeric code is not stable across
the corpus. All inputs are on disk (`$ELYSIUM_EXPORT_ROOT/lip/`, `/expressions/`, `mouths` in
the facial sidecar). *Acceptance:* mouths move with the words on every theatre line. *Deps:*
12.2, 12.3.
