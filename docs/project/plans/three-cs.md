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
the inversion. Designs: `docs/architecture/movement-architecture.md`,
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

The **character ↔ camera co-tune**, and only the co-tune. It was gated on CCC7 by design —
tuning against a walk speed the speed authority was about to move is paid for twice — and that
gate is open: the forward gaits are 53.8 / 188.5 / 65.3 u/s. It touches
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
our runtime. *Deps:* CCC7 (landed), CCC3's co-tune.

### CCC10.1 The first-person rung — the viewmodel body

A separate body, not a camera mode: nothing in CCC10's node set carries over, because the
viewmodel has its own skeleton and clip vocabulary. The 21 clan hands models
(`v_<clan>_<gender>_hands.mdl`; PL14 exports them) compose with 17 packed `v_` weapon models
over a 42-bone `Camera01`-rooted rig — a second skeleton the character bake's "poses are baked
native" result does not cover.

**Melee has no first-person model, measured:** the hands bank carries 154 sequences over
exactly 12 firearm families (each with `idle`/`idleempty`/`fidget`/`draw`/`lower`/`fire`/
`fireempty`/`reload`/`dryfire`, the `m37` with a three-part reload answering `reload_single`)
plus seven `v_lockpicks_*` sequences and nothing else. The **owner-called divergence stands**:
the equipped weapon does not move the player's camera (retail's `camera_class` arbitration —
melee/`force_3rd` yanks to third, `force_1st` into first, `togglecamera` on a forced weapon
holsters — stays implementable behind a toggle per the Feel layer's A/B rule). The accepted
consequence: a melee weapon in first person has nothing to draw; what that view shows is
deliberately open — the ✳ placeholder marks the question so nobody mistakes it for a design.

**Scope — owner call, made: ranged only.** Acceptance is the 12 firearm families. `thrown` is
dropped: the throwing star has zero models anywhere, and the patch-restored frag grenade has a
complete model set but no `grenade_*` hands family. The lockpick and Discipline viewmodels ride
the same machinery, deferred rather than designed out.

*Open RE (tracked as RE42), in dependency order — the first question gates the rung's shape:*
how the two viewmodels compose (arms from the clan hands model, geometry from the weapon's —
`m_hViewModel[]` beside `m_pViewWeapon` — asset-layout inference, not traced); the
`Camera01`-rooted rig's pose convention; how `viewmodel_fov 54` composes with the camera's FOV;
whether the first-person shot is driven by the same sequence event on the viewmodel clip or the
world model keeps driving it unseen.

*Acceptance:* a firearm draws, idles, fires, dry-fires and reloads in first person on the
clan's own hands model, driven through the same intent seam rather than a viewmodel-specific
clip call; the m37's three-part reload matches its `reload_single` transaction; equipping a
melee weapon does not move the camera, and whatever the empty first-person view shows is a
stated choice; the owner judges it live in the green room, which needs a first-person stage the
layer lab does not have. *Deps:* RE42, PL14, CCC10's shared weapon-family translation. The
camera-class facts are `docs/vtmb/camera-view-modes.md`'s.

### CCC11 The action families beyond locomotion

Directional hit, knockback and death reactions, then weapon and interaction actions; each rung
closes its own reachability slice. Every remaining producer moves onto the intent seam —
`PlayNpcActivity` stays a compatibility adapter until patrol and scripted travel cross over.
**Owns the pose, not the number**: lethality, soak, the damage roll and the health commit are
13.3's; this rung consumes their outcome.

Recovered resolver rules it adds (all policy over baked catalog data — none adds graph
machinery): the NPC class/weapon translation alternation and its four-way availability ladder,
with the first weapon answer preserved separately (reload start derives both end times from the
*selected sequence's* duration over its rate, not the authored `ReloadTime`, so the resolver
hands back the chosen sequence's duration); transition-sequence traversal between current and
ideal sequences (reads the model's sequence-transition graph — ANM4b's catalog input); the
restart rule that clears and restarts a repeated identical request; paired-action
role/size/side variant arithmetic; the **authored, not directional** blocked reaction (the
attacker plays the activity its own sequence descriptor stores, `ACT_BLOCKED_REACTION_RIGHT`
fallback — only the flinch is directional, `ACT_HIT_HEAD`/`ACT_HIT_TORSO` by `hit_yaw`); and
the `2COMBO` substitution keyed by base Brawl/Melee. There is no firearm stagger to build —
ranged capability `0x2000` does not satisfy the block gate `0x18000`.

One montage-slot mechanism serves both `scripted_sequence`'s `m_iszIdle → m_iszPlay →
m_iszPostIdle` and an interesting place's enter/hold/leave segments — the same shape, built
together or there come to be two.

**The sequence-event carrier is built here.** Nothing in the bake emits a `UAnimNotify`, so a
VtMB sequence event reaches no Unreal notify and the ballistics trigger has nothing to ride —
an attack-layer event is what reaches `CWeaponRanged::Shot`, on 4–16-frame clips where a
sub-threshold node weight during a blend drops queued notifies. The bake and the guard test
land together; a guard before the carrier would assert against a synthetic notify and rot.

*Acceptance:* an NPC's ambient behaviour and a scripted beat both reach a pose through the
intent seam, and `m_iszCustomMove` plays over a travelling body; a struck body flinches on the
`hit_yaw` grid the hit direction selects, and a blocked attacker plays the reaction its own
sequence names; a weapon's attack-layer sequence event reaches the shot while the layer is
still blending in. *Deps:* CCC10, ANM4b, `docs/vtmb/combat-and-damage.md` (its open joins are
numeric and gate 13.3, not this rung).

### Open owner calls

Movement-orientation/strafing settings so `move_yaw` resolves off the neutral cell; sync-group
phase matching between gaits (retail's crossfades are phase-independent); enabling the
`look_curve` response stage (defaults 0 = retail's linear path). Each lands as a recorded
divergence in its owning doc when made.
