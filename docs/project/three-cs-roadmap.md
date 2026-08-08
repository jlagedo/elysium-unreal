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
| Character math | 1 | `4.7 [x]` — ported line-by-line, `Elysium.Substrate.Movement` green. Its **speed authority is open**, and its world half — `StepMove`, `CategorizePosition`, the jump against real geometry — is asserted nowhere; the gym is the unfinished half of the rung |
| Camera | 2 | `11.7 [x]` faithful evaluator; the modern service and rig are `CCC2` |
| Character ↔ camera co-tune | 3 | **absent** — `CCC3` creates it, deliberately after `CCC7` settles the speed |
| Controls polish | 4 | plumbing done (`11.5 [x]`, `11.6 [x]`); the feel half is `CCC3` |
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

## The frame the slice builds

The rungs are stages of one ordered per-frame pipeline, and the order is part of the contract:

> input → `FElysiumUserCmd` → substrate thinks → mover solve (N stepper substeps) → **the body
> sample** (`CCC1`) → **intent → resolver → selection record** (`CCC4`) → **graph parameters →
> anim instance update** (`CCC5`) → camera solve (already correctly last, in `CalcCamera`) →
> presentation.

Three ordering facts are load-bearing. The sample is published at the mover's tick tail, after the
last substep, so no consumer reads a half-integrated frame. The player mesh ticks after the mover —
a tick prerequisite installed where the visual is attached — and `Elysium.Substrate.FrameOrder`
grows one assertion per rung as each stage lands, so the order is asserted rather than assumed. And
the selection feeds `GetMaxSpeed` one frame stale, which is retail's own shape — `PreThink` reads
the *currently playing* sequence, the previous frame's choice — so that latency is faithful, not a
defect to engineer away.

## Phases — two lanes off one gate

Numbered for dependency, not for date — and the numbers are not the order. `CCC0` gates everything
because no later rung can be proven without it. After it, the work runs as **two parallel lanes**:
the camera lane and the animation lane share no dependency — nothing between the body sample and
the green room reads the camera — so neither waits on the other, and a stall in one lane (a retail
trace, a bake) is a context switch rather than a slice stall.

| Lane A — the camera platform | Lane B — the animation spine |
|---|---|
| `CCC2` — the service foundation, then the modern rig | `CCC1` — the body sample |
| `CCC3` — the response curve and the leniency courses | `CCC4` — intent, resolver, record |
| | `CCC5` — the player graph |
| | `CCC6` — drive it |

The lanes join at `CCC7` (the speed authority), the co-tune half of `CCC3`, and `CCC8`; `CCC9`
retires what they replaced.

- [ ] **CCC0 The instrument.** A code-built gym generated from `ElysiumMove`'s own constants, so a
  riser, ledge, slope or ceiling brackets the threshold it tests and cannot drift from the spec.
  The generator follows the module's own pure-rules/engine-half split (the `ElysiumMoveSolve.h` /
  `ElysiumCameraSolve.h` pattern): a pure spec — constants in, placement list out — asserted in the
  Substrate tier with no world, and a dumb spawner shared by the green room's stage (whose panels
  are spawned already but carry no collision) and the `-ElysiumMove` harness; behaviour on that
  geometry is what the headless run asserts. Channels consolidate the same way: **one named-channel
  recorder** rather than a format per harness, each run emitting a channel manifest, and
  `move_diff.py` generalized into a channel differ chained into `uv run elysium debug move` the way
  `profile_report` is chained into `debug profile` — a differ that refuses an unknown or missing
  channel, so registering a channel *is* registering its comparison, and a camera regression rides
  the same deterministic runs as a movement one. Camera and animation channels join when their
  producers land (`CCC2`, `CCC1`/`CCC4`), into this recorder and never a second format.
  **Baselines are committed recordings, never regenerated expectations** — an expectation rebuilt
  from the same constants as the geometry moves with the geometry, and a gym that regenerates both
  can never turn red. The sited `sp_tutorial_1` course coordinates are surveyed here — the course
  table marks them placeholders in code. Design: `docs/architecture/movement-architecture.md`;
  harness: `docs/architecture/debug-tooling.md`.
  *Acceptance:* the speed-invariant thresholds above are baselined as committed recordings; moving
  a constant in `ElysiumMove` turns exactly the bracket that constant owns red; the differ fails on
  an unregistered or missing channel; the PC body stands **on** the gym rather than above it.
  *Deps:* none — `4.7`'s harness and the stage both exist.

- [ ] **CCC1 Publish the body sample.** `UElysiumMovementComponent` holds every input a graph wants
  and hands them to nothing: the post-solve state is private members behind scalar getters, and
  only the harness reads them. `FElysiumLocomotionSample`
  (`docs/architecture/animation-architecture.md` §3.2) is a POD struct publishing local planar
  velocity, speed, facing yaw, `move_yaw`, ground/air/water, stance and jump phase — the jump
  phase is already carried as the hold window plus the vertical velocity's sign — written once at
  the mover's tick tail, after the last stepper substep, where the state is settled. Consumers read
  it by the standard split: a game-thread copy in `NativeUpdateAnimation`, latched into the proxy
  in `PreUpdate`, so a worker thread only ever sees the copy. **Two yaw channels are recorded from
  the first day** — the wish-direction yaw and the velocity yaw — because `CCC7`'s sign recovery
  compares the retail selector's input against both, and the recordings should exist before the
  question is asked. **The NPC motor fills the same struct**; one contract, two producers, so the
  cast's locomotion and the player's cannot become two systems that happen to play the same files.
  *Acceptance:* post-solve speed, `move_yaw`, ground state and stance are readable from one struct
  for both producers; both yaw channels are registered in the differ; the Cog Npc window shows both
  producers. *Deps:* none.

- [ ] **CCC2 Camera service foundation and the player rig.** The re-architecture the camera needs:
  the shipping component is one class carrying the weights, the boom, the orbit state, the shot
  stack and the command surface, and no object owns the final view alone. The modern half lands on
  the engine's own machinery rather than beside it. **Stage one, the foundation:**
  `AElysiumPlayerCameraManager` owns the one final view per local player in `UpdateViewTarget`; the
  design's six post-layer stages are `UCameraModifier` subclasses — ordered priority, alpha in/out,
  per-modifier disable and `showdebug camera` arrive with the base class instead of being
  hand-rolled; base-request arbitration lives in the manager; `UElysiumCameraComponent` survives
  unmodified as the faithful evaluator whose output enters as a request; and first/third person are
  two persistent player choices that a weapon, dialogue or cutscene never overwrites. The A/B has a
  name: `elysium.ModernCamera` picks which rig's output is the base request while **both** evaluate
  and both record channels every frame, so one gym run diffs the two booms directly — which is also
  the co-tune's instrument. **Stage two, the modern rig:** the boom component, its collision sweep
  and its damper, asserted per channel against the faithful evaluator's recorded output rather than
  against recollection. **Explicitly out:** the prop/trigger/Python/VCD/Sequencer adapters — they
  are `11.13d`–`11.13g`'s clients and plug into the service later without reopening it. This lane
  gates only the co-tune and `CCC8`'s framed-by-both-modes claim, never the animation lane.

  **Four corrections to the design doc land inside this rung, before it is built from.** Its
  priority table has `LegacyShot`/`LegacyTrack` competing as base requests, but retail composes
  them *over* the third-person weight and the shipping `ApplyToView` already does — they are post
  layers or the change is a stated divergence. Its removal of weapon-class arbitration rests on bit
  meanings `docs/vtmb/camera-view-modes.md` still lists as unrecovered, and classes `0x08`/`0x10`
  are pinned rather than preferences, so the divergence must first establish that neither carries
  logic. Its A/B mechanism is the one named above. And it adds a profile plus a user-settings
  surface beside the existing 27-cvar surface without saying which wins.

  Two facts carry over from the code. `CalcCamera` must delegate to
  `UCameraComponent::GetCameraView` first — the documented first-person-rendering trap — and the
  manager inherits that duty with the view. And `FElysiumMoveStepper::Alpha()` is the view
  interpolation hook nothing consumes; if `elysium.move.FixedStep` ever defaults on, the rig is its
  consumer. Design and its A/B boundary: `docs/architecture/camera-architecture.md`; recovered
  behaviour: `docs/vtmb/camera-view-modes.md`.
  *Acceptance:* both modes persist across a scoped override and restore the exact prior view; the
  theatre's `12.1` camera acceptance still passes; `elysium.ModernCamera` swaps the base request
  live with both rigs' channels recorded in one run; boom length, clip state and damper position
  assert against `CCC0`'s recorded faithful baseline. *Deps:* `CCC0`; `11.5 [x]`, `11.7 [x]`,
  `11.8 [x]`, `12.1`'s verified sampler.

- [ ] **CCC3 Controls response, and the co-tune that waits.** The feel half of input, which no
  document tracks — `docs/architecture/input-architecture.md` records look-curve tuning as "a
  Feel-axis decision with no owner yet" and has no `## Feel` section. Two questions land now. The
  **response curve** between look input and view rotation, against retail's `sensitivity` ×
  `m_yaw`/`m_pitch` path — applied where `FElysiumUserCmd` is built, as one pure function in the
  `ElysiumInputScope.h` style rather than as Enhanced Input modifier assets, so the curve is
  asserted in the Substrate tier and A/B-able against the decompile instead of scattered across
  data assets. And **leniency**: VtMB has no input buffer and no coyote time — no accumulator, no
  press queue, and `CheckJumpButton` returns immediately when not grounded, with
  `CategorizePosition`'s 2-unit down-trace the only forgiveness in the system. Adding either is
  therefore a divergence under the charter's Feel layer, not a defect fix. The third question, the
  **character ↔ camera co-tune**, deliberately waits for `CCC7`: hours tuned against a walk speed
  the speed authority is about to halve are spent twice, and the baseline split protects assertions
  but not taste.
  *Acceptance:* the response curve is either read off retail or named in the code as a divergence
  at the point it applies; a jump-at-the-lip and a jump-buffered-before-landing case exist as
  recorded courses whether or not leniency is added, so the decision is measured rather than
  argued; the co-tune is performed once, after `CCC7`, with both rigs' channels in one run.
  *Deps:* `CCC0`, `CCC2` for the curve and the courses; `CCC7` for the co-tune.

- [ ] **CCC4 Intent, resolver, selection record.** `FElysiumAnimationIntent` in,
  `FElysiumAnimationSelection` out, over steps 2, 4, 5 and 6 of
  `docs/architecture/animation-architecture.md` §3.3 — there is no channel to arbitrate and no
  weapon to translate through yet, and both seams exist rather than being stubbed away. Most of the
  resolution already exists and is reachable: `UElysiumNpcAnimSubsystem::ResolveActivityClip`
  returns the vocabulary label, the concrete animation and the cell's authored ground speed, and
  `ResolveGrid` returns the baked blend space with its axis bindings. What is missing is the caller
  and the record. **The caller is one shared function** — sample and catalog in, selection out —
  invoked by the player path post-solve and by the NPC motor from its own tick: one contract, two
  producers, and the intent builder is pure, so it is asserted against a hand-built catalog in the
  Substrate tier. The front door lands on the existing subsystem; the physical split and rename of
  the catalog out of its NPC-named host waits for `CCC9`, because a big-bang split churns a
  subsystem five systems read. The record is not decoration — six things can produce a wrong pose,
  and without one line naming each, a wrong pose is a guess.
  *Acceptance:* a caller hands the resolver an intent built from `CCC1`'s sample and gets back an
  asset plus a record naming every step; the record is on screen in Cog and in the headless run's
  JSON; **and the player and the cast resolve to their own banks** — `ACT_RUN` reaches the PC-only
  bank for the player and the cast bank for an NPC (`docs/vtmb/animation_and_movers.md`), asserted
  for both producers, because a resolver keyed on the label alone hands the player the cast's
  gait. *Deps:* `CCC1`.

- [ ] **CCC5 The player animation graph.** `ABP_ElysiumBiped`: a locomotion state machine whose
  transitions come from the authored `fade` duration, blend-space players on walk, run and sneak
  driven by `move_yaw`, and a slot for one-shots. **One parameterized graph, not one per
  archetype:** every player node takes its asset dynamically — through the anim-node-function
  library setters or an exposed asset pin bound to an instance property the native update writes —
  which is the design's own step 6 (no re-selection in the graph) made structural. That choice
  serves two masters at once: one graph plays every model the resolver picks assets for, and the
  committed `.uasset` under `Content/ElysiumAuthored/` carries zero references to generated
  content, so the authored/generated boundary holds by construction. A per-archetype graph is
  reserved for a skeleton that genuinely diverges, not for asset differences, which are catalog
  data. The native base is `UElysiumBipedAnimInstance`, whose proxy overrides `Evaluate` — the
  compiled graph runs, then the composition tail over the output pose, the same shape the NPC
  proxy uses — so the custom nodes need no graph-editor wrapper module, and the standalone node
  variants stay the NPC path's concern.

  **The portrait stack migrates with the instance or it regresses silently.** A player body
  carries eyeballs and axis-interpolation rules and no flex rig; the eye input seam and the
  axis-interp/cloth tail are shared with the NPC instance rather than duplicated or dropped,
  because the failure mode — frozen eyes, untwisted forearms — logs nothing and the Content
  Browser preview cannot show it.

  One-shots ride dynamic slot montages over the baked sequences — no montage assets to bake. The
  jump is **states, not montages**: ascend while the hold window is open or vertical velocity is
  positive, descend airborne, land on the ground transition; `leap_ascend`'s authored 0.45 s fade
  against a tap jump's entire ascent is the divergence table's row and lands in the transition
  durations. Transition parity is a Content-tier test: retail combines a pair as
  `max(outgoing, incoming)` and ships 0.2 s on nearly the whole vocabulary
  (`docs/vtmb/animation_and_movers.md`), so the six transitions assert against the authored table
  rather than being reviewed. Sync-group phase matching between gaits is a Feel modernization —
  retail's crossfades are phase-independent — recorded in the divergence table and off by default.

  The six activities are `ACT_IDLE`, `ACT_WALK`, `ACT_RUN`, `ACT_SNEAK`, `ACT_CROUCH` and the
  `ACT_LEAP_ASCEND`/`_DESCEND`/`ACT_LAND` chain; none is masked and none is additive, so none needs
  the derived `<label>@<host>` naming, which is why the slice can be cut here. No aim node, no
  layered blend, no additive node — those arrive with the weapon rung. The two open questions ship
  **provisional answers** rather than stalling on `ANM4a`'s retail trace: a non-looping player
  holds its final frame, which is the held crouch until the trace answers what retail does once
  ducked; and the jump family is `leap_*`, because `hop` is a two-frame stub. Both are marked
  provisional and the trace confirms or corrects them.
  *Acceptance:* the six activities are reachable through the graph; the transitions assert against
  the authored fades in the Content tier; the eyes track and the forearms twist on the migrated
  body; `elysium.BlendSpaces 0` still A/Bs against the single resolved cell.
  *Deps:* `CCC4`; **`docs/project/animation-roadmap.md` ANM1** — an Animation Blueprint compiles
  against a native `USkeleton`, so the shared-skeleton bake gates this rung.

- [ ] **CCC6 Drive it — the green room on the gym floor.** A construction rung, not a wiring rung:
  the green room detaches the player visual from the pawn and re-parents it to the stage root —
  its pin writes the model alpha and the control rotation and nothing else — so "the stage body is
  the player body" is this rung's work, not an existing property. The attachment the player-visual
  builder makes stays made, the pawn spawns on the gym floor, the pawn drives, the visual follows,
  and the orbit camera watches from outside. No second driver, and the thing under test is the
  shipping path rather than a lab replica of it. The panel gains a locomotion readout beside the
  clip list — the sample, the selection record, the live `move_yaw`, and the machine's state.
  *Acceptance:* `uv run elysium gr tremere_male_armor_0` stands the PC body on the gym, WASD walks
  and runs it, Shift changes the gait, Ctrl crouches it, Space jumps it, and the pose follows
  without a clip being named by hand. *Deps:* `CCC0`, `CCC5`.

- [ ] **CCC7 `move_yaw` and the speed authority.** The rung where the ladder inverts — and the
  plumbing is shorter than the question. `ResolveActivityClip` already returns the authored cell
  speed; the selection record carries it; `GetMaxSpeed` reads it behind
  `elysium.move.AnimSpeedAuthority`, with the ConVar constants as the A/B. The selection is one
  frame stale at the read, which is retail's own shape and stays. **The sign** of `move_yaw` is
  recoverable and must be recovered rather than tuned: the retail selector at `0x10164870` writes
  it before choosing an activity, so what it differences is the answer — compared against both of
  `CCC1`'s recorded yaw channels, wish and velocity, so the comparison runs against recordings
  rather than re-instrumentation; a mirrored convention reproduces the same per-cell speeds, so a
  green-room A/B confirms a decision and cannot make one. **The real new scope is the blend-space
  case:** a strafing body's commanded speed is the *interpolated* cell speed at the current
  `move_yaw`, so `FElysiumResolvedGrid` exposes the per-cell speed table beside its axis bindings —
  that interpolation is what stops a sideways run authored at a different speed foot-sliding, and
  it is a real feel change under the charter's Feel layer. Root motion is not a third question —
  the exporter leaves the skeletal root in place and carries per-cell displacement as metadata, so
  the clips hold the root and the motor translates; what is open is only which speed the motor
  reads.
  *Acceptance:* the sign is read off the selector rather than chosen; the speed source is either
  switched to the authored cell — interpolated across the fan — or named in the code as a
  divergence at the point `GetMaxSpeed` answers; and the speed-dependent gym baselines are promoted
  for the first time, with the speed-invariant ones still green. *Deps:* `CCC6`.

- [ ] **CCC8 Played acceptance.** The slice's finish line, beat-scripted in the Play tier so the
  claim is a CI run rather than a recollection — playable-path rule 3. The Play tier itself is
  `11.10`, the largest unbuilt dependency in this slice's graph, and it is named here as the gate
  it is; if it has not landed when the lanes join, the fallback is one minimal beat script
  sufficient for exactly this acceptance, generalized into the tier later rather than holding the
  slice on the full harness.
  *Acceptance:* the PC body walks, runs, sneaks, crouches and jumps around the gym from real input,
  framed by both camera modes, with `move_yaw` written every frame and the stride following the
  strafe continuously; every threshold the gym brackets holds where `docs/vtmb/source_movement.md`
  says it should; and the pose is correct in the Content Browser preview and the anim editor, not
  only in our runtime. *Deps:* `CCC7`, the co-tune half of `CCC3`, `11.10` (or its scoped minimal
  beat).

- [ ] **CCC9 Retire the scaffolding.** `FElysiumNpcAnimProxy` composes by hand what the graph now
  owns: a four-slot sequence-player pool with an age-evicting fade list, plus the base grid slot —
  a larger cut than a single crossfade. Only the parts this slice replaced go: the two layer slots
  survive until the aim node lands, and the cinematic seek path — `Seek`/`ResyncPosition`, the
  substrate-clock phase lock a montage cannot replicate — survives until `ANM6` migrates the
  theatre, which is last on purpose. The blend profiles the proxy reads are the same assets
  `FAnimNode_LayeredBoneBlend` consumes unchanged, so nothing is re-baked. This is also where the
  catalog and resolver are renamed out of their NPC-named host — once the proxy's callers are gone
  and the front door `CCC4` opened is the only door.
  *Acceptance:* the slice's bodies run through `ABP_ElysiumBiped`, the proxy's fade pool and grid
  slot are gone, the resolver's name matches what it serves, and the theatre is still on its
  verified seek path. *Deps:* `CCC8`.

**Past the first slice's finish line**, and tracked here because they reach the body through the same
seam rather than a second one:

- [ ] **CCC10 The weapon rung — layered, additive and aim nodes.** The graph nodes `CCC5`
  deliberately omits: `FAnimNode_LayeredBoneBlend` over the baked blend profiles, additive nodes for
  the `_delta` family over the base each asset names, and an aim-offset node for the upper-body
  overlay family. The 3×3 aim grids bake once per declaring host but cannot be stood at all today,
  because a masked grid evaluated as a *base* pose loses the body's stance from the waist down; in a
  graph the mask is a property of the blend node rather than of the pose feeding it, which is why no
  layered blend-space path exists in the proxy and building one there would be building it wrong.
  Every cell of an aim grid shares one bone mask — measured, not assumed — so a whole grid sits
  behind one node, **and the node is named**: not an aim-offset asset, which requires mesh-space
  additive samples the baked grids are not, but a layered bone blend — consuming the baked blend
  profiles unchanged — over a plain blend-space player. This rung also retires the proxy's two
  layer slots and `elysium.AnimLayers`, and un-collapses a stated simplification: gesture and
  sequence share one clip slot today, so a scene's gesture overwrites its sequence instead of
  layering over it.
  *Acceptance:* an aim grid stands as a **layer** over a moving host, torso upright, through a node
  that owns the mask; a gesture layers over a sequence rather than replacing it.
  *Deps:* `CCC5`; `docs/project/animation-roadmap.md` ANM1 and ANM2's binding half.

- [ ] **CCC11 The action families beyond locomotion.** The acceptance ladder past the gait:
  directional hit, knockback and death reactions, then weapon and interaction actions. Each rung
  closes its own reachability slice — full combat coverage does not block walking, but an unexplained
  action inside an accepted rung does. This is also where every remaining producer moves onto the
  intent seam: no NPC, weapon or script path asks for an activity through the resolver today, and
  `PlayNpcActivity` stays a compatibility adapter until patrol and scripted travel cross over — the
  intent's source field already reserves the producers, so expansion is migration order, not new
  mechanism. Two shapes build together because they are the same shape — a `scripted_sequence`'s
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
command, the gym spec or the camera weights are `Elysium.Substrate.*`; anything reading an exported
clip, grid or input asset is `Elysium.Content.*`; anything needing a built world with real geometry
is what `-ElysiumMove` exists to turn into a per-change headless run.

## Risks

- **The speed authority moves the floor under every earlier measurement.** `CCC7` can halve the walk
  speed. The speed-invariant/speed-dependent split above is the mitigation, and it only works if the
  two baseline sets are actually kept apart — promoting a course time before `CCC7` bakes in a number
  that is about to change. The co-tune waits for the same reason: the split protects assertions, not
  tuning hours.
- **The camera is the C with no instrument.** The tested half of the camera is its bookkeeping and
  its helpers — the approach, the spline, the model-alpha band are asserted; the four-stage boom
  solve, the collision sweep, the damper and `SolveViewRoll` have no assertions as a composition.
  The evaluator is not retro-unit-tested: it becomes the recorded reference `CCC0`'s channels
  capture, and the modern rig is what asserts against it.
- **A greenfield camera plan over a working camera.** `docs/architecture/camera-architecture.md`
  describes ~2,700 lines of tested, RE-faithful code in one sentence. `CCC2` corrects the doc before
  building from it; skipping that step silently converts retail's composition model into a
  winner-takes-all stack.
- **The portrait stack can regress without a log line.** The eye seam and the axis-interp/cloth tail
  live on the NPC anim instance today; a player graph that does not carry them ships frozen eyes and
  untwisted forearms, and neither the logs nor the Content Browser preview can show it. `CCC5` owns
  the migration and asserts it.
- **`CCC8` is gated on `11.10`.** The Play tier is tracked in the master roadmap and open; the
  scoped fallback in `CCC8` exists so the slice's finish line is not hostage to the full harness.
- **The theatre is the thing to protect.** Same risk the animation programme carries: the slice's
  graph shares an animation instance family with choreographed playback. `CCC9` retires only what it
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
| Sync-group phase matching between gaits in the player graph — retail's crossfades are phase-independent; off by default | `docs/architecture/animation-architecture.md` |
| Input leniency of any kind — buffering, coyote time, a look-response curve that is not retail's. VtMB has none of these | `docs/architecture/input-architecture.md` |
| A fixed-step accumulator, shipped behind `elysium.move.FixedStep` with the faithful variable delta as the default | `docs/vtmb/source_movement.md` |
| Composing the legacy shot and track channels as post layers, or arbitrating them as base requests against the third-person weight — whichever `CCC2` chooses | `docs/architecture/camera-architecture.md` |

**Not divergences**, and recorded as defect fixes rather than choices: the box hull that `StepMove`
requires against `ACharacter`'s capsule, and keeping masked sequences out of the base clip path.
