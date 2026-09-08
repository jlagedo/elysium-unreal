# 0001 scripted-camera — VtMB's scripted-camera subsystem: camera_cinematic, the override channel and its five witnesses

## Witness
Five Play-tier witnesses each prove a disjoint slice of the subsystem, since no single map exercises
all of it: `Elysium.Play.TutorialCamera` (`sp_tutorial_1` — the `logic_scene_1` authored track, the
feed camera's Start/EndShot pair, both Jack `StartPlayerDialogRemote` conversations) proves the
composition shape, the crossfade stack, `camera_cinematic`'s adoption/immobilize/hard-cut cycle, the
one-shot snap, the tracker's numerics and the dialogue immobilize/holster pair; `Elysium.Play.TheatreCamera`
(`sp_theatre`'s `courtroom_*`/`walk_out_*`/`embrace_*` chains) proves the composition and crossfade
stack survive 115+ folded edits and repeated owner replacement; `Elysium.Play.DialogueCamera` (a
dialogue map with per-line `SetCamera` calls, one naming a missing shot) proves the `DialogDefault`
fallback, the re-shot continuation and `DialogPOV`'s feasibility gate; `Elysium.Play.StealthKillCamera`
proves `FindBestShot`'s enumeration/predicates/seeded pick and the 4050/4051 `m_bDrawPlayer` gate;
`Elysium.Play.TerminalCamera` (Hacking/Intrusion) proves the shared adoption slot's destroy rule and
the HUD/viewmodel edges. Substrate-tier witnesses per slice and `Elysium.Content.DialogueCameraDemand`
/ `.OpeningCameraTracks` / `.DialogueCameraChain` cover the headless corpus. Vantages are captured at
weight 0/0.5/1 via `uv run elysium debug shots` on every Play-tier witness; live-piloted play of all
five is the final, owner-only acceptance step and has not yet happened.

## Scope
- Roadmap rows absorbed: 11.13 Reconstruction camera director (d–h open); SC1–SC9 and RG-A–RG-E
  (all landed, closing RE46) as the subsystem's completed foundation this spec's remaining rows build on.
- Out of scope (belongs to another spec or is parked):
  - 11.14 the reachability query.
  - Victim-side `CheckAndTranslateGrapplePosition` placement half — feed/grapple lane.
  - The lock HUD progress byte and `CWeaponLockpick` feedback — lock lane, `docs/vtmb/computer-terminals.md`.
  - `PackDeadPlayerItems` — death/inventory lane.
  - Client ragdoll, `EF_NODRAW` and the `MOVETYPE_NONE` death freeze — body/movement lanes.
  - `camera_animated` re-model rebuild on `FElysiumAnimating::OnRuntimeModelChanged` — animation plan.
  - RE53 discipline-magnitude delta, RE54 physics/ragdoll, RE-K/RE-D/RE-R combat rows — unrelated systems.

## Requirements
Each is a retail behaviour the port reproduces; ruled divergences are named modernizations (owner
ruling 2026-09-07, register M1–M16 in full — see Design/Tasks note).

1. The scripted-shot release is a same-tick removal on every retail end path with no blend field and
   no release-blend cvar. `docs/vtmb/camera-view-modes.md`.
2. The goal-publish think runs at 24 Hz (`_DAT_1044eb04 = 0.04166`) with the tracker advancing once per
   rendered frame, internal order: anchor cache → look-at → origin selector → `AutoPositionFromTarget`
   → angle gate → publish → `point_player`. `docs/vtmb/camera-view-modes.md`.
3. `AttachType` matching is a case-sensitive exact byte compare (`FUN_10071e00`); a mis-cased `follow`
   is `None` and latches instead of following. `docs/vtmb/camera-view-modes.md`.
4. The fade state machine is ported verbatim: signed-duration storage, the lazy reap in
   `GetCameraOverrideWeight`, the back-dated re-time, per-entity `max()` minimums, `dur ≤ 0` instant
   snap, and the N-entry crossfade stack. `docs/vtmb/camera-view-modes.md`.
5. `RemainingTime` (`FUN_100010f0`) is reproduced verbatim with only the radicand clamped at zero — a
   tuning constant shipped content is authored against. `docs/vtmb/retail-defects.md` §7.
6. `MoveAccel == 0` reproduces retail's crawl (decel arm, 1.0 u/s floor) via an explicit branch, never
   a real divide. `docs/vtmb/camera-view-modes.md`.
7. The terminal's camera handle is one adoption slot; a terminal closer drops to the player view, never
   to a previously stacked shot. `docs/vtmb/computer-terminals.md`.
8. `SimpleSpline` composition (`FUN_100ffb90`) — ease, point lerp at `origin + fwd × 240u`,
   `roll = e × shotRoll` — is contract, not a modernization; authored `FromPlayerTime` values are tuned
   against it. `docs/vtmb/camera-view-modes.md`.
9. The cine camera beats a vehicle view in ordering; the vehicle arm itself has no live writer and is
   dead code. `docs/vtmb/camera-view-modes.md`.
10. `camera_fov`'s freeze guard (`FUN_10001c20`), default `"-1"`, threshold `10.0f`, is reproduced by
    name. `docs/vtmb/camera-view-modes.md`.
11. The HUD `0xa06d` mask's edge timing is contract (hide on shot-change-into-clear, show on
    change-into-set, show on going inactive/on destruction, no per-frame enforcement); element
    membership is not reproduced (menu/HUD are new assets). `docs/vtmb/camera-view-modes.md`.
12. `camera_showdebug`'s overlay does nothing on live retail; the cvar is declared, no overlay is drawn.
    `docs/vtmb/camera-view-modes.md`.
13. `FindBestShot`'s `"%s_%d"` enumeration, both geometry predicates and the seeded uniform pick are
    reproduced; anim events 4050/4051 are the only non-zero writer/reader pair for `m_bDrawPlayer`.
    `docs/vtmb/animation_events.md`.
14. `SetCamera`'s `DialogDefault` fallback exists; `StartPlayerDialog` does not use it. The immobilize
    pair, the payphone grapple arm, the same-tick dialogue-end cut and `DialogPOV`'s feasibility gate
    are reproduced. Closes RE46. `docs/vtmb/camera-view-modes.md`.
15. The anchor grammar (`AbsMin`/`AbsMax`, `Top`/`Bottom`, `GrappleVictim`/`GrappleAttacker` on live
    role state, `World` fallthrough, `Follow` offset, cached bone indices) is complete.
    `docs/vtmb/stealth.md` (grapple role pair), `docs/vtmb/camera-view-modes.md`.
16. `AutoPositionFromTarget`'s `h = d/sin A`, `r = √(h²+d²)` pull-back, lower-Z point only, is
    reproduced, suppressed on the third origin-selector arm. `docs/vtmb/camera-view-modes.md`.
17. modernization: the `camortho` rect is Source's orthographic debug view (`c_orthowidth`/
    `c_orthoheight`, default 100); Unreal's undistorted letterbox vs. retail's window-stretch is the
    only pixel-only divergence.
18. modernization: PVS replacement is not applicable — Unreal culls from the actual view; retail's
    hand-moved PVS origin artefact (near-player-far-from-lens entities stop updating) is deliberately
    not reproduced.
19. modernization: no Hor+ FOV widening at apply time — no `ScaleFOVByWidthRatio` exists in retail;
    Unreal's native engine widening is applied to the one parsed `viewsetup.fov` scalar.
20. modernization: replication bit-quantization is skipped; the encoder clamps (FOV [0,180], roll
    [−180,180], fade duration ±10s) are kept as contract.

## Design
The port's shape as landed: `camera_cinematic` is a director/runtime split entity with four targetname
anchors, a disposable-destroy rule and the verbatim-order 24 Hz think (SC4). One adoption slot replaces
retail's implicit last-writer-wins camera handle (M8). Composition is a single weighted stack feeding
both the cine hard-write channel and the blended track-override channel (SC2), fed by an N-entry
crossfade stack under `ElysiumCameraComponent`/`ElysiumCameraService`. The client tracker
(`ElysiumCameraSolve.cpp`) runs the frame-latched numerics (position/angles/turn-rate/FOV) against the
24 Hz goal. `FindBestShot`, the anchor grammar and `AutoPositionFromTarget` live in
`ElysiumCameraShots.{h,cpp}` and `ElysiumCameraCinematic.{h,cpp}`. Dialogue driving is in
`ElysiumEntityWorldDialogue.cpp` / `ElysiumDialogueCamera.h`.

## Seams
- Consumes: 8.10 (final user-options surface for 11.13d), 9.2/9.9 (played dialogue-resolution
  coverage), 9.8 (real inventory/interaction coverage for 11.13e), 11.10 Play tier (headless beat
  driver the five witnesses run on).
- Provides: the camera contract (adoption slot, fade/crossfade machinery, `CamMode` arms, anchor
  grammar, HUD/draw gates) that 11.13d–h, the feed/death producers, and the sequencer bridge (11.13g)
  build on.

## Tasks
- [x] SC1 The client tracker's numerics and frame contract (1.0° band, 1.0 u/s floor, dt clamp, `MoveAccel 0` crawl, FOV freeze guard).
- [x] SC2 The two channels and the `CInput` composition shape (cine hard-write vs. blended track, `SimpleSpline`, mutual exclusion, cut-with-no-blend release).
- [x] SC3 The override fade machinery and crossfade stack (signed duration, lazy reap, back-dated re-time, N-entry stack).
- [x] SC4 `camera_cinematic`, the adoption slot, `CamMode` and immobilize (24 Hz think, five modes, `point_player`, `m_bDrawPlayer`).
- [x] SC5 Shot start, draw gates and the HUD edge (`End`-without-`Start` dolly-in, speed-gated viewmodel, HUD hide/show edge).
- [x] SC6 The anchor grammar completed (`AbsMin`/`AbsMax`, grapple role pair, case-sensitive `AttachType`).
- [x] SC7 `AutoPositionFromTarget` (pull-back kernel, third origin-selector arm suppression).
- [x] SC8 `FindBestShot` and anim events 4050/4051 (enumeration, predicates, seeded pick, stealth-kill gate).
- [x] SC9 The dialogue and script chain (`DialogDefault`, immobilize/holster, payphone arm, `DialogPOV`). Closes RE46.
- [x] RG-A–RG-E Recovery tasks unblocking SC3/SC4/SC1/SC2/SC6/SC7/SC9 (vtable/spawnflags, track-channel interface, image constants, dialogue admission/gaze gate, anchor kernels/grapple role).
- [ ] 11.13d Input, settings and presentation — camera commands in the action catalog; inspect/dialogue/cinematic scopes on `UElysiumInputSubsystem`; `FElysiumViewState` (reticle/HUD/body/viewmodel/letterbox); accessibility (FOV, recenter, shake/head-motion/recoil, motion blur); `UElysiumCameraProfile` asset.
- [ ] 11.13e Prop focus, map triggers and public API — focusable target specs, soft-focus/inspect requests, collision/framing fallback, trigger component/volume, C++ value API, embedded Python opaque handles, map-epoch teardown, compatibility-safe `SetCamera`/`RemoveCamera` ownership. Camera never moves/rotates the player to frame an item.
- [~] 11.13f Dialogue director — scoped request, source-shot-first selection, deterministic grammar, body-owner transaction, save refusal, diagnostics and headless/content coverage landed; controlled UP Plus capture and played resolution/input acceptance remain.
- [ ] 11.13g Sequencer bridge — authored Level Sequences/Cine Cameras get one `Sequence` request; Camera Cut Track owns transforms/lenses/cuts/blends with no second interpolation; stop/abort/skip/travel release cleanly; legacy VCD/Worldcraft timing stays in the legacy evaluator.
- [ ] 11.13h Integration acceptance — migrate feed/death and every remaining direct producer; retain the theatre's 12.1 acceptance; request/focus/dialogue/Python/Sequencer automation over the whole director; every scoped camera returns to the exact chosen view; no camera path rotates/navigates the character (player-view half is CCC8's).

## Open questions
- The 60° living lens (`m_iFOV == 0 ⇒ 60`, not `default_fov 75`) is global and wants an owner's eye on
  real play before being treated as settled.
- Live-piloted acceptance of all five Play-tier witnesses (tutorial feed + both Jack conversations,
  sp_theatre's opening, a stealth kill, a terminal session) has not run; dev shortcuts are not
  acceptance evidence per the roadmap's standing rule.
