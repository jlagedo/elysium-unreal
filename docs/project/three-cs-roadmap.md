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

VtMB has **no scalar player gait speed at all**. `CHL2_Player::PreThink` builds six networked
per-direction speed tables from the model's own 9×1 locomotion fans, and the client writes one
cell's absolute speed into `forwardmove`/`sidemove`; `m_flMaxspeed` is the peak over all 24 cells
and serves only as a clamp ceiling (`docs/vtmb/source_movement.md` → "Player speed is
animation-driven"). The animation is the movement's speed authority, **at the input seam**. Our
mover instead reads `ElysiumMove::WalkSpeed`/`RunSpeed` — Troika's stated 100/225 u/s, which are
dead ConVars in the retail build — and the authored cells disagree by roughly half (`walk_0` is
53.8 u/s). So the Character rung cannot be closed before the animation rung: `CCC7` re-opens what
`4.7` settled, by design.

| Rung | Canonical step | Where this project stands |
|---|---|---|
| Character math | 1 | `4.7 [x]` — ported line-by-line, `Elysium.Substrate.Movement` green. Its **speed authority closed at `CCC7`**, and its world half — `StepMove`, `CategorizePosition`, the jump against real geometry — is bracketed by the gym |
| Camera | 2 | `11.7 [x]` faithful evaluator; `CCC2 [x]` the service, the post-layer stack and the modern rig, which supplies the shipped base view (owner call); the co-tune is still outstanding |
| Character ↔ camera co-tune | 3 | **absent** — the remaining half of `CCC3 [~]`, and now unblocked: `CCC7` has settled the speed |
| Controls polish | 4 | plumbing done (`11.5 [x]`, `11.6 [x]`); the feel half landed with `CCC3 [~]` — the look curve at the command seam, and leniency measured rather than assumed |
| Capability slices | 5 | the jump chain, inside `CCC5` |
| Real animation | 6 | `CCC4 [x]` the intent, resolver and selection record; `CCC5 [x]` the graph, `CCC6 [x]` the green room driving it on the gym floor |
| Vertical slice | 7 | `CCC8` — played by the owner, not beat-scripted; `CCC9 [x]` retired the scaffolding behind it |

**The gym splits its assertions by whether the speed authority can move them**, which is what let the
Character rung be partly closed ahead of the inversion and what now proves the inversion did not
disturb collision:

| Speed-invariant — baselined permanently | Speed-dependent — baselined from `CCC7` |
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
the speed the animation supplies is read **before** the solve, not after: retail's `PreThink` runs
ahead of `PlayerMove` in the same command and re-resolves all three gait activities from scratch,
never consulting `m_nSequence`, so there is no one-frame selection lag to preserve.

## Phases — two lanes off one gate

Numbered for dependency, not for date — and the numbers are not the order. `CCC0` gates everything
because no later rung can be proven without it. After it, the work runs as **two parallel lanes**:
the camera lane and the animation lane share no dependency — nothing between the body sample and
the green room reads the camera — so neither waits on the other, and a stall in one lane (a retail
trace, a bake) is a context switch rather than a slice stall.

| Lane A — the camera platform | Lane B — the animation spine |
|---|---|
| `CCC2 [x]` — the service foundation, then the modern rig | `CCC1 [x]` — the body sample |
| `CCC3 [~]` — the response curve and the leniency courses; the co-tune waits for `CCC7` | `CCC4 [x]` — intent, resolver, record |
| | `CCC5 [x]` — the player graph |
| | `CCC6 [x]` — drive it |

The lanes joined at `CCC7 [x]` (the speed authority) and join again at the co-tune half of `CCC3`
and at `CCC8`. `CCC9 [x]` has retired what they replaced: there is one animation host for every
body, so the cast and the player can no longer drift into two systems that happen to play the same
files.

- [~] **CCC0 The instrument.** A code-built gym generated from `ElysiumMove`'s own constants, so a
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
  **Baselines are measured recordings, never regenerated expectations** — an expectation rebuilt
  from the same constants as the geometry moves with the geometry, and a gym that regenerates both
  can never turn red. They live under `$ELYSIUM_EXPORT_ROOT/_move/baseline/` and are gitignored:
  `CCC7` stands a real baked body on the gym floor, which makes every recording game-derived. The sited `sp_tutorial_1` course coordinates are surveyed here — the course
  table marks them placeholders in code. Design: `docs/architecture/movement-architecture.md`;
  harness: `docs/architecture/debug-tooling.md`.
  *Acceptance:* the speed-invariant thresholds above are baselined as committed recordings; moving
  a constant in `ElysiumMove` turns exactly the bracket that constant owns red; the differ fails on
  an unregistered or missing channel; the PC body stands **on** the gym rather than above it.
  *Deps:* none — `4.7`'s harness and the stage both exist.
  *Done:* the gym (33 lanes at this rung, 43 once `CCC3` added the two leniency families, in an empty
  stage world, `ElysiumGymSpec.h` + `ElysiumGymBuilder.h`), the
  named-channel recorder and its manifest (`ElysiumChannels.h` + `FElysiumChannelRecorder`), the
  channel differ chained into `debug move` with its five refusals
  (`validation/channel_diff.py`), a gym baseline per lane that reproduces byte-for-byte, and the red
  test both ways — `StepSize` reddens only the riser lanes and `StandableZ` only the slope lanes. `elysium.playerpos` sites a course the way `elysium.campos`
  sites a vantage. The five open-floor `sp_tutorial_1` courses are surveyed onto real warehouse floor
  and confirmed headless.
  *Remaining:* three sited feature courses. `stairs`, `slope` and `doorway` carry surveyed
  coordinates that a played session stands on and the headless run cannot — the body falls through or
  is seated in a solid — so they are recorded and deferred rather than baselined. The floor the
  harness is missing is map-collision work, not a survey. Two findings sit under it: `sp_tutorial_1`
  ships no ramp near the 0.7 standable normal, and the map's `.hulls` sidecar is not the walkable
  surface, so a coordinate picked out of it is picked off the wrong geometry.

- [x] **CCC1 Publish the body sample.** `FElysiumLocomotionSample`
  (`docs/architecture/animation-architecture.md` §3.2) is a POD struct publishing local planar
  velocity, speed, facing yaw, `move_yaw`, ground/air/water, stance and jump phase — the jump
  phase carried as the hold window plus the vertical velocity's sign — written once at
  the mover's tick tail, after the last stepper substep, where the state is settled. **Two yaw
  channels are recorded from the first day** — the wish-direction yaw and the velocity yaw —
  because `CCC7`'s sign recovery compares the retail selector's input against both, and the
  recordings exist before the question is asked. **The NPC motor fills the same struct**; one
  contract, two producers, so the cast's locomotion and the player's cannot become two systems
  that happen to play the same files.
  *Acceptance:* post-solve speed, `move_yaw`, ground state and stance are readable from one struct
  for both producers; both yaw channels are registered in the differ; the Cog Npc window shows both
  producers. *Deps:* none.
  *Done:* the struct and its pure rules (`ElysiumLocomotionSample.h` — `RelativeYaw`, `StanceFrom`,
  `FromCharacterMovement`, and `EElysiumWaterLevel`, which is body state rather than a solve rule),
  asserted by `Elysium.Substrate.Locomotion`. `Speed2D` and `JumpPhase` are derivations rather than
  stored fields, so they cannot disagree with the velocity they come from. The stance carries
  **four** values, not three: `bDucked` and `bDucking` are independent and all four pairs are
  reachable — the release edge sets `bDucking` while `bDucked` is still true, and under a low ceiling
  the body stays in that unduck ramp rather than passing through it. Both producers answer one
  contract — `IElysiumPlayerBody::GetLocomotionSample` and `IElysiumNpcMotor::SampleLocomotion` —
  the player's stored because its wish belongs to the command that was integrated, an NPC's pulled
  because it has no command to desynchronise against, and `FromCharacterMovement` serving both the
  cast and the `elysium.SourceMovement 0` capsule body. Every wish reaches the sample through one
  capture point inside `WishDirection`, which is why `WaterMove` goes through the member rather than
  the free function; a frame that integrated nothing holds the previous sample. `move_yaw_wish` and
  `move_yaw_vel` are registered as `EKind::Angle`, a third comparison rule taking a wrapped
  difference, because a backpedalling body sits exactly on the ±180 boundary. The Cog Npc window's
  Locomotion tab draws both producers through one row function. `Elysium.Substrate.FrameOrder`
  gains the rung's assertion — the post-move pass is later than the mover — and the player visual
  gains the mover tick prerequisite `ACharacter` installs for every other body.
  *Scope note:* the consumer split — a game-thread copy in `NativeUpdateAnimation` latched into the
  proxy in `PreUpdate` — lands with the graph at `CCC5`; a proxy member nothing evaluates is dead
  until one exists. And the gym **registers** frame channels without comparing them: a committed
  gym baseline is the manifest alone, so the two yaw channels are asserted by the sited baselines
  and the cross-rate run, which is what the recordings were for.
  *Closed by `CCC4`:* `JumpPhase` cannot reproduce retail's latched jump/landing phases from velocity
  alone — retail distinguishes phase 1 `ACT_LEAP`, phase 7 `ACT_FALLING` and phase 8 gait/land, while
  a descent and walking off a ledge are identical in the state the sample carries. The latch that
  answers it is `FElysiumJumpLatch`, and it belongs to `CCC4` rather than `CCC5` because choosing
  between those three activities *is* step 2, which the resolver cannot answer without it.

- [x] **CCC2 Camera service foundation and the player rig.** The re-architecture the camera needs:
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
  *Done:* the four corrections landed in `docs/architecture/camera-architecture.md` before it was
  built from. `AElysiumPlayerCameraManager` owns the one final view through
  **`UpdateViewTargetInternal`** — the narrow seam, so the `ACameraActor` branch character
  generation uses, the stock debug camera styles and the POV reset all keep working — and it
  resolves the rig from the live view target every frame, never cached, because
  `elysium.SourceMovement` swaps the pawn class at spawn. The legacy scripted channel is a **post
  layer** (`UElysiumCameraModifier_LegacyShot` over the shared `UElysiumCameraModifier` base),
  which is the faithful arrangement rather than a divergence: `ApplyToView` split into
  `ApplyBaseToView` + `ApplyScriptedShotToView` over the pure `ElysiumCam::ComposeScriptedShot`, so
  the shot composes over *either* rig and choreo does not break when the base swaps. The shot stack
  stays the single timeline — the layer's alpha is written from its weight with zero blend time, so
  a zero-duration edit is still a cut. The modern rig is manager-side state over pure `ElysiumRig::`
  rules (`Elysium.Substrate.CameraRig`), not a component and not a spring arm, with a
  frame-rate-independent half-life damper, asymmetric collision and a shoulder offset. Eleven camera
  channels ride the existing recorder; both rigs record every frame whichever supplies the base,
  which a run under `elysium.ModernCamera 1` confirms by producing byte-identical channels. The Cog
  window `Elysium.Characters.Camera` and `elysium_player_get` read the same published sample the
  recorder does, so a readout and a channel diff cannot disagree.
  *Instrument findings:* the A/B caught two defects on the first comparison — an inverted pitch sign
  that put the modern camera below the eye line (visible only as `mcam_clip` firing on 78 frames
  where `cam_clip` fired on none), and a harness defect of its own where the third-person weight was
  armed per course, so recording began mid-ramp and a frame-counted settle landed at 1.0 at 60 Hz
  and 0.5 at 120. The run now waits on the weight itself. The gym's `pop_*` courses also isolate the
  faithful damper's frame-rate dependence: with horizontal position and speed agreeing exactly
  between 60 and 120 Hz, the boom still lands 0.36–0.47 u apart — sub-tolerance, so nothing reddens.
  *Deferred, with the seam kept:* weapon-class arbitration is **not** removed. The `+0x2440` bit
  meanings are still unrecovered and classes `0x08`/`0x10` look like Logic rather than preference,
  so the doc now records the removal as deferred pending RE and the forced-third/forced-first/feed
  latches stay as the entry point a weapon system will use. `UElysiumCameraProfile` and the user
  settings surface defer to `11.13d`/`8.10`, where the screen that consumes them lives; the tuning
  partition that makes them unambiguous is recorded now.

- [~] **CCC3 Controls response, and the co-tune that waits.** The feel half of input, which no
  document tracked — `docs/architecture/input-architecture.md` recorded look-curve tuning as "a
  Feel-axis decision with no owner yet" and had no `## Feel` section. Two questions land now. The
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
  argued; the co-tune is performed once, after `CCC7`, against the camera channels the gym records.
  *Deps:* `CCC0`, `CCC2` for the curve and the courses; `CCC7` for the co-tune.
  *Done:* mouse look is a first-class modern Enhanced Input path:
  `Mouse2D → IA_MouseLook → UInputModifierSmooth → UElysiumInputRouter::OnMouseLook` in
  `IMC_Player_KBM`. It is separate from the pad's `IA_Look` because a mouse delivers a displacement
  and a stick delivers a held deflection. Legacy `bEnableMouseSmoothing` is off, Mouse2D's legacy
  scale is 1.0, and the Content tier asserts the generated action, context and sole `Smooth`
  modifier. This is an explicit owner divergence from VtMB's mouse feel; the per-count
  `sensitivity` × `m_yaw`/`m_pitch` names remain the settings surface, not a fidelity constraint.
  The optional `ElysiumInput::ShapeMouseLook` acceleration stage remains after the mapping and
  defaults to the identity (`look_curve 0`); enabling that separate curve is not part of this call.
  **The pad's whole path landed on the same seam**, for a reason the mouse's did not have: a stick
  reports a *held deflection* the game integrates, so the device's noise is integrated with it.
  `ElysiumInput::ShapeStickLook` / `ShapeStickMove` own the dead zone, the saturation, the response
  curve, the filter and the sustained-turn ramp over a `joy_*` cvar surface, asserted by
  `Elysium.Substrate.StickLook`; the mapping keeps **only** the device-frame Y negate, and the
  Content tier asserts the absence of every feel modifier in the asset. The filter and the ramp are
  what force that placement rather than style — a half-life and a charge both need the frame's
  clamped, dilated delta, and an Enhanced Input modifier only ever sees the raw engine one.
  *Instrument finding:* the pad was **measured, not assumed** (`elysium.LookProbe`, kept as a
  permanent verb): the axes quantise to 1/127, the resting centre sits about 0.04 off zero, and a
  steady hold swings ±0.2 on Y between consecutive frames while X holds to ±0.04 — at a stable
  110 fps, so none of it was frame rate. That sized the dead zone, justified the filter, and is why
  an unshaped stick read as unusable while the same build's mouse read as fine.
  **The leniency question is now measured.** Two five-rung brackets — `ledge_*` and `land_*`, one
  lip and one drop, ten committed baselines — place exactly one jump press at a frame offset from a
  **body event** rather than from the clock, because the walk to a lip moves with the gait and the
  offset does not. The harness resolves the event in a probe pass and replays for record;
  `Expand` stays pure by taking the resolved frame as an argument. As recorded, the cliff is exactly
  one decision wide at both 60 and 120 Hz: `ledge_m1`/`ledge_0` jump and `ledge_p1` does not;
  `land_p1` jumps and `land_0` does not. There is no coyote time and no input buffer, and adding
  either now moves a committed number (`ledge_p1`'s or `land_m1`'s `jumps_taken`) instead of
  starting an argument. `advance_max` saturates against the back wall at 2.97 s of the 10 s hold —
  a 3.37× margin — so the lanes stay speed-invariant through a halving of the gait at `CCC7`.
  *Instrument finding:* the two new run channels changed **no** existing baseline. The recorder
  emits only the channels a run opened, so a producer that writes rows nothing else writes is free;
  the 33 gym baselines came back byte-identical and the commit adds ten files rather than rewriting
  forty-three.
  *Remaining:* the **co-tune**, and only the co-tune. It was gated on `CCC7` by design — tuning
  against a walk speed the speed authority may halve is paid for twice — and that gate is now open:
  the forward gaits are 53.8 / 188.5 / 65.3 u/s. It touches
  `ElysiumRig::FElysiumCameraRigTuning`, never the `cam_*` console store the faithful evaluator
  reads.

- [x] **CCC4 Intent, resolver, selection record.** `FElysiumAnimationIntent` in,
  `FElysiumAnimationSelection` out, over steps 2, 4, 5 and 6 of
  `docs/architecture/animation-architecture.md` §3.3 — there is no channel to arbitrate and no
  weapon to translate through yet, and both seams exist rather than being stubbed away. Most of the
  resolution already exists and is reachable: `UElysiumAnimSubsystem::ResolveActivityClip`
  returns the vocabulary label, the concrete animation and the cell's authored ground speed, and
  `ResolveGrid` returns the baked blend space with its axis bindings. What is missing is the caller
  and the record. **The caller is one shared function** — sample and catalog in, selection out —
  invoked by the player path post-solve and by the NPC motor from its own tick: one contract, two
  producers, and the intent builder is pure, so it is asserted against a hand-built catalog in the
  Substrate tier. The front door lands on the existing subsystem; the physical split and rename of
  the catalog out of its NPC-named host waits for `CCC9`, because a big-bang split churns a
  subsystem five systems read. The record is not decoration — six things can produce a wrong pose,
  and without one line naming each, a wrong pose is a guess.
  The current producer census is the concrete route test: the same record schema must distinguish
  ordinary activity resolution from 198 map-authored exact-label requests, 18 prop
  `SetAnimation` requests, scripted sequence-0 fallback and gesture no-op. It must also preserve a
  model-owner change separately: three `MorphModel` fields and 356 Python `SetModel` calls change
  the bank against which later requests resolve, while the decoded native schedule surface sends
  task arguments through ideal activity, immediate activity, overlay layer, named fallback or
  no-animation routes. The later weapon seam reads ordered table rows: duplicate-base entries are
  availability fallbacks, while the stored `required` bit is provenance only because retail's
  server translator does not consult it. NPC class translation is likewise generated policy: 77
  RTTI classes share 10 pre-translation and five class-translation bodies. Their 49 custom
  StartTask/RunTask bodies add 111 fully classified task routes, all activity-based rather than
  exact-label or layer requests. The decoded 1,872 sequence events and their complete native
  server/client dispatch map are **catalog playback metadata**, not another intent or resolver
  branch; `ANM4b` still has to emit that timeline before a selected sequence can reproduce its
  sounds, effects, attachments, weapon actions and script outputs. The same boundary holds for
  the 685 decoded autolayer bindings: their authored order and full caller weight are recovered,
  while emitting/composing them belongs to the catalog and graph rather than `CCC4`. Paired
  actions fan 29 base activities into 232 exact attacker/victim, size and side variants. The
  player side keeps
  a separate nine-mode router: its initial paired base, continuation state, attacker/victim role
  and two-model commit all belong in the selection record; three registered modes are dormant in
  the pinned binary while the other six have exact producer sites. Collapsing any of those into
  one activity string would erase behavior
  already confirmed in the 22-map corpus; detailed facts stay in
  `docs/vtmb/animation_and_movers.md`.
  *Acceptance:* a caller hands the resolver an intent built from `CCC1`'s sample and gets back an
  asset plus a record naming every step; the record is on screen in Cog and in the headless run's
  JSON; **and the player and the cast resolve to their own banks** — `ACT_RUN` reaches the PC-only
  bank for the player and the cast bank for an NPC (`docs/vtmb/animation_and_movers.md`), asserted
  for both producers, because a resolver keyed on the label alone hands the player the cast's
  gait. *Deps:* `CCC1`.
  *Done:* the two records and the pure rules over them (`ElysiumAnimationIntent.h` — the classifier,
  the jump latch, the translation pass, and the one place the slice's `ACT_*` literals are spelled),
  asserted by `Elysium.Substrate.AnimationIntent`; the resolver
  (`Visual/ElysiumAnimationResolve.h` — steps 4, 5 and 6 over a catalog **view**), asserted by
  `Elysium.Substrate.AnimationResolve` against a two-bank fixture built on the stack. **The record
  and the asset are two return values**, which is the rung's load-bearing shape: the record comes out
  of the sidecars and is always producible, while an asset needs a `USkeletalMesh` that the gym, a
  menu backdrop and any body before its visual is built do not have — so a missing asset reads
  `NoAsset` rather than as a missing record, and `ANM1` does not gate this rung.
  The catalog is a **view with a lookup callback** rather than a preloaded map, because the owning
  bank is not known until after the weighted pick and one player body's DAG names dozens of banks;
  that callback is also what lets a test hand it two stack `FElysiumBlendTable`s.
  Both producers run one `FElysiumAnimationDriver`: the player's on the map actor's post-move pass,
  the cast's on a new `TG_PostPhysics` tick function on `AElysiumNpcBody` — the engine wires no
  prerequisite between an actor's tick and its own CharacterMovement, so a selection read from `Tick`
  would read whichever registered first. `Elysium.Substrate.FrameOrder` asserts that as a class
  default, and the turn-in-place stays in pre-physics. `BuildNpcMotor` carries the model stem and the
  variant, which is the same seam `CCC11` widens into the intent.
  Eight frame and three run `anim` channels ride the existing recorder; the differ refused all 43
  committed gym baselines as unbaselined declarations before they were re-promoted, and the diff is
  purely additive — no movement or camera value moved. The Cog Locomotion tab gained five columns
  through the **same** row function, so the player's owning bank and the cast's are read side by side,
  and `elysium_player_get` gained `locomotion` and `animation` blocks.
  `ResolveActivityClip` and `PickActivityClip` are re-expressed over the one resolver, so the player
  path and the five existing `PlayNpcActivity` callers cannot disagree about a bank silently.
  *Instrument finding:* the acceptance clause reads out of the sited run's own manifest rather than
  out of a test —`ACT_RUN=run@…runotherspc_pcidles_allsequences:run_0` beside
  `ACT_WALK=walk@…move_and_ranged:walk_0`, 240 of 240 frames resolved with zero fallbacks. One body's
  run and its walk come from different glbs, which is exactly what a label-keyed resolver erases.
  `Elysium.Content.AnimationSliceCoverage` closes the slice rule over the real corpus: 52 player
  bodies × 9 activities, the three gaits resolving as blend spaces and the remaining playable
  states as sequences, with `ACT_LAND_CROUCH` the only named miss — the export agreeing with the
  capture rather than a clip being invented for it.
  *Scope note:* this rung resolves and records; it drives no pose. Nothing routed into the cast's
  native proxy, so no scaffolding was built that `CCC9` then had to delete. The recovered
  fallback ladder (run → walk → disposition → sequence zero) is `CAI_BaseNPC`'s and runs only for an
  NPC source; the player has none, which is why a player miss is a **named** miss.

- [x] **CCC5 The player animation graph.** `ABP_ElysiumBiped`: a locomotion state machine whose
  transitions carry the authored `fade` duration as a **runtime inertialization request**, blend-space
  players on walk, run and sneak driven by `move_yaw`, and a slot for one-shots. **One parameterized
  graph, not one per
  archetype:** every player node takes its asset dynamically — through the anim-node-function
  library setters or an exposed asset pin bound to an instance property the native update writes —
  which is the design's own step 6 (no re-selection in the graph) made structural. That choice
  serves two masters at once: one graph plays every model the resolver picks assets for, and the
  tracked graph source carries zero references to generated content, so the authored/generated
  boundary holds by construction. A per-archetype graph is
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
  jump is **states, not montages**, and the controlled retail corpus fixes those states: the
  ordinary path selects `ACT_LEAP` at jump phase 1, `ACT_FALLING` at phase 7, then a moving gait or
  `ACT_LAND` at phase 8. It does not select `ACT_LEAP_ASCEND` or `ACT_LEAP_DESCEND`. A ducked phase-8
  request asks for `ACT_LAND_CROUCH`, which returns no sequence on the validated player body and
  therefore needs a named answer rather than an invented clip — the resolver keeps the miss and the
  graph declares the state. Transition parity is a
  Content-tier test: retail combines a pair as
  `max(outgoing, incoming)` and ships 0.2 s on nearly the whole vocabulary
  (`docs/vtmb/animation_and_movers.md`), so the six transitions assert against the authored table
  rather than being reviewed. Sync-group phase matching between gaits is a Feel modernization —
  retail's crossfades are phase-independent — recorded in the divergence table and deferred.

  The locomotion slice is `ACT_IDLE`, `ACT_WALK`, `ACT_RUN`, `ACT_SNEAK`, `ACT_CROUCH` and the
  `ACT_LEAP`/`ACT_FALLING`/`ACT_LAND` chain; none is masked and none is additive, so none needs
  the derived `<label>@<host>` naming, which is why the slice can be cut here. No aim node, no
  layered blend, no additive node — those arrive with the weapon rung. The faithful held-crouch rule
  is now exact: reuse sequence 8 while it is active, then restart the same non-looping sequence after
  `StudioFrameAdvance` sets its finished flag. The graph must therefore drive one-shot completion
  into the existing `NotifyOneShotComplete` seam rather than hold the terminal frame.
  *Acceptance:* the locomotion activities are reachable through the graph; the transitions assert
  against the authored fades in the Content tier; the eyes track and the forearms twist on the migrated
  body; `elysium.BlendSpaces 0` still A/Bs against the single resolved cell.
  *Deps:* `CCC4`. **`ANM1` does not gate this rung** — the same finding `CCC4` recorded, for the same
  reason: `ABP_ElysiumBiped` is a **template** Animation Blueprint carrying no target skeleton
  (`AnimBlueprintCompiler.cpp` compiles one with none, and `AnimGraphNode_AssetPlayerBase` permits
  asset-player nodes carrying no asset), so it binds to whatever skeleton the mesh brings.
  *Done:* the template graph, its tracked **T3D source** and the generator that rebuilds the package
  from it (`pipeline/unreal/graphs/ABP_ElysiumBiped.t3d` + `make_player_anim_bp.py`) — the asset is
  generated, never committed, which is the repo's "authored live, captured as text, rebuilt by a
  generator" rule rather than an exception to it. `elysium.PlayerGraph` (default 1) installs it and
  falls back to the NPC instance by name. The portrait stack is **shared, not duplicated**:
  `UElysiumBodyAnimInstance` is the base both `UElysiumBipedAnimInstance` and
  the cast's native instance derived from, and `Elysium.Content.PlayerGraphInstance` stands a real
  baked body on the graph and asserts the axis-interpolation rules resolve against that body's own
  skeleton and that a `SetEyeInput` write lands — the regression that logs nothing.
  The authored fade reaches the body as a **runtime inertialization request** rather than a
  compile-time `CrossfadeDuration`: the pair combines as `max(outgoing, incoming)`, `flags & 0x2`
  is a zero-duration request rather than a branch, and `Content/ElysiumAuthored/README.md` bars
  encoding game-derived timings in a tracked package, so the graph asset carries a ceiling and
  nothing else. `Elysium.Substrate.AnimationGraph`, `Elysium.Content.PlayerGraphTransitionParity`
  and the resolution plus asset-kind matrix in `Elysium.Content.AnimationSliceCoverage` close the
  tier half.
  `ACT_LAND_CROUCH` is **declared by the graph, not fixed by the resolver**: the resolver keeps
  returning the named miss, and the state projection routes it onto `Land` while the record still
  names what was asked for.
  *Instrument findings:* the live run found three defects the tiers could not, and each is now a
  pure rule asserted in the Substrate tier rather than a fix at a call site. Anim-graph array pins
  are `BlendPose_<index>` and the friendly labels match no pin, so an unchecked link compiled,
  exported and ran as a **T-pose** — every link is now checked and the builder refuses to export on
  a dead wire. `GetRelevantAnimTimeRemaining` answers **`MAX_flt`**, not zero, when it finds no
  relevant asset player, so "cannot say" read as "still playing" and parked a landing for fourteen
  seconds (`IsPlayableRemaining`). And a request that resolved nothing was projected anyway, leaving
  an asset pin null and a sequence player evaluating to the bind pose — retail never reaches
  `ResetSequenceInfo` on a failed selection, so the pose is now **held** (`ShouldHoldPose`), and a
  one-shot with no clip is a **finished** one rather than an unanswerable one (`OneShotStateFor`),
  which is what stops a ducked landing floating for the fallback window.
  *Deferred, with the seam kept:* the sync-group phase matching named above is not built; the gait
  speeds that decide stride and foot-sliding are `CCC7`'s by design, so no number here was tuned
  against a walk speed that is about to move.

- [x] **CCC6 Drive it — the green room on the gym floor.** A construction rung, not a wiring rung:
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
  *Done:* the lab has **two modes over one stage** — `Review`, the animation programme's clip stage,
  and `Drive`, reached by `uv run elysium gr <stem> --drive` (`-GreenRoomDrive`) or the window's mode
  buttons. `LabSetMode` is the one door both come through, so neither mode's teardown can be
  half-done by one caller and whole by another. The rung's construction is `LabSetDriveBody`: it
  calls the shipping `BuildPlayerVisual` and **leaves the attachment the builder made** — the hull
  offset, the facing basis, the mover tick prerequisite and the cached stem that `TickPlayerAnimation`
  reads — where the capture path detaches and re-parents to the stage root.
  **What drive mode does not do is the load-bearing half.** `PinCameraAndPlayerSurface` refuses
  structurally while driving rather than merely not being called, because both pins fight the
  shipping path — the control rotation is where mouse look lands and the model alpha is solved from
  the fade band every frame; the orbit shot is popped off the player's own stack on entry, so the
  view is the manager's and the rig's; and `TickDrive` writes nothing but the stage key/fill carried
  with the body (a gym is unlit geometry in an empty level) and the requested overlays. No camera
  shot, no control rotation, no clip seek, no placement.
  The floor is the same gym the movement harness stands, built from the mover's **live** tuning
  rather than a cached spec, and `ElysiumGym::DefaultOrigin()` is now one symbol both harnesses read
  — two origins for one spec would produce two coordinate sets for one geometry, and
  the gym baselines are in these. The lane picker reseats with the harness's own sequence
  (`ResetState`, `SeatOrigin`, control rotation to the lane yaw, a boom reseed so the damper does not
  ease across the teleport), and `Elysium.Substrate.GymSeat` names the `flat` lane drive mode starts
  on, so renaming it reddens instead of standing a body in the void. The stage world's movement
  freeze is released only because a floor was supplied, third person is set once as the player's
  persistent choice rather than pinned per frame, and driving on a real map is refused outright
  rather than standing a second floor through a level.
  The Drive tab **reads and never re-derives**: the shared 15-column locomotion row moved out to
  `ElysiumCogLocomotionRow.h/.cpp` so the Npc window and the drive panel are one function over the
  one contract; graph state, `move_yaw`, speed and axis come off the instance; the one-shot report
  shows its generation against the driver's, and a negative remaining renders as "cannot say" rather
  than as a number. Held-versus-playing needed a new published bit — `IsHoldingPose()`, rather than a
  reader recomputing `ShouldHoldPose` from private inputs — and the readout carries **three** states,
  because a body that has never been handed a selection poses the bind pose by construction and that
  correct frame looks exactly like the defect below it.
  That defect now has a shared measure: `ElysiumPose::Measure` /
  `FillRefPoseComponentSpace` (`ElysiumPoseDeviation.h`), asserted by
  `Elysium.Substrate.PoseDeviation` — bone 0 skipped, since in component space the root carries the
  actor transform and a body that merely walked would otherwise read as a changed pose.
  `Elysium.Content.PlayerGraphInstance` grew to the same measure over the real generated graph on a
  real baked body: the bind pose taken *before* anything is published, a resolved idle through the
  actual resolver over that body's own sidecars, the blend-space pin proven separately from the
  sequence pin, and the `ACT_LAND_CROUCH` miss moving **zero** bones while staying posed.
  `elysium gr <stem> --drive` needed the switch taken out of the positional list or it would have
  arrived as `-GreenRoomClip=--drive` with the mode never armed — `pipeline/tests/test_harness_options.py`
  is that assertion. Cog's auto-open stops grabbing the keyboard while driving on both the console
  and the launch path, since ImGui consumes every key it holds; F1 hands it over and back.
  *Instrument findings:* standing the PC body on the shipping path found two bake defects nothing
  else could, both in the shared skeleton and both silent. The bone tree was **empty**:
  `FReferenceSkeletonModifier` owns bones and bind poses while `USkeleton::BoneTree` is a parallel
  array the build never touched, so every bone carried no retargeting data at all — fixed by merging
  the authored reference skeleton back through `MergeAllBonesToBoneTree`, which is the one public
  door that fills both halves. And every bone was on Unreal's default
  `EBoneTranslationRetargetingMode::Animation`, which applies a shared clip's own translation tracks
  verbatim: 96.6% of the 115,005 translation tracks across the 60 shared banks are constant across
  every frame — the emitting model's bind pose, not movement — so one bank's clip dragged every other
  body's joints onto the donor's proportions. Forearms stretched and hands fanned with no log line,
  a clean Content Browser preview, and every other assertion green. Now `Skeleton` mode below the
  root, with `Bip01` (a clip's displacement) and `Bip01 Pelvis` (an additive's hip delta, which
  `Skeleton` mode zeroes outright) keeping the animation's own translation, asserted by
  `Elysium.Content.BakedSkeletonRetargeting`. The spine and clavicle bones lose a small authored
  translation under it — a stated simplification, marked in the bake; the faithful answer is for the
  exporter to stop writing constant translation tracks at all, which would leave every untracked bone
  on its own bind pose by construction.

- [x] **CCC7 `move_yaw` and the speed authority.** The rung where the ladder inverts. **Its RE is
  closed** — the recovered behaviour is in `docs/vtmb/source_movement.md` → "Player speed is
  animation-driven" and `docs/vtmb/animation_and_movers.md` → "`move_yaw` is right-positive" and
  "The gait ladder runs ahead of the compact-code dispatch" — so what remained was construction plus
  four owner calls the recovery made unavoidable.

  **The sign is recovered, not open.** `move_yaw = AngleDiff(facingYaw, velocityYaw)`, wrapped to
  (−180, 180), right-positive, zero forward, sourced from *realized velocity* — so `move_yaw_vel`
  is the channel and `move_yaw_wish` is not what retail differences. It maps onto
  `UKismetAnimationLibrary::CalculateDirection` with **no negation**, Source's reversed subtraction
  and Unreal's opposite handedness cancelling. Three retail behaviours ride the same write and are
  the rung's actual plumbing: a 720 °/s slew that re-arms only when the previous write was within
  0.3 s, a snap otherwise, and a **hold** at zero speed where `CalculateDirection` returns 0.

  **The speed seam moves.** Retail publishes six per-direction tables and the client injects one
  cell's absolute speed into the move command; `m_flMaxspeed` is only the clamp ceiling. So
  `elysium.move.AnimSpeedAuthority` reading the selection record inside `GetMaxSpeed` reproduces a
  scalar retail does not have. The faithful shape resolves a per-direction cell speed where the
  wish vector is built, which is `FElysiumResolvedGrid` exposing its per-cell speed table beside
  its axis bindings — the table is needed either way, but as an input to the command rather than
  to a max-speed read. Root motion is not a further question: the exporter leaves the skeletal root
  in place and carries per-cell displacement as metadata, so the clips hold the root and the motor
  translates.

  **Four owner calls, each with the faithful answer now known:**
  1. **Interpolation across the fan.** Retail snaps to one of eight cells by a 3×3 digital-key
     table and has no analog input to serve. Blending cell speed by `move_yaw` is what stops a
     sideways run foot-sliding on a stick, and it is a Feel-layer divergence rather than the
     faithful behaviour it was assumed to be.
  2. **Crouch faster than walk.** With the duck crop dead code and `sv_sneakscale` 2.3 multiplying,
     retail's sneak runs 63.1–71.8 u/s against a walk of 23.9–53.8. Reproducing the authority
     reproduces that.
  3. **The walk asymmetry.** Walk alone ignores `m_flSpeedScale`, so a speed buff pins the gait to
     run. Faithful, and strange enough to be worth stating rather than inheriting silently.
  4. **The `cmdMoveMag` term** — the one defect rather than a call. Retail's walk/run test is
     `speed2D > T || cmdMoveMag > T`; ours has only the realized term, so we ramp into the run
     where retail snaps on the first frame of full input. `T` is the body's own forward walk cell
     plus 1.0, per-model.

  *Acceptance:* `move_yaw` is produced from realized velocity with the slew, the re-arm and the
  hold, asserted in the Substrate tier; the per-direction cell speed reaches the wish vector, or
  the scalar shape is named in the code as a divergence at the point it applies; the four calls
  above are each recorded in the divergence table with the faithful behaviour beside them; and the
  speed-dependent gym baselines are promoted for the first time, with the speed-invariant ones
  still green. *Deps:* `CCC6`.
  *Done:* the pure table and its seam (`ElysiumGaitSpeeds.h` — `FElysiumGaitSpeedTable`,
  `ElysiumGait::WishSpeedFrom`), asserted by `Elysium.Substrate.GaitSpeeds`;
  `ElysiumBlendGrids::SpeedFan` over the baked grids and
  `UElysiumAnimSubsystem::ResolveGaitSpeeds` over the un-relaxed activities, asserted against the
  real corpus by `Elysium.Content.GaitSpeeds` at 53.8 / 188.5 / 65.3 u/s with walk and run resolving
  to **different banks**. `GetMaxSpeed()` is the ceiling and nothing in the solve reads it;
  `WishSpeed(WishDir, Scale)` is the one seam, and the four call sites go through it.
  **The tables are pushed rather than pulled** — the driver re-resolves them only when the body key
  moves and the map actor hands them over on change — which is what dissolves the ordering problem
  the rung was expected to have. `elysium.move.AnimSpeedAuthority` defaults to 1 with the constants
  as the `0` path, and `elysium.move.GaitSpeedInterpolate` defaults to 1.
  `move_yaw` is `MoveYawPose`, produced by `ElysiumLocomotion::AdvanceMoveYaw` — 720 °/s, the 0.3 s
  re-arm, the hold at a standstill — owned by the driver because the slew is a rate and a producer's
  sample is a getter a readout may take twice. It rides as its own frame channel beside the two raw
  yaws. `FElysiumGaitReference` is built by `ElysiumAnimIntent::GaitFrom` from the same tables the
  mover commands from, so its walk/run threshold is the body's own forward walk cell plus one unit;
  the sample grew `CommandedSpeed` and the latch takes `max(realized, commanded)`.
  *Instrument findings:* three defects the tiers could not have found, each now a pure rule.
  `FMath::FixedTurn` answers in [0, 360), so an unnormalized slew read +184 where the fan wanted
  −176 and resolved the wrong cell **only near the wrap seam**. A fresh `FElysiumMoveYawFilter` must
  read as never-written or its first moving frame slews out of forward instead of snapping. And
  standing a real body on the gym found that **the crouch has been a toggle since `CCC5`**, so every
  course segment that released the Duck button was a no-op: `duckpop` never stood back up, and the
  two `unduck_*` lanes never *asked* to, which meant they had been recording a refusal they never
  requested. `ReleaseCrouch` presses the toggle a second time and the three lanes measure again.
  *Deferred:* the per-(channel, lane) `speedDependent` granularity. `advance_max` and
  `ground_transitions` are invariant on 36–37 lanes and are the measurement on 5–6, and the flag is
  per-channel; with the baselines now freely re-promotable the cost of that is a promote rather than
  a false green, so it is recorded and not built.

- [ ] **CCC8 Played acceptance.** The slice's finish line, and it is **the owner playing it** —
  owner call: no beat script, no Play-tier automation, no `11.10` dependency. The gym and the
  channel differ already bound correctness; what is left is feel, and feel is judged by playing.
  The checklist below is what to play against, not a harness to build.
  *Acceptance:* the PC body walks, runs, sneaks, crouches and jumps around the gym from real input,
  framed by both camera modes, with the stride following the strafe continuously and no visible
  pop at a gait change; every threshold the gym brackets still holds where
  `docs/vtmb/source_movement.md` says it should (the headless run says this, not the play session);
  and the pose is correct in the Content Browser preview and the anim editor, not only in our
  runtime. *Deps:* `CCC7`, the co-tune half of `CCC3`.

- [x] **CCC9 Retire the scaffolding.** `FElysiumNpcAnimProxy` composed by hand what the graph now
  owns: a four-slot sequence-player pool with an age-evicting fade list, plus the base grid slot —
  a larger cut than a single crossfade. Only the parts this slice replaced go: the two layer slots
  survive until the aim node lands, and the cinematic seek path — `Seek`/`ResyncPosition`, the
  substrate-clock phase lock a montage cannot replicate — survives until `ANM6` migrates the
  theatre, which is last on purpose. The blend profiles the accumulator reads are the same assets
  `FAnimNode_LayeredBoneBlend` consumes unchanged, so nothing is re-baked. This is also where the
  catalog and resolver are renamed out of their NPC-named host — once the pool's callers are gone
  and the front door `CCC4` opened is the only door.
  *Acceptance:* the slice's bodies run through `ABP_ElysiumBiped`, the proxy's fade pool and grid
  slot are gone, the resolver's name matches what it serves, and the theatre is still on its
  verified seek path. *Deps:* `CCC8`.
  *Done:* **the whole cast moved with the player**, which the acceptance forces rather than merely
  permits — the pool *is* the cast's crossfade, so "the fade pool is gone" is only true once nothing
  poses through it. `FElysiumNpcAnimProxy` and `UElysiumNpcAnimInstance` are deleted outright
  (~1,150 lines: the four players, `FFadingClip`, `TakeFreeSlot`'s age eviction, `FadeWeight`, the
  grid slot), the `bPlayerMaterial` branch in `BuildNpcVisual` is gone, and every body installs
  `ABP_ElysiumBiped`. An NPC's stance now crossfades through the graph's own montage slot and its
  inertialization request.
  The two survivors live on `FElysiumBipedAnimProxy`: the autolayer accumulator **verbatim** — two
  slots, the mask resolve against the target skeleton, the overlay-then-additive walk,
  `elysium.LayerDump` — and the seek path as **one** standalone sequence player. While that player
  holds a clip it replaces the graph's output outright rather than blending with it, which is what
  keeps a scene's pose a function of scene time; the graph keeps advancing underneath, so a scene
  hands the body back to a machine that kept up with the world instead of one frozen where the scene
  began. `UElysiumAnimSubsystem` is the rename, files and log category with it.
  *Divergence:* **a cinematic clip change no longer crossfades.** The seek path survived and the
  pool's fade did not, and a scene's clip boundaries are the scene's own to time. Recorded here
  because it is a behaviour change rather than a refactor; `ANM6` owns whether the theatre wants one
  back.
  *Findings:* two, both about what a cut exposes rather than what it removes. **The pool was also
  the no-graph fallback** — with the generated package missing, the cast used to animate on the
  native instance, and a graph-only host would have T-posed the whole game on a stale mount with
  nothing but a warning. The clip player answers `PlayOneShot` when there is no compiled graph, so
  the named failure stays named. And **the first clip on a body has to snap**: the montage slot's
  source pose is a state machine that has been handed no asset yet, so an ordinary blend-in fades
  every body up out of the reference pose on map load — the rule the pool's `bInitialized` guard
  carried, now the blend-in on the first one-shot.
  The green room's grid lab was **kept by moving it onto the graph** rather than deleted with the
  slot it used: `PlayNpcGrid` publishes a selection naming the blend space, so a review body poses
  through the path the game plays through. Skeletal props and preview bodies take the native host
  deliberately — they stand one named clip and nothing publishes a selection for one, so a compiled
  locomotion machine would sit inert behind them.

**Past the first slice's finish line**, and tracked here because they reach the body through the same
seam rather than a second one:

- [x] **CCC10 The weapon rung — layered, additive and aim nodes.** The graph nodes `CCC5`
  deliberately omits: `FAnimNode_LayeredBoneBlend` over the baked blend profiles, additive nodes for
  the `_delta` family over the base each asset names, and an aim node for the upper-body
  overlay family. The 3×3 aim grids bake once per declaring host but cannot be stood at all today,
  because a masked grid evaluated as a *base* pose loses the body's stance from the waist down; in a
  graph the mask is a property of the blend node rather than of the pose feeding it, which is why no
  layered blend-space path exists in the accumulator and building one there would be building it
  wrong.
  Every cell of an aim grid shares one bone mask — measured, not assumed, and rooted at
  `Bip01 Spine1` across the six male shared banks only, so a consumer composing a grid as one masked
  layer asserts the property rather than assuming it (`docs/vtmb/animation_and_movers.md` A.4) — so a
  whole grid sits behind one node, **and the node is named**: not an aim-offset asset, which requires
  mesh-space additive samples the baked grids are not, but a layered bone blend — consuming the baked
  blend profiles unchanged — over a plain blend-space player. This rung also retires the last two
  nodes `FElysiumBipedAnimProxy` owns outside the graph — the layer slots `CCC9` left standing — and
  un-collapses a stated simplification: gesture and sequence share one clip slot today, so a scene's
  gesture overwrites its sequence instead of layering over it.

  **The green room surface moves onto the graph rather than going with the slots**, which is
  `CCC9`'s grid-lab precedent applied to the layer lab: `RenderAutoLayers` — the declared-layer list,
  the arm/re-arm per entry, the entry index and overlay/additive tag, `Arm as declared`, the weight
  slider and the composing count — is how the owner drives a layer by hand, and deleting it with the
  accumulator would leave the node with no live driver. It re-points at the graph's layered blend and
  **gains what the aim grid needs to be steered at all**: an `aim_yaw`/`aim_pitch` pair over the
  armed grid, distinct from the existing eye/gaze sliders, and a moving host under it so the torso
  claim can be judged against a walk rather than an idle. The owner drives the live acceptance from
  there; the headless tiers carry everything that can be asserted without a stage.

  **A layer arrives two ways, and only one of them is in the model.** The autolayer table binds the
  aim grid and the bobble/delta family to a host sequence and evaluates it at the host's own cycle
  (`smith_ready → smith_aim_layer`). Beside that, 46 masked sequences per bank carry an
  `ACT_*_LAYER_*` activity that **no autolayer entry names** — every weapon's attack, reload and
  dry-fire layer, the discipline casts and both `lookback_*_layer` — and the game DLL selects them by
  activity and composes them as layers (`docs/vtmb/animation_and_movers.md` A.3).
  `docs/vtmb/combat-and-damage.md` closes that second producer: `PLAYER_ATTACK1` realizes
  `ACT_RANGE_ATTACK1_LAYER`, the weapon activity table translates it per family
  (`ACT_RANGE_ATTACK_LAYER_GLOCK`, `_M37`, `_STEYR`, `_SUBMACHINEGUN`, `_CROSSBOW`, over generic
  pistol, two-handed and submachine-gun fallbacks), and `PLAYER_RELOAD` adds `ACT_RELOAD_LAYER`. So
  the layer path needs a **resolver-driven** entry beside the bake-time binding — an intent arriving
  on the `UpperBody` channel and resolving to a masked sequence — not only a host the bake already
  wired. The contract carries it already (`EElysiumAnimChannel::UpperBody` and `AimYaw`/`AimPitch` on
  `FElysiumAnimationIntent`, written through to the pose values by the resolver), so this is node
  work rather than contract work.

  **The player's aim yaw is pinned at zero.** The ordinary player selector writes `aim_yaw` as the
  literal `0.0f` and takes `aim_pitch` from a separate field, neither derived from the movement pair
  (`docs/vtmb/animation_and_movers.md`). The player's 3×3 grid therefore resolves as a pitch column
  in retail and the yaw axis is NPC-side. That scopes the acceptance below; it is not a reason to
  bake the grid differently, since the grid is shared and the parameter is the producer's.

  **Melee is in this rung, but not for aiming.** Measured over the exported banks: all **49** 3×3
  `aim_yaw`/`aim_pitch` grids live in `move_and_ranged` (25 male, 24 female) and every one belongs to
  a firearm or thrown weapon — `glock`, `smith`, `anaconda`, `deserteagle`, `enfield`, `m37`,
  `rem700`, `steyr`, `submachinegun`, `supershotgun`, `crossbow`, `flamet`, `throwing_star`, the
  generic `pistol` and `twohanded` fallbacks, several with their own `crouch`/`midcrouch` variants.
  The melee banks (`meleeshared_onehand`, `meleeshared_twohand`) carry **no 3×3 grid and no autolayer
  host at all**. A melee weapon's upper body is instead a single masked overlay riding locomotion —
  `<weapon>_bobble_layer` and `<weapon>_relaxed_move_layer` on `baseballbat`, `katana`, `knife`,
  `sledgehammer`, `bushhook`, `stake`, `tireiron` and `claws`, with `fists` declaring none — which is
  weapon carry and sway, not a direction. So melee needs the **same masked node** and no aim
  parameter, and the ranged/melee split is also an asset-kind split: a ranged bobble is a `_delta`
  additive, a melee bobble is a `_layer` overlay.

  **But melee does not resolve to one blend profile, and the boundary is not melee-versus-ranged.**
  The mask follows the weapon's **grip** (`docs/vtmb/animation_and_movers.md` §A.4, measured over
  both banks): a two-handed grip takes the 49-bone upper-body gate — every firearm, *and* the melee
  `bushhook` and `sledgehammer` — while a one-handed grip takes a 24-bone right-arm mask that leaves
  the torso to the base, which is `baseballbat`, `katana`, `knife`, `stake` and `tireiron`. So this
  rung stands **two** blend profiles behind one node kind, the aim grids and the two-handed melee
  share one of them, and a resolver that picks the profile from "is this a melee weapon" gets
  `bushhook` and `sledgehammer` wrong. The profiles are per-mask assets `ANM2` already bakes; what is
  new here is only that the melee path cannot assume a single one.

  **This rung is the third-person pass; first person is `CCC10.1`.** The aim layer poses the
  player's **world model**, and `ShouldDrawLocalPlayer` draws that only in third person. The view
  mode changes neither the aim/attack origin nor movement, so the aiming *math* is shared and only
  what is drawn differs — and which mode a weapon starts in is a per-weapon-class bit, not an
  animation property (`docs/vtmb/camera-view-modes.md`). So this rung builds the third-person body
  and takes no viewmodel dependency, and nothing here waits on `PL14`.

  **A layer clip carries a gameplay commit.** The shot is not traced from the input function — an
  attack-layer sequence event reaches `CWeaponRanged::Shot`, so the notify riding a *layered* clip is
  the ballistics trigger (`docs/vtmb/combat-and-damage.md`). Those clips are 4–16 frames at 30 fps
  (the submachine gun's is 4, the Steyr's 7), so a blend-in sized like an ordinary crossfade is a
  large fraction of the clip, and a notify that does not fire while the layer is blending loses the
  shot outright. This is the property `CCC11`'s weapon actions build on.

  *Acceptance:* an aim grid stands as a **layer** over a moving host, torso upright, through a node
  that owns the mask; a melee host's `_bobble_layer` rides the same node with no aim parameter, with
  a one-handed weapon resolving the right-arm profile and a two-handed one resolving the same
  upper-body profile the firearms use; a
  weapon's attack layer is reached by activity translation rather than by an autolayer binding and
  its sequence event fires while layered; and a gesture layers over a sequence rather than replacing
  it. Split by instrument, per the surface below: the headless tiers carry mask resolution, node
  composition order and the activity-translation path, and **the owner judges the pose live in the
  green room**, which is why the layer lab has to survive this rung driving the graph.
  *Landed:* the grip and weapon-translation tables, the `UpperBody`/`Additive` resolver branch, the
  instance pins, and the four graph nodes — a `LayeredBoneBlend` carrying the tag the runtime finds
  it by, an `ApplyAdditive`, the aim `BlendSpacePlayer` and the `BlendListByBool` that selects grid
  or sequence — authored live and captured to `ABP_ElysiumBiped.t3d`. The proxy's layer accumulator
  is deleted and the layer lab drives the graph, with four preset acceptance cases that stand a body,
  enter drive and arm a case in one click. `Elysium.Content.UpperBodyLayerArming` walks the whole
  arming path headlessly — vocabulary, declaring host, derived `<clip>@<host>` asset, mask profile on
  the playing skeleton — over every label an autolayer table binds.
  **The bone mask is not a graph pin**: `FAnimNode_LayeredBoneBlend::BlendMasks` is edit-time state,
  so the node is found through `FAnimSubsystem_Tag` and set by name through `SetBlendMask`, which is
  what Epic's own `ULayeredBoneBlendLibrary` does. A null mask is legal only because the graph is a
  *template* Animation Blueprint, which is what keeps a generated profile asset out of the tracked
  graph text.
  *Findings:* three, all of them silent failures. **A blend profile does not travel with
  `AddCompatibleSkeleton`** — the layer masks were created only on the bank skeleton that authored
  them, and a profile's entries are bone references into the skeleton that owns them, so no body
  could ever resolve its own mask and every masked layer was refused. `DeclareCompatibleSkeletons`
  now mirrors them onto the declaring family skeleton, name preserved, bones intersected.
  **`HasCompiledGraph` asked `IsChildOf(UAnimBlueprintGeneratedClass)`**, which is the metaclass of a
  generated graph class rather than an ancestor of it — a predicate that was never true for any body,
  gating not only the layer refusal but `PlayOneShot`/`StopOneShot`, which had been routing to the
  clip-player fallback on every body while still animating.
  **`glock_aim_layer` is an authored sign defect** and the wrong grid to calibrate against
  (`docs/vtmb/animation_and_movers.md` → aim axes); the preset cases lead with `anaconda` instead.
  *Scope moved out, owner-called.* Two of the four acceptance clauses leave this rung.
  **The gesture/sequence un-collapse goes to `ANM6`**, which already owns choreographed playback. It
  is the only clause with nothing built, it shares no machinery with the other three, and the RE
  behind it landed against the shape this rung assumed: a gesture is an overlay in the same four-slot
  `CBaseAnimatingOverlay` array, rate-scaled then free-running, so the second scene-time-pinned
  player sketched here would reproduce timing retail does not have
  (`docs/vtmb/animation_and_movers.md` A.4c).
  **"Its sequence event fires while layered" goes to `CCC11`**, because the carrier does not exist:
  nothing in the bake emits a `UAnimNotify`, so VtMB sequence events reach no Unreal notify today.
  The risk the clause names is real and unchanged — those layer clips are 4–16 frames, and a
  sub-threshold node weight during a blend drops queued notifies — but it is only testable once the
  sequence-event bake that `CWeaponRanged::Shot` needs exists, and that bake is `CCC11`'s.
  *Accepted live.* An aim grid stands as a layer over a moving host with the torso tracking view
  pitch and the legs keeping their gait. The grip boundary reads correctly side by side, and the
  **left arm is the discriminator**: under the two-handed `bushhook_bobble_layer` both arms and the
  torso pose while the legs stay on the base, and under a one-handed overlay only the right arm
  moves. The two grips resolve different profiles through one node, which is the claim a
  melee-versus-ranged split fails.
  *Deps:* `CCC5`; `docs/project/animation-roadmap.md` ANM1 and ANM2's binding half;
  `docs/vtmb/combat-and-damage.md` for the weapon-layer producers.

- [ ] **CCC10.1 The first-person rung — the viewmodel body.** `CCC10` is the third-person pass: it
  poses the player's **world** model, which `ShouldDrawLocalPlayer` draws only in third person. This
  rung is the other half — the viewmodel that is drawn instead, its rig, its clips, and the seam that
  selects them. It is a **separate body, not a camera mode**: nothing in `CCC10`'s node set carries
  over, because the viewmodel has its own skeleton and its own clip vocabulary.

  **Melee has no first-person model, and that is measured rather than assumed.** The shared male
  hands viewmodel carries **154 sequences over exactly 12 firearm families** — `anaconda`,
  `crossbow`, `desert_eagle`, `flamethrower`, `m37`, `pistol_glock`, `rifle_rem700`,
  `rifle_steyraug`, `submachine_mac10`, `submachine_uzi`, `supershotgun` and `thirtyeight` — each
  with its own `idle`, `idleempty`, `fidget`, `draw`, `lower`, `fire`, `fireempty`, `reload` and
  `dryfire`, plus the `m37`'s three-part `reload_begin`/`reload`/`reload_complete` answering its
  authored `reload_single`. The only non-firearm entries are seven `v_lockpicks_*` sequences
  (`equip`, `start`, `pick`, `atk`, `end`). **There is no fists, katana, baseball-bat, sledgehammer,
  claws, stake or tire-iron family anywhere in the set**, and no melee weapon ships a `v_` model of
  its own.

  **The camera class is authored per item, and melee is a hard force.** Every `vdata/items` record
  carries a symbolic `camera_class`, and the shipped census is: `noswitch` (119 — every quest item
  and key, plus `item_w_unarmed` and the ghoul/Protean claws), `melee` (20 — fists, katana, baseball
  bat, baton, knife, sledgehammer, tire iron, fire axe, torch and the rest), `ranged` (18 — every
  firearm plus the crossbow, flamethrower and three Discipline records), `thrown` (4 — throwing star,
  frag grenade and Chang's two) and `force_1st` (4 — the lockpick and the three physics-gun dev
  items). Joined to the recovered arbitration (`docs/vtmb/camera-view-modes.md`) and **confirmed
  against the live retail game**:

  | Authored class | Bit | Behaviour |
  |---|---|---|
  | `ranged` | `0x02` | settable, first person under the shipped `camera_prefs 6`, toggleable to third and back |
  | `thrown` | `0x04` | settable, likewise first by default — **out of this rung's scope**, see below |
  | `force_1st` | `0x08` | always first person, not user-settable — which is why the hands bank carries `v_lockpicks_*` and nothing else non-firearm |
  | `melee`, `force_3rd` | `0x10` | `ForceThirdPersonOn` — **always third**, not user-settable, sets `m_fForcedThird`. Two spellings of one class; `melee` *is* the engine's force-third class rather than a class that happens to default third |
  | anything else, including `noswitch` and an absent key | `0` | the early-out — equipping changes nothing |

  Retail plays this exactly: a gun works in **both** views, and drawing a melee weapon **forces you
  out of first person**. That is what `m_fForcedThird` is for, and it is what makes the
  `inven_holster` branch in `CAM_ToggleCamera` legible — the equipped item cannot leave third
  person, so trying to toggle out of one holsters it. Bit `0x01` is **dead config**: no ladder arm
  produces it and no item can carry it, so `camera_prefs`' shipped default `6` and the value `7`
  differ only in a bit nothing matches.

  **Divergence — owner call, made: the equipped weapon does not move the player's camera.** The
  faithful behaviour is the table above: equipping re-arbitrates, `melee`/`force_3rd` yanks the
  player out of first person, `force_1st` yanks them into it, and `togglecamera` on a forced weapon
  holsters it rather than switching. We keep the view mode the **player's**, so switching to a melee
  weapon in first person stays in first person. The consequence is accepted rather than solved:
  **there is nothing to draw**, because no melee weapon has a viewmodel or a hands-bank family. What
  that looks like — empty hands, a held pose, an automatic nudge to third — is deliberately left open
  and revisited when the rung is built; it is not a reason to reinstate the yank. `camera_prefs` and
  its per-class stickiness are the faithful path and stay implementable behind a toggle, per the Feel
  layer's A/B rule.

  The class is parsed by an inlined case-sensitive `memcmp` ladder in the item-record vdata parser,
  present byte-identically in both DLLs — `client.dll` `0x101a5394`–`0x101a5438` inside the parser
  at `0x101a48b0`, and `vampire.dll` `0x1025aa65`–`0x1025ab08` inside `0x10259f80`. `noswitch` is
  **not a recognized literal**; it reaches `0` through the same fall-through as a typo or a missing
  key, so the authored vocabulary is a convention rather than a checked enum.

  **Scope — owner call, made: this rung is ranged only.** Its acceptance is the 12 firearm families
  and nothing else. `thrown` is dropped: the throwing star has **zero models anywhere**, packed or
  loose (one orphan `.vmt` survives, and the shipped item description is Troika stating outright that
  it was never finished), and the frag grenade — which the Unofficial Patch restores as an obtainable
  item — has a **complete model set but no animation**: it declares `"anim_prefix" "grenade"` and the
  hands bank has no `grenade_*` family, which is a strong candidate for why the restored item is
  non-functional in play (no camera response, no throw). The lockpick and the Discipline viewmodels
  ride the same machinery and are deferred rather than designed out.

  **The asset set.** 21 models under `models/hands/**` as `v_<clan>_<gender>_hands.mdl` — seven clans
  plus `hunter` and a `shared` fallback, per gender, with two Tremere `_shield` variants; their
  export is `docs/project/roadmap.md` `PL14`. They carry `NumFlexDescs` 0
  (`docs/vtmb/facial_animation.md`), so the portrait stack is not a dependency here. Beside them sit
  **17 packed per-weapon viewmodels**: one per firearm family (`v_anaconda`, `v_crossbow`,
  `v_desert_eagle`, `v_flamethrower`, `v_m37`, `v_pistol_glock`, `v_rifle_rem700` and
  `v_rem700_bach`, `v_rifle_steyraug`, `v_submachine_mac10`, `v_submachine_uzi`, `v_supershotgun`,
  `v_thirtyeight`), plus `v_pineapple`, `v_lockpicks_ref` and the Disciplines `v_thaumaturgy`,
  `v_holylight` and `v_dragonbreath`. These are small rigs carrying the weapon geometry itself — the
  glock's is 12 bones: a right arm plus `Dummy_Mag` and `body`.

  **Open RE, tracked as `docs/project/roadmap.md` `RE42` and listed here because it is what this rung
  waits on.** The weapon camera class is closed inside `RE42` and needs no more work; the viewmodel
  body does. In dependency order:
  - **How the two viewmodels compose.** Every firearm family appears twice — as animation in the
    hands bank and as a `v_` model of its own — and the hands rig carries arms and fingers while the
    weapon rig carries the gun. The reading that fits is **two complementary slots rather than a
    winner**: arms from the clan's hands model, geometry from the weapon's, which is what
    `m_hViewModel[]` being an array beside a separate `m_pViewWeapon`
    (`docs/vtmb/savegame_format.md`) would mean. Asset-layout inference, **not traced in code** — it
    is the first thing to confirm, because the whole rung's shape follows from it.
  - **The rig's pose convention.** The hands rig is 42 bones rooted at `Camera01`, not the
    character skeleton's `Bip01` chain, so it is a different skeleton and the "poses are baked
    native" rule has to be established for it rather than inherited from the character bake.
  - **`viewmodel_fov` is 54** (`docs/vtmb/source_movement.md`) and how it composes with the camera's
    own FOV is unread.
  - **The sequence-event surface on a viewmodel clip.** `CCC10` establishes that the shot fires from
    an attack-layer event on the world model; whether the first-person shot is driven by the same
    event on the viewmodel clip, or the world model keeps driving it unseen, is unread — and it
    decides whether the two bodies share one commit or need arbitration.

  **Placeholder, and it is a joke until it is not:** with the camera divergence above, a melee weapon
  can be held in first person and nothing exists to draw. The view shows a **Claude glyph** (✳) until
  the real answer is chosen. It is deliberately absurd so that nobody mistakes it for a design, and
  it is the visible marker of the open question rather than a silent empty frame.

  *Acceptance:* a firearm draws, idles, fires, dry-fires and reloads in first person on the clan's
  own hands model, driven through the same intent seam rather than a viewmodel-specific clip call;
  the m37's three-part reload matches its `reload_single` transaction; **equipping a melee weapon
  does not move the camera**, and whatever the empty first-person view shows is a stated choice
  rather than an accident; and the owner judges it live in the green room, which needs a
  first-person stage the layer lab does not have today.
  *Deps:* `docs/project/roadmap.md` `RE42` for the viewmodel recovery — its (a) two-slot composition
  question gates the rung's shape and is the one to answer first; `PL14` for the export, without
  which the rung cannot start; `CCC10` for the resolver's weapon-family translation, which is shared;
  `docs/vtmb/camera-view-modes.md` and `docs/architecture/camera-architecture.md` for the visibility
  projection. Not `CCC10`'s nodes.

- [ ] **CCC11 The action families beyond locomotion.** The acceptance ladder past the gait:
  directional hit, knockback and death reactions, then weapon and interaction actions. Each rung
  closes its own reachability slice — full combat coverage does not block walking, but an unexplained
  action inside an accepted rung does. **This rung owns the pose, not the number**: lethality,
  soak, the damage roll and the health commit are `docs/project/roadmap.md` `13.3`, and this rung
  consumes their outcome rather than computing it.

  **The combat half of that ladder now has its RE**, in `docs/vtmb/combat-and-damage.md`: the
  activities are named (`ACT_PREBLOCK` from compact action 13, `ACT_BLOCK`, the heavy-block band's
  `ACT_BLOCK_HEAVY`, the attacker's blocked reaction, hit and knockback as a separate outcome) and
  `rules.txt` supplies the seven reaction margins that select between them. Three of its findings
  change what this rung builds rather than merely feeding it:
  - **A blocked reaction is authored, not directional.** The attacker plays the activity stored in
    its *own current sequence descriptor*, falling back to `ACT_BLOCKED_REACTION_RIGHT`. Left and
    right are a per-sequence authored field, so a direction-picking resolver rule here would be
    invented rather than recovered. Only the flinch is directional — `ACT_HIT_HEAD` or
    `ACT_HIT_TORSO` oriented by `hit_yaw`, which is what the four baked `hit_yaw` grids serve.
  - **The melee combo is one more resolver rule with a recovered table.** `ACT_MELEE_ATTACK`
    substitutes `ACT_MELEE_ATTACK_2COMBO` on a random draw against a rank table keyed by the *base*
    Brawl or Melee ability, read before temporary adjustments. It is policy over baked catalog data
    like the four rules below, and adds no graph machinery. There is no directional melee state
    machine to reproduce: `med`, `low`, `far` and `jump` are sequence variants, not commands.
  - **There is no firearm stagger to build.** Ranged capability `0x2000` does not satisfy the melee
    block gate `0x18000`, and no firearm-specific stagger threshold is established. A firearm's
    reaction is the generic flinch, and inventing a melee-style stagger meter for one would be a
    divergence with nothing behind it.

  This is also where every remaining producer moves onto the intent seam: no NPC, weapon or script
  path asks for an activity through the resolver today, and
  `PlayNpcActivity` stays a compatibility adapter until patrol and scripted travel cross over — the
  intent's source field already reserves the producers, so expansion is migration order plus a
  bounded set of recovered resolver rules the NPC path carries and the player path lacks: the NPC
  class/weapon translation alternation and its four-way availability ladder (with the first weapon
  answer preserved separately, because the commit feeds it to the weapon's own animation update —
  reload start derives both the weapon's and the owner's end time from the *selected sequence's*
  duration over its playback rate rather than from the magazine's authored `ReloadTime`, so the
  resolver has to hand back the chosen sequence's duration and not only its asset),
  transition-sequence traversal between the current and ideal sequences, the restart rule that
  clears and restarts a repeated identical request rather than ignoring it, and the paired-action
  role/size/side variant arithmetic (`docs/vtmb/animation_and_movers.md`). All four are
  resolver-level policy over baked catalog data — a transition clip is one more selection played
  through the same one-shot slot — so none adds graph machinery. Transition traversal reads the
  model's sequence-transition graph, which the exporter does not yet carry; that bake input is
  `ANM4b`'s catalog scope and this rung's dependency on it. Two shapes build together because they are the same shape — a `scripted_sequence`'s
  `m_iszIdle → m_iszPlay → m_iszPostIdle`, and an interesting place's own enter/hold/leave segments,
  whose naming varies by type (`_INTO`/`_OUTOF`, `_BEGIN`/`_END`, `_PICKUP`/`_HANGUP`) and whose
  ordering comes from the three weighted lists the vdata table declares
  (`docs/vtmb/vdata-catalog.md`). One montage-slot mechanism serves both, and building them apart is
  how there come to be two.
  **The sequence-event carrier is built here, and `CCC10` handed over a clause with it.** Nothing in
  the bake emits a `UAnimNotify`, so a VtMB sequence event reaches no Unreal notify today and the
  ballistics trigger has nothing to ride: an attack-layer sequence event is what reaches
  `CWeaponRanged::Shot` (`docs/vtmb/combat-and-damage.md`). The clause `CCC10` could not test comes
  with it — those layer clips are 4–16 frames at 30 fps, and a node whose weight is still below
  threshold during a blend drops its queued notifies, so a shot can be lost outright with nothing
  logged. The bake and the guard test land together; a guard written before the carrier exists would
  assert against a synthetic notify and rot.
  *Acceptance:* an NPC's ambient behaviour and a scripted beat both reach a pose through the intent
  seam rather than a direct clip call, and `m_iszCustomMove` plays over a travelling body; a struck
  body flinches on the `hit_yaw` grid the hit direction selects, and a blocked attacker plays the
  reaction its own sequence names rather than one chosen from a direction; a weapon's attack-layer
  sequence event reaches the shot while the layer is still blending in.
  *Deps:* `CCC10`; `docs/project/animation-roadmap.md` ANM4's catalog for anything outside the
  hand-read set; `docs/vtmb/combat-and-damage.md` for the weapon and reaction chains, whose open
  joins are numeric (spread and crosshair math, the ranged multiplier decomposition, post-soak
  filters) and therefore gate `13.3` rather than this rung.

## The acceptance surface

Three instruments, and which claim each can carry.

**The gym and the courses** answer *where a threshold is*. `sp_tutorial_1` cannot: it contains no 17u
step beside an 18u and a 19u, so a real map proves the tutorial's stairs work and never proves the
step code cliffs where the decompile says. Generated geometry is the only thing that brackets a
constant.

**The sited courses on `sp_tutorial_1`** answer *whether we match retail*, and are permanently
required for it — retail will not load a gym we authored, so course-time comparison against a capture
is real-geometry-only. The five open-floor courses are surveyed onto real warehouse floor; the three
feature courses are surveyed but not yet standable in the headless run, so they record without a
baseline (`CCC0` → *Remaining*).

**The green room** answers *whether it looks right*. Locomotion, unlike pose composition, has numeric
ground truth — walk-up-18-succeeds and walk-up-19-fails is a boolean — so most of this slice asserts
in a tier rather than on a contact sheet. The green room is where the remainder lands: stride
continuity across the strafe, and whether a gait reads as the original's.

Tier placement follows the existing rule — content-free claims about the solve, the sample, the user
command, the gym spec or the camera weights are `Elysium.Substrate.*`; anything reading an exported
clip, grid or input asset is `Elysium.Content.*`; anything needing a built world with real geometry
is what `-ElysiumMove` exists to turn into a per-change headless run.

## Risks

- **The speed authority moves the floor under every earlier measurement** — closed by `CCC7`, and
  the split held: with the authority on, moving `StepSize` reddens only the riser lanes and
  `StandableZ` only the slope lanes, which is what says the inversion was a feel change and not a
  collision regression. The walk did more than halve — 53.8 u/s against `speed_walk`'s 100 — so the
  co-tune waiting was the right call.
- **A harness can measure nothing and look green.** Standing a real body on the gym found that the
  crouch has been a toggle since `CCC5`, so every course segment releasing the Duck button had been
  a no-op and the two `unduck_*` lanes were recording a refusal they never requested. Nothing failed;
  the recordings were stable and plausible. The general form is that a course's *intent* is not
  asserted anywhere — only its output is — so a recipe can go stale against the mover it drives.
- **The camera is the C with no instrument** — closed by `CCC2`. `SolveViewRoll` is asserted, and
  the four-stage boom solve, the collision sweep and the damper are now recorded per frame as
  `cam_*` beside the modern rig's `mcam_*`, so the evaluator is the recorded reference rather than
  an untested composition. What the instrument cannot bound is taste, which is `CCC3`'s and
  `CCC8`'s.
- **A greenfield camera plan over a working camera** — closed by `CCC2`, which corrected the four
  defects in `docs/architecture/camera-architecture.md` before building from it. The one that would
  have cost most was the priority table: composing retail's scripted channel as a base request
  converts a one-camera-and-weights engine into a winner-takes-all stack and eases an authored
  cutscene timeline twice.
- **The portrait stack can regress without a log line.** The eye seam and the axis-interp/cloth tail
  live on the NPC anim instance today; a player graph that does not carry them ships frozen eyes and
  untwisted forearms, and neither the logs nor the Content Browser preview can show it. `CCC5` owns
  the migration and asserts it.
- **`CCC8` is played, not automated.** Owner call: the finish line is a played session against the
  rung's checklist, so the slice depends on no Play-tier harness and `11.10` gates nothing here. The
  cost is that the finish line is a recollection rather than a CI run — accepted deliberately,
  because the gym and the channel differ are what carry the correctness claim.
- **The theatre is the thing to protect** — held at `CCC9`, which retired only what it replaced. The
  slice's graph shares an animation instance family with choreographed playback, and the phase-locked
  clip player came through the cut intact; what a scene lost is the crossfade between its clips,
  recorded as that rung's divergence. It stays on that path until `ANM6` migrates it.
- **A gym is not a game.** Every threshold can pass while the game still feels wrong. The gym bounds
  correctness; the played acceptance at `CCC8` and the owner's judgement are what bound feel.

## Divergences to record when settled

Each is recorded beside the faithful behaviour in the topic that owns it, and marked as a divergence,
per the house rules.

| Divergence | Owning document |
|---|---|
| ~~The player's gait speed is a scalar constant read at `GetMaxSpeed`~~ — **settled at `CCC7`, and therefore not a divergence**: the speed is the animation's own per-direction cell, injected at the wish vector, with `GetMaxSpeed` reduced to the ceiling retail's `m_flMaxspeed` is. The constants survive as `elysium.move.AnimSpeedAuthority 0` and as the fallback for a body with no fan | `docs/architecture/movement-architecture.md` |
| **Symmetrizing each gait fan** — the authored walk is 38.2 u/s strafing left against 23.9 u/s strafing right, which is faithful and reads as a limp. Mirrored pairs are averaged at table build; on the male body only the ±90 pair differs, so it is one number | `docs/architecture/movement-architecture.md` |
| **The pose parameter's slew, re-arm and hold are reproduced, and the graph steers on the filtered angle while the speed reads the unfiltered commanded one** — retail's three angles, kept apart rather than collapsed | `docs/vtmb/animation_and_movers.md` |
| Any change to movement-orientation and strafing settings made so that `move_yaw` resolves off the neutral cell — **an open owner call, not yet made** | `docs/architecture/animation-architecture.md` |
| Holding a sustained unarmed crouch on the terminal frame — **not faithful and therefore not an owner divergence**: retail reuses sequence 8 until its finished flag is set, then reselects and restarts that same one-shot on the next request | `docs/vtmb/animation_and_movers.md` |
| ~~`ACT_SNEAK` reached from ducked-and-moving~~ — **settled, and therefore not a divergence**: retail's condition is exactly `FL_DUCKING && speed2D > 5.0 u/s`, with no walk/run split and no relaxed variant below it | `docs/vtmb/animation_and_movers.md` |
| ~~The walk/run split taken from realized speed rather than the `+speed` key~~ — **settled in kind**: retail reads no button, and `+speed` reaches the gait only by selecting the walk table client-side. What remains is a **defect, not a divergence** — retail tests `speed2D > T \|\| cmdMoveMag > T` and ours omits the commanded term, so we ramp into the run where retail snaps on the first frame of full input | `docs/vtmb/animation_and_movers.md` |
| ~~The landing one-shot's hold duration~~ — **settled at `CCC5`, and therefore not a divergence**: the landing ends when the graph reports its clip finished, and `FElysiumJumpLatch::LandHoldSeconds` survives only as the fallback for a body with no pose layer at all, which the gym stands | `docs/architecture/animation-architecture.md` |
| ~~The walk/run split keeping **gait memory with a hysteresis margin**~~ — **removed at `CCC7`**: `HysteresisFraction` defaults to 0, which is retail's own behaviour, because the `cmdMoveMag` term made the input a step function rather than the ramp the margin existed to damp. The field survives as a dial, and the band is still asserted when one is asked for | `docs/architecture/animation-architecture.md` |
| **The airborne wish speed holds the last grounded cell** rather than `sv_jump_maxspeed` — faithful, and a correctness fix: retail stops refreshing its tables for the whole jump while the client keeps writing the last grounded cell, so 350 is a ceiling that never fires against a run peak of 208 u/s | `docs/architecture/movement-architecture.md` |
| **Interpolating cell speed across the `move_yaw` fan** — **owner call, made**: retail snaps to one of eight cells by a 3×3 digital-key table and has no analog input to serve, while the walk fan's cells differ by more than 2× and a stick lands between them. `elysium.move.GaitSpeedInterpolate` defaults to 1 and 0 is the faithful snap | `docs/architecture/movement-architecture.md` |
| ~~Ducked movement scaled by Source's `/3`~~ — **settled at `CCC7`**: the ducked speed is the sneak fan's own cell, and retail applies no duck multiplier at all (`HandleDuckingSpeedCrop` occupies a vtable slot nothing calls in either DLL). The `/3` survives only on the constants fallback, which has no sneak table to read | `docs/architecture/movement-architecture.md` |
| ~~Crouch-move slower than walk~~ — **owner call, made: reproduced.** Retail's crouch is *faster* than its walk (forward sneak 65.3 u/s against a forward walk of 53.8), because `sv_sneakscale` 2.3 multiplies and the walk table alone ignores `m_flSpeedScale`. The `Elysium.Content.GaitSpeeds` assertion is what holds it | `docs/vtmb/source_movement.md` |
| **The equipped weapon does not move the player's camera** — **owner call, made**: retail arbitrates the view mode from the item's authored `camera_class` on every weapon change, so `melee`/`force_3rd` (`0x10`) yanks the player out of first person, `force_1st` (`0x08`) yanks them into it, and `togglecamera` on a forced weapon holsters the item rather than switching view. The view mode stays the player's instead. Accepted consequence: a melee weapon in first person has **nothing to draw**, since no melee weapon has a viewmodel or a hands-bank family; what that view shows is deliberately left open at `CCC10.1`. `camera_prefs` and its per-class stickiness remain the faithful path behind a toggle | `docs/architecture/camera-architecture.md` |
| The modern rig supplying the shipped base view — **owner call, made**: it is the base request ahead of `CCC3`'s co-tune, which stays outstanding | `docs/architecture/camera-architecture.md` |
| Sync-group phase matching between gaits in the player graph — retail's crossfades are phase-independent; not built, and an owner call not yet made | `docs/architecture/animation-architecture.md` |
| A look-response curve that is not retail's — **built and shipped off**: `look_curve` defaults to 0, at which the gain is exactly 1.0 and the path is retail's linear one. Enabling it is an owner call not yet made | `docs/architecture/input-architecture.md` § Feel |
| Modern Enhanced Input mouse sample normalization — **owner call, made**: `Mouse2D → IA_MouseLook` carries `UInputModifierSmooth`; legacy `bEnableMouseSmoothing` is off, and removing the mapping modifier is the direct A/B | `docs/architecture/input-architecture.md` § Feel |
| Input leniency — buffering or coyote time. VtMB has neither, **none is implemented**, and the `ledge_*`/`land_*` brackets now hold the committed before-picture, so adding either moves a number | `docs/architecture/input-architecture.md` § Feel |
| A fixed-step accumulator, shipped behind `elysium.move.FixedStep` with the faithful variable delta as the default | `docs/vtmb/source_movement.md` |
| ~~Composing the legacy shot and track channels as post layers, or arbitrating them as base requests~~ — **settled as post layers, and therefore not a divergence**: retail composes the scripted channel over the third-person weight, so this is the faithful behaviour and the doc's table was corrected | `docs/architecture/camera-architecture.md` |
| Weapon-class camera arbitration — **deferred, not made**. The `+0x2440` bits are unrecovered and `0x08`/`0x10` read as Logic; the forced-third/first/feed latches stay as the seam | `docs/vtmb/camera-view-modes.md` |
| The modern rig's frame-rate-independent half-life damper, asymmetric collision recovery and shoulder offset — shipped (row above), with `CCC3`'s co-tune still outstanding | `docs/architecture/camera-architecture.md` |

**Not divergences**, and recorded as defect fixes rather than choices: the box hull that `StepMove`
requires against `ACharacter`'s capsule, and keeping masked sequences out of the base clip path.
