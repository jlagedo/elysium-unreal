# 0015 weapon-overlays — an armed body composes every channel retail's overlay stack pushes

## Witness
An armed body's four-slot `FElysiumOverlayStack` composes what retail's `CBaseAnimatingOverlay`
composes: every `_attack_layer`, `_attack_delta` and `_reload_layer` across the five weapon
families plays over the gait, the previous-sequence cross-fade and the event look-ahead match the
reference compositor, and two bodies compose against the 1,718 captured frames with no channel
short. Combat and weapon content, witnessed after the tutorial is beatable.

## Scope
The overlay subsystem (LIFE10): the four graph-closure branches, per-slot envelope and lifetime,
the post-multiplied additive, the shared-bank remap, the cross-fade chain, the event look-ahead,
the mask source. Owned elsewhere and consumed here: the gesture that rides the same array —
**0010** (8); the flinch stack (`m_Flinch`, three slots) — 0005's reaction stream, to be checked
not folded in; the wielded weapon and the montage mechanism — **0005**.

## Sources
- Oracle: `docs/vtmb/animation_rig_resolution.md` → "The overlay contract",
  `docs/vtmb/animation_and_movers.md` A.3 / A.4c, `docs/vtmb/animation_events.md`.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `models/` (`move_and_ranged`
  and the five weapon families' layers), the five `sp_theatre` capture databases.

## Witness data
- 1,113 of 1,718 captured frames (65 %) are short at least one channel, none arms an extra one;
  every `_attack_layer`, `_attack_delta` and `_reload_layer` is unreachable from a gait's
  autolayer closure and is authored by `CBaseAnimatingOverlay`, Source's game-pushed stack.
- Landed: the four-slot substrate, per-slot envelope/lifecycle, player and cast producers, five
  graph closures, the post-multiplied additive (`flags@8 & 0x10`, `FAnimNode_ElysiumPostAdditive`)
  crossing `FAnimNode_ElysiumBankRemap` once — matching the reference compositor at 0.009 cm
  median on a differing-bind Tremere and an all-copy Malkavian; `BakedCharacterParity`,
  `OracleIdentity`, `RigRetarget`, `RigPose`, `RigLayers`, `FanDuration` green (225 gait fans /
  84 owners, 207 within 0.0010 s). Open: `RigCompose` red at 3.168 cm control / 1.358 cm layered
  (legs first; cause named — T-C7, the cross-fade chain; not a bake gap); gaps G2–G11.
- The overlay does not use montage lifetime.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The four-slot substrate** (was LIFE10): the closures, the envelope, the additive, the
  remap. Oracle: `animation_rig_resolution.md`.
- [ ] **2. The previous-sequence cross-fade** (T-C7, G9).
  Job: the cross-fade chain so `RigCompose` goes green on control and layered.
  Oracle: `animation_rig_resolution.md`.
  Size: M. Effort: Opus / high.
- [ ] **3. Events** (G3, G7).
  Job: the event look-ahead; overlay per-layer event dispatch.
  Oracle: `animation_events.md`.
  Size: S. Effort: Sonnet / high.
- [ ] **4. The stamps and the arithmetic** (G2, G4, G5, G6, G8, G10).
  Job: fan cycle duration; combat-stance stamps; `AddGesture` re-use; the envelope arithmetic's
  `if` / `else if` divergence; the player `aim_pitch` slew; between-key interpolation.
  Oracle: `animation_rig_resolution.md`.
  Size: M. Effort: Sonnet / high.
- [ ] **5. The two named instrument defects** (G11).
  Job: fixed, so the instruments measure what they claim.
  Size: S. Effort: Sonnet / medium.
- [ ] **6. The mask source.**
  Job: whether the blend-profile bake already carries the per-bone weight list the overlay mask
  needs, or a second source must be authored — settled, then built.
  Size: S. Effort: Sonnet / high.

## Seams
- Provides: the four-slot composition to 0010's gesture and every combat/weapon consumer.
- Consumes: 0005's wielded weapon and montage mechanism.
- Open recoveries: the mask source (6); whether `m_Flinch` is the existing reaction stream.
