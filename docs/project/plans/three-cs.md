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

### CCC7 The speed authority — the NPC producer

`docs/architecture/animation-architecture.md` § 3.2 gives the gait speed tables to the driver for
**both** producers: they key on the body rather than the frame, are re-resolved only when that key
moves, and are pushed to the mover on change. Only the player's mover receives that push. An NPC's
`UCharacterMovementComponent` takes whatever speed its caller passes, and its driver's
`FElysiumGaitReference` is never seeded from `GaitFrom(GaitSpeeds)` — so the cast's walk/run split
is judged against the player's own constants rather than against each body's authored forward cell.

What remains is the second push and the classifier seed. The producer already resolves the tables:
`FElysiumAnimationDriver::Tick` fills `GaitSpeeds`/`GaitGeneration` for every body, cast included.
A caller that names its own travel speed — a scripted beat's `Custom` gait, a patrol leg — reads
the selected cell's authored ground speed instead of a constant, which is what the scripted `Walk`
gait already does through `ResolveNpcActivityClip`. `ElysiumNpcGait::WalkSpeed`/`RunSpeed` remain
the fallback for a body whose export resolves no fan.

*Acceptance:* a walking or running cast member travels at its own selected cell's authored speed,
so the stride does not slide; the walk/run threshold is the body's own forward walk cell plus one
unit rather than `ElysiumMove::WalkSpeed`; and both producers still fill one
`FElysiumLocomotionSample`, so the player's speed authority and the cast's are one system.

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

A separate body, not a camera mode. The 21 hands models and 17 packed `v_` weapon models exported
by PL14 render as **two skeletal components**: clan hands and weapon geometry. They are not merged
and the weapon is not attached to a socket on the hands component. Both consume one semantic
animation intent, resolve the matching family sequence and evaluate independently in the same
camera-root space; each owns its mesh, sequence/cycle and bone palette.

Both assets preserve authored `Camera01` as bone 0. Their bind frames and compatible arm/hand names
are the alignment contract, while packed weapon rigs retain only the subset their geometry needs.
The bake writes complete Unreal-native, parent-relative local poses in centimetres, Z-up and
left-handed space after resolving every VtMB flag, mask and additive base. Runtime seeds each
component root from the same first-person camera transform and uses stock Unreal skeletal
evaluation. No VtMB frame, storage or pose rule is permitted in the runtime evaluation path.

The viewmodel projection is independent of player FOV: `t = tan(viewmodel_fov * pi / 360)`, with
projection scales `1/t` and `aspect/t`, clip range 1..28400 in the recovered VtMB space, and normal
aspect policy 4:3 or 16:9 under the widescreen/anamorphic setting. The 11.13d view projection owns
the local-body/viewmodel visibility decision. A hidden view suppresses both component submissions
without destroying components or resetting their visual sequence state.

Server sequence events are timing carriers into weapon mode dispatch; weapon logic, not either
visual component, commits a shot or reload transaction. Client viewmodel events own muzzle flash,
magazine/shell and other presentation only. CCC10.1 therefore consumes the semantic
animation-intent seam and presents its result; it does not introduce a viewmodel-specific clip call
or a second gameplay event path. Tremere shield scripts swap the hands-role model to the baked male
or female `_shield` variant without changing any other part of the contract.

**Melee has no first-person model, measured:** the hands bank carries 154 sequences over
exactly 12 firearm families (each with `idle`/`idleempty`/`fidget`/`draw`/`lower`/`fire`/
`fireempty`/`reload`/`dryfire`, the `m37` with a three-part reload answering `reload_single`)
plus seven `v_lockpicks_*` sequences and nothing else. **Owner call:** when retained first person
suppresses retail's melee/`force_3rd` camera move, the presenter renders **no hands and no weapon**.
It does not synthesize a melee viewmodel or reuse a third-person body clip.

**Scope — owner call, made: ranged only.** Acceptance is the 12 firearm families. `thrown` is
dropped: the throwing star has zero models anywhere, and the patch-restored frag grenade has a
complete model set but no `grenade_*` hands family. The lockpick and Discipline viewmodels ride
the same machinery, deferred rather than designed out.

*Acceptance is presentation-only.* Test intents drive draw, idle, fire, dry-fire, ordinary reload
and the M37 begin/per-shell/complete visual phases on both components and every accepted firearm
family. The test proves clan/shield selection, matching sequence/cycle, `Camera01`/right-hand
alignment, both FOV/aspect policies, visibility suppression/resume and the empty melee view. It
does not spend ammunition, create a ray/projectile, advance reserve-to-magazine state or originate
a VtMB event. Firearms and melee basics (13.3) retain ammo/shot/reload authority; CCC11 retains the
sequence-event carrier, with ANM4b supplying its catalog/event data.

*Deps:* PL14's complete two-role corpus; 11.13d's `FElysiumViewState` body/viewmodel visibility
projection; the semantic animation-intent seam and CCC10's shared weapon-family translation;
ANM4b/CCC11 for real event-carrying actions. The recovered retail contracts are owned by
`docs/vtmb/animation_and_movers.md` and `docs/vtmb/camera-view-modes.md`.

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
