# 0012 camera-director — every scoped camera returns to the exact chosen view: input scopes, prop focus, the sequencer bridge, the last direct producers

## Witness
A scoped camera request (inspect, dialogue, cinematic, feed, death, an authored Level Sequence)
takes the frame and returns it to the exact view the player had, with input integrating only
where the scope allows it; no camera path rotates or navigates the character. The theatre's
scene acceptance (0010) is retained under the director. Live-piloted play of the five
scripted-camera witnesses of 0001 remains the owner's final step.

## Scope
The director rows 0001 left open once the scripted-camera subsystem landed: input, settings and
presentation; prop focus, map triggers and the public API; the dialogue director's remainder;
the sequencer bridge and the owner call on cutscene composition; the migration of the last
direct producers. Owned elsewhere and consumed here: the landed subsystem (SC1–SC9, RG-A–RG-E)
— **0001**; the theatre scene path and its acceptance — **0010**; the player-view half (CCC8) —
unowned.

## Sources
- Oracle: `docs/vtmb/camera-view-modes.md`, `docs/vtmb/computer-terminals.md`,
  `docs/vtmb/stealth.md` (the grapple role pair), `docs/vtmb/animation_events.md` (4050/4051).
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/*.entities.glb`
  (`camera_cinematic`, `point_player`, `default_camera`), `dialogues/` (per-line `SetCamera`).

## Witness data
- The port's shape as landed (0001): `camera_cinematic` is a director/runtime split entity with
  four targetname anchors, a disposable-destroy rule and the verbatim-order 24 Hz think; one
  adoption slot replaces retail's last-writer-wins handle; composition is one weighted stack
  feeding the cine hard-write channel and the blended track-override channel over an N-entry
  crossfade stack; the client tracker runs the frame-latched numerics against the 24 Hz goal.
- The camera has two composition systems on one component: the legacy shot stack cutscenes use
  and win the frame with, and `UElysiumCameraService`'s `Sequence` request kind, reserved for
  cutscenes and unproduced; they arbitrate by layering accident. Nothing pushes
  `ElysiumInput::Priority::Cinematic`, so live look input integrates through a shot and the base
  rig snaps to it when the shot's weight ramps out. The owner call — keep cutscenes on the legacy
  shot stack or move them onto `Sequence` — is this spec's (moved from 0003/0010 on 2026-09-12).
- The 60° living lens (`m_iFOV == 0 ⇒ 60`, not `default_fov 75`) is global and wants the
  owner's eye on real play before it is treated as settled.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [ ] **1. Input, settings and presentation** (was 11.13d).
  Job: camera commands in the action catalog; inspect/dialogue/cinematic scopes on
  `UElysiumInputSubsystem`, with `Priority::Cinematic` pushed for a track's duration;
  `FElysiumViewState` (reticle, HUD, body, viewmodel, letterbox); accessibility (FOV, recenter,
  shake/head-motion/recoil, motion blur); a `UElysiumCameraProfile` asset.
  Provides: the viewmodel visibility decision 0013 reads.
  Size: M. Effort: Sonnet / high.
- [ ] **2. Prop focus, map triggers and the public API** (was 11.13e).
  Job: focusable target specs, soft-focus/inspect requests, collision/framing fallback, a trigger
  component/volume, the C++ value API, embedded Python opaque handles, map-epoch teardown,
  compatibility-safe `SetCamera` / `RemoveCamera` ownership. The camera never moves or rotates
  the player to frame an item.
  Size: M. Effort: Sonnet / high.
- [ ] **3. The dialogue director's remainder** (was 11.13f).
  Job: the scoped request, source-shot-first selection, deterministic grammar, body-owner
  transaction, save refusal and diagnostics are landed; the controlled UP Plus capture and the
  played resolution remain.
  Size: S. Effort: Sonnet / medium.
- [ ] **4. The sequencer bridge and the composition call** (was 11.13g).
  Job: the owner call recorded (legacy shot stack vs `Sequence`), then: authored Level Sequences
  and Cine Cameras get one `Sequence` request; the Camera Cut Track owns transforms, lenses, cuts
  and blends with no second interpolation; stop/abort/skip/travel release cleanly; legacy VCD /
  Worldcraft timing stays in the legacy evaluator.
  Consumes: 0010's scene path.
  Size: M. Effort: Opus / high.
- [ ] **5. The last direct producers** (was 11.13h).
  Job: feed and death and every remaining direct producer migrated onto the director; every
  scoped camera returns to the exact chosen view; the theatre's acceptance retained.
  Consumes: 0010, 0005 (death), the feed.
  Size: M. Effort: Sonnet / high.

## Seams
- Provides: the director's scopes, focus and bridge to 0013 (viewmodel visibility), 0010 and
  0017 (the camera save admission).
- Consumes: 0001's landed subsystem; 0010's scene path (4, 5).
- Open recoveries: the 60° living lens's owner ruling; the five live-piloted witnesses.
