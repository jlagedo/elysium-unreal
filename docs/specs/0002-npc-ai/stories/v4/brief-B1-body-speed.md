# Brief B1 — V4b: the body's speed is retail's whole velocity script (coder; no build)

**Final in its job (amended after V4r, 2026-10-04 — R1 and J9); two inputs arrive from reader
R1b** (`packets-R1b.md` § "For B1": three constants and the arrival tolerance). Do not start
before `packets-R1b.md` exists. Read `README.md` here (§1 "Turning and the walk" with its
amendment; §2 M5–M6; §7 K1; § "Shared names"), `packets-R1.md` (all of it), `packets-R1b.md`, the
ruling J9 (`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`docs/vtmb/npc-ai/shape.md` § "CAI_Motor's unnamed bodies …" (the 2026-10-04 addendum). After
V4a's commit. Re-locate by Grep.

## What R1 measured (so you do not chase the old lead)

The walk's cruise speed is **right**: sentry2 walks at a constant 101.278 cm/s, its own bank's
`walk_0` (female; the male cell is 136.683), facing its path. **No fan change, no cell, no
scale.** N13's time is lost at **arrival**: the body covers the leg in ~5.6 s, then creeps the
last ~30 cm for ~4.1 s (distance × ~0.53 per 0.7 s) until it is inside a 1 cm radius, and only
then does the kernel see `arrived`. Mechanism, inferred from code and not yet toggled: the body is
a `UCrowdFollowingComponent` agent, nothing calls `SetCrowdSlowdownAtGoal(false)`, so Detour
scales the speed by the distance to the goal.

## Files (only these)

- `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.cpp` (`ApplyCrowdState`, `CommandedTravelSpeed`
  ~:335-342, the `MaxWalkSpeed` write ~:475-482, `ResolveFollowerRequest`) and
  `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.h` (`FollowerArrivalFloorCm`)
- `Source/ElysiumUE/Private/Visual/ElysiumNpcMoveScript.{h,cpp}` (new: the velocity script as a
  plain function over waypoints, no Unreal object in its signature)
- `Source/ElysiumUE/Public/ElysiumWorldServices.h` and its implementers
  `Source/ElysiumUE/Public/ElysiumMapActor.h`,
  `Source/ElysiumUE/Private/Map/ElysiumMapActorEmbodiment.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumTestServices.h` — the two accessors of README § "Shared
  names" only
- `Source/ElysiumUE/Private/Tests/ElysiumNpcMoveScriptTests.cpp` (new); and a test that pins the
  body's own fan read as the speed's authority, if one exists (Grep `CommandedTravelSpeed`)

Read, not edited: `Visual/ElysiumAnimationDriver.cpp`, `Visual/ElysiumLocomotionSample.cpp`.

## The job

1. **The motor's speed input is retail's.** Under K1 (the motor on the body's tick), the ideal
   speed is the kernel's `GetIdealSpeed 0x10091740` = `m_flGroundSpeed +0x654` (V4a writes it every
   advance at the kernel's pose parameters), not the body's own fan read at its own measured
   `move_yaw`. The body reads it through the accessor each tick (`KernelIdealSpeedCm()`, README
   § "Shared names"); the conversion to cm/s is stated at the line. **50.0 units/s when it
   answers 0** (`0x102630b0`). A body with no live kernel (a non-NPC) keeps today's path.
2. **The velocity script `0x102630b0`, whole, as the body's commanded speed** (J9 — this is what
   keeps the change inside K1; the toggle alone would be a new divergence). In
   `ElysiumNpcMoveScript`: over the follower's remaining path points, ideal = item 1;
   acceleration = ideal + `_DAT_104493c0`; each waypoint's speed = `ideal × clamp(dot(in, out) +
   _DAT_10449198, 0, 1)`; **the last waypoint's speed is 0**; a forward pass and a backward pass
   limit each waypoint's speed by constant acceleration over the distance between waypoints. Each
   tick the commanded speed is the script interpolated at the body's place on the path, and the
   step is retail's trapezoid, `(|v_now| + v_new) × interval × 0.5` (`_DAT_10449270`,
   `MoveGroundExecute 0x10264680`), clamped to the remaining distance as `MoveGroundStep
   0x102e1760` does. That is what `MaxWalkSpeed` is set from. **The two constants come from
   `packets-R1b.md`**; if R1b left one unrecovered, write "unrecovered; the coder reads it first"
   in your notes, read it yourself (the `vtmb-corpus` tools, by its address) and cite it at the
   line — **never substitute a guess**; if it cannot be read, stop and report.
3. **The crowd follower's slowdown at goal goes off** (`SetCrowdSlowdownAtGoal(false)` in
   `ApplyCrowdState`, beside `SetCrowdSeparation`) **because item 2's deceleration replaces it** —
   in the same change, never alone. Comment at the line: retail's stop is the velocity script's
   constant deceleration to the last waypoint (`0x102630b0`); Detour's distance-scaled slowdown is
   not retail's and is what made N13's creep (inferred until the B integrator's measured toggle).
4. **The arrival tolerance**: the follower's acceptance radius becomes what `packets-R1b.md`
   states retail's is; if R1b found the 1 cm floor is not retail's, replace `FollowerArrivalFloorCm`
   with the retail value, cited; if R1b left it unrecovered, leave the floor and say so at the
   line ("unverified against retail; R1b").
5. **The second accessor**, for B2 (you own the interface file this wave): `bool
   GetNpcMoveFacingYaw(float& OutYawDegrees) const` — the active move's direction along its path
   at the body's place (waypoint to next waypoint), false when no move is under way. Retail eases
   it through corners with the turn script `0x102627e0`, rate-limited backwards by
   `_DAT_10457f60` (R1b): port that easing in `ElysiumNpcMoveScript` if R1b gives the constant,
   else answer the unsmoothed direction and name the missing easing at the line.
6. **Tests**: `Elysium.Arm.NpcKernelMotor.VelocityScript` (`0x102630b0`) on fixture paths — a
   straight leg accelerates, cruises at the ideal and reaches 0 at the last waypoint in finite
   time; a 90° corner slows to `ideal × clamp(0 + _DAT_10449198, 0, 1)`; an ideal of 0 runs at
   50.0; the trapezoid step. No test of the crowd follower. Delete a test that pins the body's
   own speed as the authority, listed in your report.

## Stop rule (J9)

The cause of the creep is inferred from code. The B integrator's first act on its build is to
measure the toggle on `input_clearpatrolpath` (`arrived` at 10.428 s against ~5.6 s of travel). If
the creep survives it, your change does not land: the cause is unread again. So keep items 2–4
separable from item 1 in your diff, and say in your report which lines are which.

## Not yours

The kernel's `move_yaw`, slot 15 / slot 18, the ideal yaw, the turn (B2); the clock (V4a); the fan
and its cells (nothing is wrong with them); the weighted pick.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported (addresses),
the constants and where each came from, the accessors' exact signatures and header, tests, what
stayed unrecovered.
