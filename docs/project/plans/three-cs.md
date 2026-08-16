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

### CCC10.2 The third-person rung — the wielded weapon body

What a character holds, so a drawn weapon is visible on the body that swings it. The recovered
contract — the four item model roles, the equip transaction and its sex selection, the follow-attach
bone copy, the seven unskinned prop bones on the character body, the `anim_prefix` key and the
shipped corpus — is `docs/vtmb/wielded_weapons.md`. Read it first; it is not restated here.

**The design call, settled.** Retail's mechanism is a per-bone name-matched **copy** of the
wearer's world matrix, with unmatched bones running the weapon's own evaluated clip. The uniform
bake lane, the frame-0 reference pose, the material contract and the binding metadata are owned by
`docs/architecture/wielded-weapon-integration.md`; `export bundle items` + `export wield` deliver
the assets and `/ElysiumBaked/Items/DA_WieldModels`. This rung consumes them:

- **Every held weapon** attaches as a `USkeletalMeshComponent` bound by `SetLeaderPoseComponent` —
  the garment recipe `ElysiumNpcVisual::InstallGarment` already ships. Name matching hands a melee
  weapon its prop bone (the wearer's own attack clips swing it — up to 179.99° on `gerber`, 21.4 in
  on `bush hook`), a firearm rides the hand, and a bone the wearer lacks holds the frame-0 reference
  pose — retail's own composition for a clip-constant sub-rig. No socket assets, no per-mode
  branching, no correction factors: a placement that needs one means the bake is wrong upstream.
- **`changball` and `gio_spirit`** (`item_w_chang_energy_ball` / `item_w_chang_ghost`) are thrown
  projectiles — free-standing actors playing their own clips, never attached.
- Two enhancements stay optional, keyed on manifest data the assets already carry: a static-mesh
  swap for the clip-constant majority if profiling ever demands it (worst measured map:
  `la_bradbury_2`, 70 armed NPCs), and a small blend graph to restore `w_m_lockpick`'s own 6.357°
  pick wiggle, the corpus's only visible own-motion. Neither touches the pipeline.

**Prerequisite, load-bearing rather than a nicety:** the character bake must retain the seven
zero-weight prop bones **and their animation channels**. Nothing skins to them, so a bake that drops
unweighted bones removes the attachment target and the swing together — and the failure is silent at
idle, appearing only once a melee attack plays.

The runtime work:

- parse `wieldmodel_m` / `wieldmodel_f` and `anim_prefix` onto `FElysiumItemDef`
  (`Substrate/ElysiumRulebook.cpp` parses `playermodel` / `viewmodel` / `infomodel` and stops there);
- resolve the row for the wielder's sex and attach on equip, detach on holster, through the existing
  funnels — `FElysiumWeapon::OnEquipped` / `OnHolstered` and `FElysiumInventory::SetActiveWeapon`.
  Do not add a second equip path;
- consume `FElysiumCameraView::bDrawWorldWeapon`, which the presentation publisher already fills
  (`UI/ElysiumPresentationSubsystem.cpp`) and which today has **no reader**. Suppress submission
  only — never destroy the attachment or clear a model to hide it;
- serve NPC bodies and the player body through one path. A drawn weapon on a cast member is the same
  operation, and the NPC loadout already selects weapons.

**Two things this rung must not do.** It must not reach for `playermodel` — that is the loose ground
model and is a different mesh. And it must not invent a socket, an offset or a hand-authored
transform: the bind poses agree by construction, so a placement that needs a correction factor means
the prop bone or the local frame is wrong, and the fix is upstream in the bake.

**Inherited unknowns, none of which gate the ordinary case.** No separate holster path exists in the
recovered equip/detach pair, so a holstered-but-carried state has no known retail mechanism;
`item_g_stake` nulls both wield models and its visual is most likely the staking execution's, not a
carried weapon's; and what drives `w_{m,f}_handleclaws`' locomotion clips to track its wearer is
untraced, which reaches only the Chang encounter. Each is named in the owning doc with the evidence
that would close it. `w_f_bushhook.mdl`'s degenerate bind is reproduced as authored. A weapon whose
row resolves to a null
model is a legitimate no-geometry answer and reports as one — it is never a missing-asset warning.

NPCs and the player share one path, and the corpus says so: equip selects on the wielder through
`IsMale` with no player-only branch, and the camera gate never applies to an NPC-owned weapon.

*Acceptance:* from real input in the arena, drawing each of the six wield-model melee weapons puts
its geometry in the correct hand on both a male and a female body; the weapon tracks the hand through
locomotion and through a swing with no separate drive call; holstering removes it; the first-person
view suppresses it and returning to third restores it without a rebuild. The pose is correct in the
Content Browser preview, not only in our runtime. *Deps:* the baked wield corpus and
`DA_WieldModels` (`export wield`); CCC10's layer for the swing that moves it; ANM4b supplies the
real per-weapon activity translation, which this rung does not need in order to be visible.

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
