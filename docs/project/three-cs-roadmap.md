# 3 C's Roadmap — Character, Camera, Controls

## Goal and ownership

This tracker drives one outcome and the work that reaches it:

> **A real character moving in a purpose-built map — walking, running, sneaking, crouching and
> jumping from real input, framed by both camera modes, animating from the movement command stream
> with no clip named by hand.**

It owns the **player-feel vertical**: the mover's published body state, the intent and resolver that
turn it into a pose request, the player animation graph, the camera service and player rig, the
response curve between input and body, the gym and channels that measure all of it, and the played
acceptance that closes it.

**It does not own:** the character asset bake, shared skeleton, layer masks or blend spaces
(`docs/project/animation-roadmap.md` ANM1–ANM3); the generated action corpus and its RE
(`ANM4a`/`ANM4b`, `RE37`); the cinematic playback migration (`ANM6`); the dialogue director, prop
focus, Sequencer bridge or the camera options surface (`docs/project/roadmap.md` `11.13d`–`11.13g`);
the Enhanced Input action catalog, remapping screen or `config.cfg` projection (`10.6`); or any VtMB
format fact. Facts belong in the owning `docs/vtmb/` topic, the Unreal design in
`docs/architecture/movement-architecture.md`, `docs/architecture/camera-architecture.md`,
`docs/architecture/input-architecture.md` and `docs/architecture/animation-architecture.md`, and
project priority and roll-up status in `docs/project/roadmap.md`. This file owns detailed status and
task order for the slice, and nothing else.

`docs/project/roadmap.md`, `docs/project/animation-roadmap.md` and
`docs/project/retail-capture-roadmap.md` are this tracker's siblings. The master retains the parent
rows `4.7`, `8.11b`, `11.13` and `10.6`.

## The ladder

The industry order for the three C's is character math → camera → co-tune → controls polish →
capability slices → animation → vertical slice, with animation deliberately last so a skinned body
cannot flatter a bad input curve. **This project cannot run that order unmodified, and the reason is
recovered rather than stylistic.**

VtMB's `CHL2_Player::PreThink` sets `m_flMaxSpeed` from the **current sequence's own root motion**
(`docs/vtmb/source_movement.md` → "Player speed is animation-driven"). The animation is the
movement's speed authority. Our mover instead reads `ElysiumMove::WalkSpeed`/`RunSpeed` — Troika's
stated 100/225 u/s, which are dead ConVars in the retail build — and the authored cells disagree by
roughly half (`walk_0` is 53.8 u/s). So the Character rung cannot be closed before the animation
rung: `CCC7` re-opens what `4.7` settled, by design.

| Rung | Canonical step | Where this project stands |
|---|---|---|
| Character math | 1 | `4.7 [x]` — ported line-by-line, `Elysium.Substrate.Movement` green. Its **speed authority is open** |
| Camera | 2 | `11.7 [x]` faithful evaluator; the modern rig is `CCC2` |
| Character ↔ camera co-tune | 3 | **absent** — `CCC3` creates it |
| Controls polish | 4 | plumbing done (`11.5 [x]`, `11.6 [x]`); the **feel half is untracked** — `CCC3` |
| Capability slices | 5 | the jump chain, inside `CCC5` |
| Real animation | 6 | `CCC4`–`CCC6` |
| Vertical slice | 7 | `CCC8` |

**The gym splits its assertions by whether `CCC7` can move them**, which is what lets the Character
rung be partly closed now despite the inversion:

| Speed-invariant — baseline at `CCC0`, permanently | Speed-dependent — baseline only after `CCC7` |
|---|---|
| the 18u step cliff, bracketed 16/17/**18**/19/20/24 | horizontal gap clearance |
| the 25u `sv_jump_boost` pop and the 43u crouch-jump ceiling | every course completion time |
| slope standability across the 0.7 normal (44/45/46/50°) | air-strafe speed gain |
| unduck refusal under a low ceiling, grounded and airborne | friction stopping distance |

A vertical threshold that moves when the speed source changes is a collision regression, not a feel
delta. That distinction is the whole reason the gym is generated from the constants rather than
authored.

## Phases

Numbered for dependency, not for date. `CCC0` gates everything because no later rung can be proven
without it.

- [ ] **CCC0 The instrument.** A code-built gym generated from `ElysiumMove`'s own constants, so a
  riser, ledge, slope or ceiling brackets the threshold it tests and cannot drift from the spec; it
  extends the green room's existing stage, whose panels are spawned already but carry no collision.
  Camera and animation channels join the `-ElysiumMove` per-frame record so a camera regression
  rides the same deterministic runs as a movement one, and `move_diff.py` is chained into
  `uv run elysium debug move` the way `profile_report` is chained into `debug profile`. Design:
  `docs/architecture/movement-architecture.md`; harness: `docs/architecture/debug-tooling.md`.
  *Acceptance:* the speed-invariant thresholds above are baselined, and moving a constant in
  `ElysiumMove` turns exactly the bracket that constant owns red; the new channels are registered in
  the comparator rather than written and silently never compared; the PC body stands **on** the gym
  rather than above it. *Deps:* none — `4.7`'s harness and the stage both exist.

- [ ] **CCC1 Publish the body sample.** `UElysiumMovementComponent` holds every input a graph wants
  and hands them to nothing: only the harness reads the mover today. `FElysiumLocomotionSample`
  (`docs/architecture/animation-architecture.md` §3.2) publishes local planar velocity, speed,
  facing yaw, `move_yaw`, ground/air/water, stance and jump phase from the post-solve state, in
  `PostMoveTick`, where the camera director and eye tick already run — so no consumer reads a
  half-integrated frame. **The NPC motor fills the same struct**; one contract, two producers, so the
  cast's locomotion and the player's cannot become two systems that happen to play the same files.
  *Acceptance:* post-solve speed, `move_yaw`, ground state and stance are readable from one struct
  for both producers, and the Cog Npc window shows both. *Deps:* none.

- [ ] **CCC2 Camera service foundation and the player rig.** `AElysiumPlayerCameraManager`, the
  local-player `UElysiumCameraService`, value requests with generation and map-epoch handles, the
  modern rig, and first/third person as two persistent player choices that a weapon, dialogue or
  cutscene never overwrites. The existing shot stack and camera-track sampler are wrapped as legacy
  requests before their output changes, and the faithful Hooke evaluator stays as a developer A/B on
  the `elysium.SourceMovement 0` → `AElysiumCapsulePawn` pattern. Design and its A/B boundary:
  `docs/architecture/camera-architecture.md`; recovered behaviour: `docs/vtmb/camera-view-modes.md`.

  **Four corrections to the design doc land inside this rung, before it is built from.** Its priority
  table has `LegacyShot`/`LegacyTrack` competing as base requests, but retail composes them *over*
  the third-person weight and the shipping `ApplyToView` already does — they are post layers or the
  change is a stated divergence. Its removal of weapon-class arbitration rests on bit meanings
  `docs/vtmb/camera-view-modes.md` still lists as unrecovered, and classes `0x08`/`0x10` are pinned
  rather than preferences, so the divergence must first establish that neither carries logic. It
  names no A/B mechanism for the evaluator it retains. And it adds a profile plus a user-settings
  surface beside the existing 27-cvar surface without saying which wins.

  *Acceptance:* both modes persist across a scoped override and restore the exact prior view; the
  theatre's `12.1` camera acceptance still passes; the faithful evaluator stands as an A/B; and boom
  length, clip state and damper position assert against `CCC0`'s channels rather than against
  recollection. *Deps:* `CCC0`; `11.5 [x]`, `11.7 [x]`, `11.8 [x]`, `12.1`'s verified sampler.

- [ ] **CCC3 Controls response and the character↔camera co-tune.** The feel half of input, which no
  document tracks today — `docs/architecture/input-architecture.md` records look-curve tuning as "a
  Feel-axis decision with no owner yet" and has no `## Feel` section. Two questions. The **response
  curve** between look input and view rotation, against retail's `sensitivity` × `m_yaw`/`m_pitch`
  path. And **leniency**: VtMB has no input buffer and no coyote time — no accumulator, no press
  queue, and `CheckJumpButton` returns immediately when not grounded, with `CategorizePosition`'s
  2-unit down-trace the only forgiveness in the system. Adding either is therefore a divergence
  under the charter's Feel layer, not a defect fix.
  *Acceptance:* the response curve is either read off retail or named in the code as a divergence at
  the point it applies; a jump-at-the-lip and a jump-buffered-before-landing case exist as recorded
  courses whether or not leniency is added, so the decision is measured rather than argued.
  *Deps:* `CCC0`, `CCC2`.

- [ ] **CCC4 Intent, resolver, selection record.** `FElysiumAnimationIntent` in,
  `FElysiumAnimationSelection` out, over steps 2, 4, 5 and 6 of
  `docs/architecture/animation-architecture.md` §3.3 — there is no channel to arbitrate and no weapon
  to translate through yet, and both seams exist rather than being stubbed away. Most of the
  resolution already exists and is reachable: `UElysiumNpcAnimSubsystem::ResolveActivityClip` returns
  the vocabulary label, the concrete animation and the cell's authored ground speed, and
  `ResolveGrid` returns the baked blend space with its axis bindings. What is missing is the caller
  and the record. The record is not decoration — six things can produce a wrong pose, and without one
  line naming each, a wrong pose is a guess.
  *Acceptance:* a caller hands the resolver an intent built from `CCC1`'s sample and gets back an
  asset plus a record naming every step; the record is on screen in Cog and in the headless run's
  JSON. *Deps:* `CCC1`.

- [ ] **CCC5 The player animation graph.** `ABP_ElysiumBiped`, the first of the per-archetype
  Animation Blueprints (`docs/architecture/animation-architecture.md` §4): a locomotion state machine
  whose transitions come from the authored `fade` duration, `FAnimNode_BlendSpacePlayer` on walk, run
  and sneak driven by `move_yaw`, and a montage slot for the jump chain. No aim node, no layered
  blend, no additive node — those arrive with the weapon rung and need work this slice deliberately
  does not do. The six activities are `ACT_IDLE`, `ACT_WALK`, `ACT_RUN`, `ACT_SNEAK`, `ACT_CROUCH`
  and the `ACT_LEAP_ASCEND`/`_DESCEND`/`ACT_LAND` chain; none is masked and none is additive, so none
  needs the derived `<label>@<host>` naming, which is why the slice can be cut here.

  Two open questions land in this rung. **What holds a held crouch** — `crouch` is a 61-frame
  non-looping clip and the vocabulary carries no unarmed crouched idle. **Which jump family fires and
  when** — the registry exposes both `ACT_HOP*` and `ACT_LEAP*`, `hop` is two frames, and
  `leap_ascend` is a loop because VtMB's jump is a held push with no authored length. Both are
  answered by the same controlled retail trace `ANM4a` runs.
  *Acceptance:* the six activities are reachable through the graph, the transitions come from the
  authored fades, and `elysium.BlendSpaces 0` still A/Bs against the single resolved cell.
  *Deps:* `CCC4`.

- [ ] **CCC6 Drive it — the green room on the gym floor.** The stage body **is** the player body: the
  green room already pins the player pawn and forces its model fully opaque, so the pawn drives, the
  visual follows, and the orbit camera watches from outside. No second driver, and the thing under
  test is the shipping path rather than a lab replica of it. The panel gains a locomotion readout
  beside the clip list — the sample, the selection record, the live `move_yaw`, and the machine's
  state.
  *Acceptance:* `uv run elysium gr tremere_male_armor_0` stands the PC body on the gym, WASD walks and
  runs it, Shift changes the gait, Ctrl crouches it, Space jumps it, and the pose follows without a
  clip being named by hand. *Deps:* `CCC0`, `CCC5`.

- [ ] **CCC7 `move_yaw` and the speed authority.** The rung where the ladder inverts. **The sign** of
  `move_yaw` is recoverable and must be recovered rather than tuned: the retail selector at
  `0x10164870` writes it before choosing an activity, so what it differences is the answer, and a
  mirrored convention reproduces the same per-cell speeds — a green-room A/B confirms a decision and
  cannot make one. **The speed** is a design decision as much as an RE one: closing the loop makes
  movement speed a function of the animation blend, which is the only way a sideways run and a
  forward run authored at different speeds stop being foot-sliding, and it is a real feel change
  under the charter's Feel layer. Root motion is not a third question — the exporter leaves the
  skeletal root in place and carries per-cell displacement as metadata, so the clips hold the root
  and the motor translates; what is open is only which speed the motor reads.
  *Acceptance:* the sign is read off the selector rather than chosen; the speed source is either
  switched to the authored cell or named in the code as a divergence at the point `GetMaxSpeed`
  answers; and the speed-dependent gym baselines are promoted for the first time, with the
  speed-invariant ones still green. *Deps:* `CCC6`.

- [ ] **CCC8 Played acceptance.** The slice's finish line, beat-scripted in the Play tier so the
  claim is a CI run rather than a recollection — playable-path rule 3.
  *Acceptance:* the PC body walks, runs, sneaks, crouches and jumps around the gym from real input,
  framed by both camera modes, with `move_yaw` written every frame and the stride following the
  strafe continuously; every threshold the gym brackets holds where `docs/vtmb/source_movement.md`
  says it should; and the pose is correct in the Content Browser preview and the anim editor, not
  only in our runtime. *Deps:* `CCC7`, `11.10`.

- [ ] **CCC9 Retire the scaffolding.** `FElysiumNpcAnimProxy` composes by hand what the graph now
  owns — the two-player crossfade and the base grid slot. Only the parts this slice replaced go: the
  layer slots survive until the aim node lands, and the cinematic seek path survives until `ANM6`
  migrates the theatre, which is last on purpose. The blend profiles the proxy reads are the same
  assets `FAnimNode_LayeredBoneBlend` consumes unchanged, so nothing is re-baked.
  *Acceptance:* the slice's bodies run through `ABP_ElysiumBiped`, the proxy's crossfade and grid slot
  are gone, and the theatre is still on its verified seek path. *Deps:* `CCC8`.

**Past the first slice's finish line**, and tracked here because they reach the body through the same
seam rather than a second one:

- [ ] **CCC10 The weapon rung — layered, additive and aim nodes.** The graph nodes `CCC5`
  deliberately omits: `FAnimNode_LayeredBoneBlend` over the baked blend profiles, additive nodes for
  the `_delta` family over the base each asset names, and an aim-offset node for the upper-body
  overlay family. The 3×3 aim grids bake once per declaring host but cannot be stood at all today,
  because a masked grid evaluated as a *base* pose loses the body's stance from the waist down; in a
  graph the mask is a property of the blend node rather than of the pose feeding it, which is why no
  layered blend-space path exists in the proxy and building one there would be building it wrong.
  Every cell of an aim grid shares one bone mask — measured, not assumed — so a whole grid sits behind
  one node. This rung also retires the proxy's two layer slots and `elysium.AnimLayers`, and
  un-collapses a stated simplification: gesture and sequence share one clip slot today, so a scene's
  gesture overwrites its sequence instead of layering over it.
  *Acceptance:* an aim grid stands as a **layer** over a moving host, torso upright, through a node
  that owns the mask; a gesture layers over a sequence rather than replacing it.
  *Deps:* `CCC5`; `docs/project/animation-roadmap.md` ANM1 and ANM2's binding half.

- [ ] **CCC11 The action families beyond locomotion.** The acceptance ladder past the gait:
  directional hit, knockback and death reactions, then weapon and interaction actions. Each rung
  closes its own reachability slice — full combat coverage does not block walking, but an unexplained
  action inside an accepted rung does. This is also where every remaining producer moves onto the
  intent seam: no NPC, weapon or script path asks for an activity through the resolver today, and
  `PlayNpcActivity` stays a compatibility adapter until patrol and scripted travel cross over. Two
  shapes build together because they are the same shape — a `scripted_sequence`'s
  `m_iszIdle → m_iszPlay → m_iszPostIdle`, and an interesting place's own enter/hold/leave segments,
  whose naming varies by type (`_INTO`/`_OUTOF`, `_BEGIN`/`_END`, `_PICKUP`/`_HANGUP`) and whose
  ordering comes from the three weighted lists the vdata table declares
  (`docs/vtmb/vdata-catalog.md`). One montage-slot mechanism serves both, and building them apart is
  how there come to be two.
  *Acceptance:* an NPC's ambient behaviour and a scripted beat both reach a pose through the intent
  seam rather than a direct clip call, and `m_iszCustomMove` plays over a travelling body.
  *Deps:* `CCC10`; `docs/project/animation-roadmap.md` ANM4's catalog for anything outside the
  hand-read set.

## The acceptance surface

Three instruments, and which claim each can carry.

**The gym and the courses** answer *where a threshold is*. `sp_tutorial_1` cannot: it contains no 17u
step beside an 18u and a 19u, so a real map proves the tutorial's stairs work and never proves the
step code cliffs where the decompile says. Generated geometry is the only thing that brackets a
constant.

**The sited courses on `sp_tutorial_1`** answer *whether we match retail*, and are permanently
required for it — retail will not load a gym we authored, so course-time comparison against a capture
is real-geometry-only. Their coordinates are placeholders today and must be surveyed before their
baselines mean anything; that survey is `CCC0`'s.

**The green room** answers *whether it looks right*. Locomotion, unlike pose composition, has numeric
ground truth — walk-up-18-succeeds and walk-up-19-fails is a boolean — so most of this slice asserts
in a tier rather than on a contact sheet. The green room is where the remainder lands: stride
continuity across the strafe, and whether a gait reads as the original's.

Tier placement follows the existing rule — content-free claims about the solve, the sample, the user
command or the camera weights are `Elysium.Substrate.*`; anything reading an exported clip, grid or
input asset is `Elysium.Content.*`; anything needing a built world with real geometry is what
`-ElysiumMove` exists to turn into a per-change headless run.

## Risks

- **The speed authority moves the floor under every earlier measurement.** `CCC7` can halve the walk
  speed. The speed-invariant/speed-dependent split above is the mitigation, and it only works if the
  two baseline sets are actually kept apart — promoting a course time before `CCC7` bakes in a number
  that is about to change.
- **The camera is the C with no instrument.** `SolveBoom`, the collision sweep, the damper and
  `SolveViewRoll` have no coverage at all today; the tested half of the camera is its bookkeeping.
  `CCC0`'s channels exist so `CCC2` does not add a second untested solve beside the first.
- **A greenfield camera plan over a working camera.** `docs/architecture/camera-architecture.md`
  describes ~2,700 lines of tested, RE-faithful code in one sentence. `CCC2` corrects the doc before
  building from it; skipping that step silently converts retail's composition model into a
  winner-takes-all stack.
- **The theatre is the thing to protect.** Same risk the animation programme carries: the slice's
  graph shares an animation instance with choreographed playback. `CCC9` retires only what it
  replaced, and the theatre stays on its verified seek path.
- **A gym is not a game.** Every threshold can pass while the game still feels wrong. The gym bounds
  correctness; the played acceptance at `CCC8` and the owner's judgement are what bound feel.

## Divergences to record when settled

Each is recorded beside the faithful behaviour in the topic that owns it, and marked as a divergence,
per the house rules.

| Divergence | Owning document |
|---|---|
| The player's gait speed is a constant rather than the current sequence's root motion — standing today, marked at `UElysiumMovementComponent::GetMaxSpeed`, and resolved either way by `CCC7` | `docs/architecture/movement-architecture.md` |
| Any change to movement-orientation and strafing settings made so that `move_yaw` resolves off the neutral cell — **an open owner call, not yet made** | `docs/architecture/animation-architecture.md` |
| A held crouch's hold rule, if the controlled trace cannot answer what retail does once ducked | `docs/vtmb/animation_and_movers.md` |
| Jump transition timing — `leap_ascend`'s authored 0.45 s fade is longer than a tap jump's entire ascent, so something has to give | `docs/architecture/animation-architecture.md` |
| Input leniency of any kind — buffering, coyote time, a look-response curve that is not retail's. VtMB has none of these | `docs/architecture/input-architecture.md` |
| A fixed-step accumulator, shipped behind `elysium.move.FixedStep` with the faithful variable delta as the default | `docs/vtmb/source_movement.md` |
| Composing the legacy shot and track channels as post layers, or arbitrating them as base requests against the third-person weight — whichever `CCC2` chooses | `docs/architecture/camera-architecture.md` |

**Not divergences**, and recorded as defect fixes rather than choices: the box hull that `StepMove`
requires against `ACharacter`'s capsule, and keeping masked sequences out of the base clip path.
