# Movement architecture (Unreal)

How VtMB's player movement runs on Unreal: the object graph the mover sits in, what it publishes,
the A/B that keeps it honest, the deliberate divergences, and the instrument that measures all of it.

Engine-neutral VtMB facts — `CGameMovement`'s formulas, the hulls and view offsets, the jump's
`rules.txt` tuning, the frame bound, ducking, water, and the absence of ladders — live in
`docs/vtmb/source_movement.md`, which owns the movement layer as it exists in the original. The bind
and command surface is `docs/architecture/input-architecture.md`; the camera the mover feeds is
`docs/architecture/camera-architecture.md`; the pose the body plays is
`docs/architecture/animation-architecture.md`. Status and task order live only in
`docs/project/three-cs-roadmap.md` and its roll-up in `docs/project/roadmap.md`.

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

**The camera decides whether the body is drawn.** `CalcCamera` delegates to the camera component and
feeds its `ModelAlpha()` into `ApplyPlayerModelAlpha`, which writes the scalar on the visual's
material and toggles visibility. The fade band is the camera's; the body only obeys it.

## The A/B

`elysium.SourceMovement 0` stands the player on `AElysiumCapsulePawn` — an ordinary `ACharacter` with
Unreal's own `UCharacterMovementComponent` — instead of the faithful body. Both implement
`IElysiumPlayerBody`, both accept the same command, and both build the same skeletal visual, so the
switch isolates "is this the mover's fault?" in one cvar. It is the pattern any later feel divergence
copies: **the faithful path stays executable, and the A/B is a body swap rather than a branch inside
the solve.**

## Divergences

Recorded here beside the faithful behaviour, per `docs/project/remaster-direction.md`.

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

**Ducked speed** uses Source's `/3` rather than a read-out VtMB value, because retail's is
animation-driven. That, and the standing gait constants, are the speed-authority question below.

**The frame bound is pinned in three places** — `ElysiumFrame::ClampFrameDelta` in
`ElysiumGameClock.h`, read by the clock, the input router and the mover — because all three take
Unreal's raw delta and retail applies the bound once, ahead of all of them.

## The speed authority

VtMB's `CHL2_Player::PreThink` sets `m_flMaxSpeed` from the **current sequence's own root motion**,
scaled by `sv_walkscale`/`sv_runscale`/`sv_sneakscale` (`docs/vtmb/source_movement.md` → "Player speed
is animation-driven"). The animation drives the movement, not the other way round.

Ours does the opposite: `GetMaxSpeed` answers from `ElysiumMove::WalkSpeed`/`RunSpeed` — Troika's
*stated* 100/225 u/s, which are dead ConVars in the retail build — and the authored cells disagree.
`walk_0` is 136.7 cm/s, or **53.8 u/s** against `speed_walk` 100; the PC bank's `run_0` is 478.7 cm/s,
or **188.5 u/s** against `speed_runbase` 225.

**This is an open owner call, marked in the code at the point `GetMaxSpeed` answers**, and it is the
reason the movement layer cannot be closed ahead of the animation layer. Closing the loop makes speed
a function of the animation blend, which is the only way a sideways run and a forward run authored at
different speeds stop producing foot-sliding — and it is a real feel change, not a defect fix. Two
sub-questions sit under it: which cell the retail read samples (the neutral one, or the one the live
`move_yaw` selects), and what `sv_sneakscale` 2.3 divides or multiplies.

**Root motion is not a third question.** The exporter leaves the skeletal root in place and carries
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

A lane is also where nothing game-derived is: the geometry comes from this repository's own
constants and the input from its own course table, so the recordings are committed rather than
gitignored, and a gym run needs no exported map.

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
committed baseline compares, and they are written to **saturate**: how far the body got before
something stopped it, how high it stood, how high it reached. A body either climbs a riser or is
stopped by it, and either answer is the same at any gait given a course long enough to reach the
feature at the slowest speed the mover can produce.

Camera and animation channels ride the **same** runs rather than a second harness, because the
command stream is already deterministic and frame-pinned: a camera regression and a movement
regression are then the same diff. What the camera contributes is boom length, clip state, damper
position and solved angles; what animation contributes is `move_yaw`, the state machine's state, and
the selected asset.

Running it, promoting a baseline and the cross-rate check are `docs/architecture/debug-tooling.md`.

## Tracking

Implementation sequence and status live only in `docs/project/three-cs-roadmap.md` and its roll-up in
`docs/project/roadmap.md`.
