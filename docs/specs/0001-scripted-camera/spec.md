# 0001 scripted-camera — VtMB's scripted-camera subsystem: camera_cinematic, the override channel and its five witnesses

## Witness
Five witnesses, each a disjoint slice of the subsystem: `sp_tutorial_1` (the `logic_scene_1`
authored track, the feed camera's Start/EndShot pair, both Jack `StartPlayerDialogRemote`
conversations) for the composition shape, the crossfade stack, `camera_cinematic`'s adoption /
immobilize / hard-cut cycle, the one-shot snap, the tracker's numerics and the dialogue
immobilize/holster pair; `sp_theatre`'s `courtroom_*` / `walk_out_*` / `embrace_*` chains for
the stack surviving 115+ folded edits and repeated owner replacement; a dialogue map with
per-line `SetCamera` calls, one naming a missing shot, for the `DialogDefault` fallback, the
re-shot continuation and `DialogPOV`'s feasibility gate; a stealth kill for `FindBestShot` and
the 4050/4051 `m_bDrawPlayer` gate; a terminal session for the adoption slot's destroy rule and
the HUD/viewmodel edges. Landed; live-piloted play of all five is the owner's step.

## Scope
The subsystem: the client tracker, the two channels and their composition, the override fade
machinery, `camera_cinematic` and the adoption slot, shot start and the HUD edge, the anchor
grammar, `AutoPositionFromTarget`, `FindBestShot`, the dialogue and script chain. Owned
elsewhere: the director rows built on it (input scopes, prop focus, the sequencer bridge, the
last direct producers) — **0012**; the victim-side `CheckAndTranslateGrapplePosition` — the
feed/grapple lane; the lock HUD byte — `computer-terminals.md`; `PackDeadPlayerItems`,
client ragdoll, `EF_NODRAW`, the `MOVETYPE_NONE` death freeze — body and death lanes;
`camera_animated`'s re-model rebuild — the animation lane.

## Sources
- Oracle: `docs/vtmb/camera-view-modes.md`, `docs/vtmb/computer-terminals.md`,
  `docs/vtmb/animation_events.md`, `docs/vtmb/stealth.md`, `docs/vtmb/retail-defects.md` §7.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/*.entities.glb`
  (`camera_cinematic`, `point_player`, `camera_fov`), `dialogues/`.

## Witness data
The retail contract, each item reproduced (owner ruling 2026-09-07; modernizations M1–M16 in
`camera-view-modes.md`):
- The scripted-shot release is a same-tick removal on every retail end path, no blend field, no
  release-blend cvar.
- The goal-publish think runs at 24 Hz (`_DAT_1044eb04 = 0.04166`) with the tracker advancing
  once per rendered frame, in the order anchor cache → look-at → origin selector →
  `AutoPositionFromTarget` → angle gate → publish → `point_player`.
- `AttachType` is a case-sensitive exact byte compare (`FUN_10071e00`); a mis-cased `follow` is
  `None` and latches.
- The fade machine verbatim: signed-duration storage, the lazy reap in
  `GetCameraOverrideWeight`, the back-dated re-time, per-entity `max()` minimums, `dur ≤ 0`
  instant snap, the N-entry crossfade stack.
- `RemainingTime` (`FUN_100010f0`) verbatim with only the radicand clamped at zero
  (`retail-defects.md` §7).
- `MoveAccel == 0` reproduces retail's crawl (decel arm, 1.0 u/s floor) by an explicit branch.
- The terminal's camera handle is one adoption slot; a closer drops to the player view.
- `SimpleSpline` composition (`FUN_100ffb90`): ease, point lerp at `origin + fwd × 240u`, `roll =
  e × shotRoll`; authored `FromPlayerTime` values are tuned against it.
- The cine camera beats a vehicle view; the vehicle arm is dead code.
- `camera_fov`'s freeze guard (`FUN_10001c20`), default `"-1"`, threshold `10.0f`.
- The HUD `0xa06d` mask's edge timing (hide on shot-change-into-clear, show on change-into-set,
  show on going inactive / destruction, no per-frame enforcement); element membership not
  reproduced (menu/HUD are new assets).
- `camera_showdebug` does nothing on live retail; the cvar is declared, no overlay drawn.
- `FindBestShot`'s `"%s_%d"` enumeration, both geometry predicates and the seeded uniform pick;
  anim events 4050/4051 the only writer/reader pair for `m_bDrawPlayer`.
- `SetCamera`'s `DialogDefault` fallback exists; `StartPlayerDialog` does not use it; the
  immobilize pair, the payphone grapple arm, the same-tick dialogue-end cut and `DialogPOV`'s
  feasibility gate (RE46).
- The anchor grammar: `AbsMin` / `AbsMax`, `Top` / `Bottom`, `GrappleVictim` /
  `GrappleAttacker` on live role state, `World` fallthrough, `Follow` offset, cached bone indices.
- `AutoPositionFromTarget`: `h = d/sin A`, `r = √(h²+d²)` pull-back, lower-Z point only,
  suppressed on the third origin-selector arm.
- Modernizations: the `camortho` rect is Source's orthographic debug view (letterbox vs
  window-stretch is the only pixel divergence); PVS replacement not applicable (Unreal culls from
  the actual view); no Hor+ FOV widening at apply (Unreal's native widening on the one parsed
  `viewsetup.fov`); replication bit-quantization skipped, the encoder clamps kept (FOV [0,180],
  roll [−180,180], fade duration ±10 s).

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter.

- [x] **1. The client tracker** (was SC1): the 1.0° band, the 1.0 u/s floor, the dt clamp, the
  `MoveAccel 0` crawl, the FOV freeze guard. Oracle: `camera-view-modes.md`.
- [x] **2. The two channels and the composition shape** (was SC2): cine hard-write vs blended
  track, `SimpleSpline`, mutual exclusion, cut-with-no-blend release.
- [x] **3. The override fade machinery and the crossfade stack** (was SC3).
- [x] **4. `camera_cinematic`, the adoption slot, `CamMode` and immobilize** (was SC4): the
  24 Hz think, five modes, `point_player`, `m_bDrawPlayer`.
- [x] **5. Shot start, draw gates and the HUD edge** (was SC5): the `End`-without-`Start`
  dolly-in, the speed-gated viewmodel, the HUD hide/show edge.
- [x] **6. The anchor grammar** (was SC6).
- [x] **7. `AutoPositionFromTarget`** (was SC7).
- [x] **8. `FindBestShot` and anim events 4050/4051** (was SC8).
- [x] **9. The dialogue and script chain** (was SC9; closes RE46).
- [x] **10. The recovery rows** (was RG-A–RG-E): vtable/spawnflags, the track-channel interface,
  image constants, dialogue admission and the gaze gate, anchor kernels and the grapple role.

## Seams
- Provides: the camera contract (adoption slot, fade/crossfade machinery, `CamMode` arms, anchor
  grammar, HUD/draw gates) to 0012, the feed/death producers and the sequencer bridge.
- Consumes: nothing open.
- Open: the 60° living lens and the five live-piloted witnesses, carried by 0012.
