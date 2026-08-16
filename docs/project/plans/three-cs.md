# 3 C's plan (CCC) — open-task specifications

Specifications for **open** CCC tasks only. Status lives solely in `docs/project/roadmap.md`;
a task that lands is deleted here. No status marks in this file.

> The slice's outcome: a real character moving in a purpose-built map — walking, running,
> sneaking, crouching and jumping from real input, framed by both camera modes, animating from
> the movement command stream with no clip named by hand.

The ladder inverts the industry order (animation last) by recovered necessity: VtMB has **no
scalar player gait speed** — the model's own per-direction cell speeds are the movement's speed
authority at the input seam (`docs/vtmb/source_movement.md` → "Player speed is
animation-driven") — so the Character rung could not close before the animation rung. The gym
splits its assertions by speed-dependence, which kept the collision thresholds baselined through
the inversion. The animation ladder itself — the cast's gait push, weapons in hands, reactions,
the viewmodel — is the LIFE programme's (`plans/animation.md`); this slice owns camera,
controls and the played movement feel. Designs: `docs/architecture/movement-architecture.md`,
`docs/architecture/camera-architecture.md`, `docs/architecture/input-architecture.md`,
`docs/architecture/animation-architecture.md`.

### CCC0 The instrument — remaining

The three sited feature courses (`stairs`, `slope`, `doorway`) carry surveyed coordinates that a
played session stands on and the headless run cannot — the body falls through or seats in a
solid — so they record without baselines. The missing floor is map-collision work, not survey
work. Two findings under it: `sp_tutorial_1` ships no ramp near the 0.7 standable normal, and
the map's `.hulls` sidecar is not the walkable surface, so a coordinate picked from it is picked
off the wrong geometry.

### CCC3 Controls response — remaining

The **character ↔ camera co-tune**, and only the co-tune. It was gated on the speed authority by
design — tuning against a walk speed the speed authority was about to move is paid for twice —
and that gate is open: the forward gaits are 53.8 / 188.5 / 65.3 u/s. It touches
`ElysiumRig::FElysiumCameraRigTuning`, never the `cam_*` console store the faithful evaluator
reads, and is performed once against the camera channels the gym records.

### CCC8 Played acceptance

The slice's finish line, **played by the owner** — owner call: no beat script, no Play-tier
automation, no 11.10 dependency. The gym and the channel differ bound correctness; what is left
is feel. *Acceptance:* the PC body walks, runs, sneaks, crouches and jumps around the gym from
real input, framed by both camera modes, with the stride following the strafe continuously and
no visible pop at a gait change; every threshold the gym brackets still holds where
`docs/vtmb/source_movement.md` says it should (the headless run says this, not the play
session); the pose is correct in the Content Browser preview and the anim editor, not only in
our runtime. *Deps:* the player speed authority (landed), CCC3's co-tune.

### Open owner calls

Enabling the `look_curve` response stage (defaults 0 = retail's linear path). Lands as a
recorded divergence in its owning doc when made.
