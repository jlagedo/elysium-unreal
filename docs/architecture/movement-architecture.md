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
than authored**, and a threshold cannot drift from the constant it tests. It is built in code on the
green room's stage, which already spawns its panels and needs only collision to carry a pawn.

Each axis brackets a constant so the cliff is visible rather than inferred:

| Feature | Brackets | Constant |
|---|---|---|
| stair risers | 16 / 17 / **18** / 19 / 20 / 24 u | `StepSize` |
| ledges | 24 / **25** / 26 u, and 42 / **43** / 44 u | `JumpBoost`, and the crouch-jump reach |
| slopes | 44 / 45 / 46 / 50° | `StandableZ` 0.7 |
| ceilings | 36 → 72 u | `DuckHeight`, `StandHeight`, `CanUnduck` |
| gaps | graded against the held-jump arc | `BaseJumpVelocity`, `JumpHoldSeconds`, `JumpGravityMultiplier` |
| a long flat run | — | `Friction`, `StopSpeed`, `Accelerate` |
| a corner and doorway cluster | — | `WalkMove`'s two-attempt scheme, which special-cases neither |

**Assertions split by whether the speed authority can move them.** Vertical thresholds — step height,
crouch-jump ceiling, slope standability, unduck refusal — are collision geometry and are invariant
under a speed change, so they baseline permanently. Horizontal gap clearance, course times, air-strafe
gain and stopping distance all move when the speed source changes, so they baseline only once that
call is made. A vertical threshold that shifts when the speed changes is a collision regression, not
a feel delta.

**A synthetic gym cannot replace the real map, and does not try.** Retail will not load geometry we
authored, so any comparison against a retail capture is real-geometry-only; that is what the sited
`sp_tutorial_1` courses are for. The gym answers where a threshold is; the sited courses answer
whether we match.

## The recorded channels

The harness replays a fixed command stream and writes one row per frame. Position and velocity are
emitted in **Source units**, not centimetres, so a row reads directly against the decompile and
against `docs/vtmb/source_movement.md`'s own numbers.

A channel is compared only if the comparator knows about it: a numeric channel needs an absolute
tolerance, a state channel is compared for exact equality, and a channel registered as neither is
written to the CSV and silently never checked. Ground state, stance and water level are exact —
any flip is a behaviour change, never a rounding one.

Camera and animation channels ride the **same** runs rather than a second harness, because the
command stream is already deterministic and frame-pinned: a camera regression and a movement
regression are then the same diff. What the camera contributes is boom length, clip state, damper
position and solved angles; what animation contributes is `move_yaw`, the state machine's state, and
the selected asset.

Running it, promoting a baseline and the cross-rate check are `docs/architecture/debug-tooling.md`.

## Tracking

Implementation sequence and status live only in `docs/project/three-cs-roadmap.md` and its roll-up in
`docs/project/roadmap.md`.
