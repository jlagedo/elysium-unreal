# Movement architecture (Unreal)

How VtMB's player movement runs on Unreal: the object graph the mover sits in, what it publishes,
the A/B that keeps it honest, the deliberate divergences, and the instrument that measures all of it.

Engine-neutral VtMB facts — `CGameMovement`'s formulas, the hulls and view offsets, the jump's
`rules.txt` tuning, the frame bound, ducking, water, and the absence of ladders — live in
`docs/vtmb/source_movement.md`, which owns the movement layer as it exists in the original. The bind
and command surface is `docs/architecture/input-architecture.md`; the camera the mover feeds is
`docs/architecture/camera-architecture.md`; the pose the body plays is
`docs/architecture/animation-architecture.md`. Status and task order live only in
`docs/project/roadmap.md` (the CCC slice; the speed-authority and gait work is the LIFE
programme's).

## The split — pure rules, engine half

The same shape the camera uses. `Public/ElysiumMoveSolve.h` is the pure half: `namespace ElysiumMove`
carries the constants in centimetres (`U = 2.54f` converts at the point of declaration, so a Source
unit never reaches the runtime), the `CGameMovement` math as free functions over values, and two
value types — `FElysiumMoveTuning` for the `sv_*` console surface and `FElysiumMoveStepper` for the
timestep. No pawn, no world, no component. That is what lets `Elysium.Substrate.Movement` assert the
whole solve content-free under `-nullrhi`.

`UElysiumMovementComponent` is the engine half: the state machine in retail's own call order
(`PlayerMove` → `CategorizePosition` → `Duck` → `FullWalkMove`/`AirMove`/`WaterMove`), the traces,
and the state the solve is not allowed to hold. Its private method names are Source's on purpose —
a reader comparing against the decompile should not have to translate.

## The body is a box, and that decides the class

`StepMove` runs its raised attempt and then tests the down-trace's plane normal against `0.7`. A flat
box bottom lands squarely on a step top and reports `(0,1,0)`; a capsule's rounded bottom catches the
step's top edge and reports about `0.65`, just under the test, so every climb is rejected and no step
is ever taken (`docs/vtmb/source_movement.md`).

`ACharacter` creates a capsule root that cannot be substituted, so **the player is not an
`ACharacter` and the mover is not a `UCharacterMovementComponent` override.** `AElysiumPawn` is an
`APawn` with a `UBoxComponent` root sized from `ElysiumMove::HullHalfWidth` and `StandHeight`, and
`UElysiumMovementComponent` derives from `UPawnMovementComponent`.

## The object graph

`AElysiumPawn` owns four components and wires them in one direction:

| Component | Role |
|---|---|
| `UBoxComponent Hull` | the root, and the collision the solve traces |
| `UElysiumCameraComponent Camera` | attached to the hull at `StandViewZ` above its centre |
| `UElysiumMovementComponent Movement` | `UpdatedComponent` is the hull |
| `USkeletalMeshComponent PlayerVisual` | **injected, not constructed** — the map actor builds the PC's clan body and hands it over |

**One command, two consumers.** `ApplyUserCmd` fans the frame's `FElysiumUserCmd` to the mover and to
the camera. Neither polls input, and nothing else reads the command.

**The hull and the eye move together.** `SetHullHeight` resizes the box and `SetEyeHeight` re-bases
the camera's relative Z off the new half-extent, both driven from `Duck`/`FinishDuck`/`FinishUnDuck`.
The anchor differs by ground state, and that difference *is* the crouch-jump: on the ground the feet
stay planted and the head drops; airborne the centre stays fixed, so the feet rise 18 units and the
reachable ledge goes from the 25-unit jump-boost pop to 43. Both live on the pawn rather than on
`IElysiumPlayerBody`, because a body that is not a hull has no eye to re-base.

**The camera decides whether the body is drawn.** `AElysiumPlayerCameraManager` resolves the frame's
draw policy and hands it to the body through `IElysiumPlayerBody::ApplyDrawPolicy`; the pawn applies
eligibility and the fade band and decides nothing. The pawn's own `CalcCamera` produces a view and
never writes visibility — a scene capture is a second view of the frame, not a second opinion about
the body. Details: `docs/vtmb/camera-view-modes.md` § 5.

## Divergences

Recorded here beside the faithful behaviour, per `docs/project/reconstruction-direction.md`.

**A fixed-step accumulator.** Retail has no tick: `Host_FilterTime` bounds a *variable* frametime to
`[0.001, 0.1]` seconds and returns — no accumulator, no fixed-interval loop
(`docs/vtmb/source_movement.md`). A fixed step is therefore a divergence rather than the baseline, and
it ships behind `elysium.move.FixedStep`, default `0` = the faithful variable delta.
`FElysiumMoveStepper` carries the remainder and a `MaxSubSteps` backstop when it is enabled.

**The residual frame-rate dependence is smaller than the shape of the code suggests**, measured over
`uv run elysium debug move` at 60/120/240 Hz. The jump apex is a flat **25.00 units at every rate** —
the gravity half-step split plus VtMB's *additive* `CheckJumpButton` is exact velocity-Verlet. What
drifts is air control alone: a strafe-jump exits at 259.5 / 259.8 / 261.0 u/s across the three rates,
about 0.6%.

**Ducked speed** is the sneak fan's own cell under the animation authority. Retail applies **no**
duck speed multiplier at all — `HandleDuckingSpeedCrop` is dead code, unreachable through any call
site in either DLL (`docs/vtmb/source_movement.md` → "The ducking speed crop is dead code") — and
gets its slower crouch purely from reading the sneak table. Source's `/3` survives on the constants
fallback alone, where there is no sneak table to read.

**The frame bound is pinned in three places** — `ElysiumFrame::ClampFrameDelta` in
`ElysiumGameClock.h`, read by the clock, the input router and the mover — because all three take
Unreal's raw delta and retail applies the bound once, ahead of all of them.

## The speed authority

**Retail has no scalar gait speed to port.** `CHL2_Player::PreThink` builds six networked
per-direction speed tables from the model's own 9×1 locomotion fans, and `client.dll` writes one
cell's absolute speed straight into `forwardmove`/`sidemove`; `m_flMaxspeed` is the peak over all 24
cells and serves only as the clamp ceiling
(`docs/vtmb/source_movement.md` → "Player speed is animation-driven"). The animation drives the
movement, not the other way round — and it does so **at the input seam, not at a max-speed read**.

Ours does the opposite: `GetMaxSpeed` answers from `ElysiumMove::WalkSpeed`/`RunSpeed` — Troika's
*stated* 100/225 u/s, which are dead ConVars in the retail build — and the authored cells disagree.
`walk_0` is 136.7 cm/s, or **53.8 u/s** against `speed_walk` 100; the PC bank's `run_0` is 478.7 cm/s,
or **188.5 u/s** against `speed_runbase` 225.

**The authority is the animation's, behind `elysium.move.AnimSpeedAuthority` (default 1).** The
constants remain as the A/B's `0` path, as the fallback for a body with no resolved fan, and as what
a port with no player animation should use.

### The seam

`GetMaxSpeed()` is the **ceiling** — retail's `m_flMaxspeed`, the peak over every cell of every gait
table. Nothing inside the solve reads it, and no clamp against it can fire, because the peak is ≥
every cell by construction. What the solve asks instead is
`UElysiumMovementComponent::WishSpeed(WishDir, Scale)`, over the pure rule
`ElysiumGait::WishSpeedFrom` (`ElysiumGaitSpeeds.h` — the same pure-rules/engine-half split as
`ElysiumMoveSolve.h`), so the decision table is asserted with no pawn.

`FElysiumGaitSpeedTable` is one gait's fan: nine authored cells, an axis range, and the gait's own
scale kept as a field so a readout can show what the animation authored against what the scale did
to it. `ElysiumBlendGrids::SpeedFan` fills one from a baked grid and interpolates across a cell that
baked without motion; `UElysiumAnimSubsystem::ResolveGaitSpeeds` resolves all three from the
**un-relaxed** `ACT_WALK`/`ACT_RUN`/`ACT_SNEAK`, exactly as retail's extractor does.

**The tables are pushed, not pulled.** They depend on the body — stem, weapon, form, variant — and
never on what the body is doing, so `FElysiumAnimationDriver` re-resolves them only when that key
moves and `AElysiumMapActor` hands them to the mover on change. That is what dissolves the ordering
problem: the mover runs in the pre-physics pass and the driver in the post-move one, so a pulled
table would always be a frame stale, while a pushed one is queried synchronously with the current
wish direction. A weapon swap carries one frame of latency, which is strictly less than the network
lag retail carries.

### Three angles, and they are not interchangeable

The **commanded** wish yaw picks the speed cell, instantaneously, inside `WishDirection` before the
move — retail's digital-key direction. The **realized** velocity yaw is what the retail selector
differences. The **pose parameter** follows the realized yaw through `ElysiumLocomotion::AdvanceMoveYaw`'s
720 °/s slew, its 0.3 s re-arm and its hold at a standstill, and is the only one the blend grid sees.
Steering the speed off the filtered angle would make a turning body's speed lag its direction.

### What closing it settled

- **Ducked speed** is the sneak fan, not a divisor. The `/3` in the constants path is Source's own
  default and is a divergence (below), because retail applies no duck multiplier at all.
- **The airborne wish speed is the last grounded one.** Retail stops refreshing its tables for the
  duration of a jump while the client keeps writing the last grounded cell, so `sv_jump_maxspeed`
  350 is a ceiling that never fires — the run peak is 208 u/s.
- **There is no one-frame staleness to preserve.** `PreThink` never reads `m_nSequence`; it
  re-resolves all three activities every frame. The lag in retail is a network snapshot lag with no
  single-player analogue.
- **The walk/run threshold is per-model** — the body's own forward walk cell plus one unit, which
  `ElysiumAnimIntent::GaitFrom` builds from the same tables the mover commands from, so the
  classifier cannot call a body a runner at a speed its run fan cannot produce.

**Root motion is not a fifth question.** The exporter leaves the skeletal root in place and carries
per-cell displacement as metadata beside the resolved cell, so the clips hold the root and the motor
translates. A graph that enables root motion on a locomotion state double-moves.

## The gym

A movement gym's dimensions *are* its specification, so ours is **generated from `ElysiumMove` rather
than authored**, and a threshold cannot drift from the constant it tests. It is built in code in an
empty stage world — the same one the green room stands on — and each lane is a solid-walled corridor
ending in a back wall, so a body that gets through its feature stops somewhere known.

**The bracket is derived and the baseline is measured**, which is what keeps the gym able to fail. An
expectation recomputed from the same constants as the geometry moves with the geometry and can never
turn red, so nothing in the spec states what a course *should* record. A lane is named for its
bracket offset rather than for the value it stands at — `riser_p1` is one unit above `StepSize`
whatever `StepSize` becomes — so moving a constant changes what the lane records instead of what it
is called, and the committed recording is what disagrees.

A lane's **geometry** is where nothing game-derived is: it comes from this repository's own constants
and its input from its own course table. The **body** standing on it is a real baked one, because a
gym driving a body with no animation would measure the constants fallback rather than the speed
authority — so the recordings are game-derived and live beside the sited courses' under
`$ELYSIUM_EXPORT_ROOT/_move/baseline/`. They stay *measured recordings* rather than regenerated
expectations, which is the property that lets the gym fail; what is given up is fresh-clone
reproducibility, so a regression check needs a completed character export first.

Each axis brackets a constant so the cliff is visible rather than inferred:

| Feature | Brackets | Constant |
|---|---|---|
| stair risers | `StepSize` −2 / −1 / **0** / +1 / +2 / +6 u | `StepSize` |
| capping roofs over a jump | `JumpBoost` −1 / **0** / +1 u of clearance | `JumpBoost`, `JumpBoostScale` |
| a crouch-jump under a clear roof | — (recorded, see below) | the airborne duck's own lift |
| ramps | `acos(StandableZ)` −1.5 / −0.5 / +0.5 / +4.5° | `StandableZ` 0.7 |
| roofed spans, standing and ducked | `StandHeight` and `DuckHeight`, each ±1 u | `StandHeight`, `DuckHeight` |
| a chamber between the two hulls | — | `CanUnduck`, grounded and airborne |
| apertures | `2 × HullHalfWidth` −1 / **0** / +1 / +4 / +16 u | the hull width, and `WalkMove`'s two attempts |
| gaps | 32 → 160 u | the held-jump arc |
| a long clear run | — | `Friction`, `StopSpeed`, `Accelerate` |
| a lip walked off, jumped after | −1 / **0** / +1 / +2 / +4 **frames** from ground-lost | `CheckJumpButton`'s airborne refusal — there is no coyote time |
| the same lip, jumped before landing | +1 / **0** / −1 / −2 / −4 **frames** from ground-gained | the same refusal — there is no input buffer |

**`sv_jump_boost` is bracketed by a ceiling, not by a ledge.** It is an instant origin displacement
and the sweep that applies it is capped by whatever is overhead, so a roof at `StandHeight + G`
truncates the pop below the pop's own height and never touches it above — which brackets the constant
without anything having to be landed on, and keeps the measurement purely vertical. The lane runs
with `BaseJumpVelocity` overridden to zero so the pop is the only thing lifting the body; with the
held push also in play the body clears every roof that could bracket it and the bracket says nothing.

**The crouch-jump's reach is recorded rather than bracketed, and that is a property of the geometry
rather than a gap in the gym.** No roof can cap the ducked reach without also capping the standing
pop that precedes it — the standing body needs exactly the headroom the ducked measurement would have
to deny it — so no geometry straddles the answer. The lane measures the lift itself, and `DuckHeight`
moving is what changes it.

**The two leniency brackets are in frames, and they are the one thing the gym times rather than
saturates.** Every other recipe holds its intent long enough that *when* the body arrives cannot
matter; these place exactly one press, and it has to land on a chosen frame. Timing it off the clock
would not survive a speed change — the walk to the lip moves with the gait — so the press is placed
against a **body event** instead: the harness runs the course once with no press to find the frame
the ground state flips, then replays it with the press at that frame plus the lane's offset. That is
sound because a refused press is a provable no-op — `CheckJumpButton` returns on `!bOnGround`
without touching velocity, gravity scale, the hold window or `m_nOldButtons` — so the event frame is
identical in both passes. The press is one frame rather than held, because a *held* airborne jump
already auto-fires on landing (the latch is only cleared by a release) and would measure that
instead. The recorded answer is `jumps_taken`, 0 or 1 at any gait; `event_frame` is recorded beside
it and deferred, because *when* the lip is reached does move with the speed. The faithful behaviour
and the divergence either bracket would represent: `docs/architecture/input-architecture.md` §
Feel.

**Assertions split by whether the speed authority can move them.** Vertical thresholds — step height,
the origin pop, slope standability, unduck refusal, aperture width — are collision geometry and are
invariant under a speed change, so they baseline permanently. Horizontal gap clearance, course times,
air-strafe gain and stopping distance all move when the speed source changes, so they baseline only
once that call is made. A vertical threshold that shifts when the speed changes is a collision
regression, not a feel delta. The leniency brackets sit on the invariant side despite involving a
walk to a lip, because their answer is whether one press became a jump and their press is placed
relative to a body event rather than to the clock.

**A synthetic gym cannot replace the real map, and does not try.** Retail will not load geometry we
authored, so any comparison against a retail capture is real-geometry-only; that is what the sited
`sp_tutorial_1` courses are for. The gym answers where a threshold is; the sited courses answer
whether we match.

The reverse also holds, and it is why the gym is not optional: **`sp_tutorial_1` does not contain the
features in bracketing form.** It ships no ramp anywhere near the 0.7 standable normal — every sloped
surface in it is either a few degrees or effectively a wall — its doors are shut at map load, and its
one curb near the step limit sits behind drains deep enough to swallow the body. A real map proves
the shipped geometry behaves; it cannot locate a cliff, because nothing in it was built to straddle
one.

## The recorded channels

The harness replays a fixed command stream and records it through one named-channel recorder.
Position and velocity are emitted in **Source units**, not centimetres, so a row reads directly
against the decompile and against `docs/vtmb/source_movement.md`'s own numbers.

A channel's comparison rule is part of its declaration: a numeric channel carries an absolute
tolerance, a state channel is compared for exact equality, and a name that is in neither class cannot
be written at all. Ground state, stance and water level are exact — any flip is a behaviour change,
never a rounding one. Each run publishes the declarations it used as a manifest beside its own
output, and the comparator reads that rather than carrying a second copy.

**A channel has a scope, and the scope decides what a baseline can assert.** A *frame* channel is one
value per frame; a *run* channel is one value for the whole course. Every frame channel is
speed-dependent, and that is structural rather than incidental — *when* a body reaches a feature moves
with its gait even where *whether* it reaches it does not. So the gym's run channels are what a
baseline compares, and they are written to **saturate**: how far the body got before
something stopped it, how high it stood, how high it reached. A body either climbs a riser or is
stopped by it, and either answer is the same at any gait given a course long enough to reach the
feature at the slowest speed the mover can produce.

That last clause is a number, not a hope. A lane's 656 travelable units over the hold gives the
slowest gait that still saturates: `ApproachSeconds` 10 s puts it at 65.6 u/s, which the authored
forward sneak cell (65.3) sits just under — so the ducked families take `DuckApproachSeconds` 16 s
instead, a floor of 41 u/s that no authored crouch approaches.

Camera and animation channels ride the **same** runs rather than a second harness, because the
command stream is already deterministic and frame-pinned: a camera regression and a movement
regression are then the same diff. What the camera contributes is boom length, clip state, damper
position and solved angles; what animation contributes is `move_yaw`, the state machine's state, and
the selected asset.

Running it, promoting a baseline and the cross-rate check are `docs/architecture/debug-tooling.md`.

## Tracking

Implementation sequence and status live only in `docs/project/roadmap.md` (the CCC slice; the
speed-authority and gait work is the LIFE programme's).
