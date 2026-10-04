# Brief B1 — V4b: the body's speed is retail's whole velocity script (coder; no build)

**Final (amended after V4r, settling packets S1 and S4 and the judge's second sitting,
2026-10-04 — R1, J9, S1 item 6, S4 f.1, J14.6).** **This lane
no longer waits on reader R1b**: the constants, the five passes and the arrival tolerance are read
(`packets-S1.md` items 5–6) and written into this brief; where this brief and `packets-R1.md` or
the README disagree, this brief wins. Read `README.md` here (§1 "Turning and the walk" with its
amendment; §2 M5–M6; §7 K1; § "Shared names"), `packets-R1.md` (all of it), `packets-S1.md` item 6
(the listing walk this brief condenses), the
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

Wave check ([B1, B2, V11-1], re-checked after the second sitting): none of your files is B2's
(`ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`,
`ElysiumNpcThink.cpp`, its two tests) or V11-1's (`../v11/README.md` §4). **By ownership of
functions**: `Public/ElysiumWorldServices.h` is yours for the two accessors only; V11-1's new
gate (`0x102a11d0`, J10) **calls** `IElysiumEmbodiment::TraceRetail` declared in that header and
does not edit it — do not touch `TraceRetail` or its neighbours. A line V11-1's test double needs
in `Tests/ElysiumTestServices.h` reaches the integrator, not you.

## The job

1. **The motor's speed input is retail's.** Under K1 (the motor on the body's tick), the ideal
   speed is the kernel's `GetIdealSpeed 0x10091740` = `m_flGroundSpeed +0x654` (V4a writes it every
   advance at the kernel's pose parameters), not the body's own fan read at its own measured
   `move_yaw`. The body reads it through the accessor each tick (`KernelIdealSpeedCm()`, README
   § "Shared names"); the conversion to cm/s is stated at the line. **50.0 units/s when it
   answers 0** (`0x102630b0`). A body with no live kernel (a non-NPC) keeps today's path.
2. **The velocity script `0x102630b0`, whole, as the body's commanded speed** (J9 — this is what
   keeps the change inside K1; the toggle alone would be a new divergence). In
   `ElysiumNpcMoveScript`, in Source units and units per second (convert at the boundary, stated
   at the line), as `packets-S1.md` item 6 walked it — port it pass for pass, bugs included:
   - **Rebuilt every move tick** (`MoveGroundExecute 0x10264680` → `0x10262590` → `0x102630b0`),
     from the body's **current** position and **current** speed; never kept between ticks.
   - **Inputs.** `ideal` = item 1 (**50.0 when 0**); `accel = ideal + 50.0` (double
     `0x104493c0`). Entry 0: the body's origin, its yaw, its speed (`|m_vecVelocity|`, 3-D).
     Then one entry per remaining path point.
   - **Each waypoint's speed.** The last one → **0.0**. Else `d1 = next − this`, `d2 = this −
     the previous entry's location`, both z-zeroed and normalised; `s = dot(d1, d2) + 0.2`
     (double `0x10449198`); `s <= 0` → 0; `s > 1` → 1; speed `= s × ideal`.
   - **Pass 1, distances and prune** (`0x1026330e`): `flDist[i] = |loc[i+1] − loc[i]|` (3-D);
     `flDist[i] < 0.01` (double `0x1044e658`) and `i != 0` → remove entry `i`, retry.
   - **Pass 2, forward** (`0x102633b8`), `i = 0 .. n−2`: `dv = v[i+1] − v[i]`; `dv > 0`: `t = dv /
     accel`; when `v[i]·t + 0.5·accel·t² > flDist[i]` and `SolveQuadratic(0.5·accel, v[i],
     −flDist[i])` (`0x1013a6f0`, `r1 = (sqrt(b² − 4ac) − b) / 2a`) answers: `v[i+1] = v[i] +
     r1·accel`.
   - **Pass 3, backward** (`0x10263480`), `i = n−1 .. 1`: `dv = v[i] − v[i−1]`; **`dv > 0`** (the
     same sign test as pass 2, not the SDK's `dv < 0`): `t = dv / accel`; when `v[i]·t +
     0.5·accel·t² > flDist[i]` (**entry `i`'s** distance) and `SolveQuadratic(0.5·accel, v[i],
     −flDist[i])`: `v[i−1] = v[i] − r1·accel`. As written it never fires on a slowing pair.
     Retail's; reproduce it and say so at the line.
   - **Pass 4, cruise points** (`0x10263554`), `i = 0`, while `i < n−1`: `t1 = (ideal − v[i]) /
     accel`, `d1 = v[i]·t1 + 0.5·accel·t1²`; `t2 = (ideal − v[i+1]) / accel`, `d2 = v[i+1]·t2 +
     0.5·accel·t2²`.
     - `d1 + d2 < flDist[i]`: insert at `i+1` the point `lerp(loc[i], loc[i+1], d1 / flDist[i])`
       at speed `ideal`; insert at `i+2` the point `lerp(loc[i], loc[old i+1], (flDist[i] − d2) /
       flDist[i])` at speed `ideal`; `i += 3`. **No guards** (the SDK's `d > 1.0 && t > 0.1` are
       absent); a zero-length piece is removed by pass 5.
     - else, when `|DeltaV(v[i], v[i+1], flDist[i])| < accel` (`0x102e1470` = `(v2² − v1²)·0.5 /
       d`): `r = (accel + v[i]) / (accel + v[i+1])`; `SolveQuadratic((0.5·r² + 0.5)·accel,
       r·v[i+1] + v[i], −flDist[i])` → `t`; `dA = v[i]·t + 0.5·accel·t²`; `dB = t·r·v[i+1] +
       0.5·accel·(t·r)²`; `peak = v[i] + t·accel`; **`peak < ideal`** → insert at `i+1` the point
       `lerp(loc[i], loc[i+1], dA / (dA + dB))` at speed `peak`, `i += 1`. Then `i += 1`.
     - else `i += 1`.
   - **Pass 5, times** (`0x10263991`): `flElapsed[0] = 0`; for `i = 0 .. n−2`: `flDist[i]`
     recomputed (3-D); `v[i] > 0 || v[i+1] > 0` → `flTime[i] = flDist[i] / (0.5·(v[i] +
     v[i+1]))`, else `1.0`; `flDist[i] < 0.01 || flTime[i] < 0.01` → remove entry `i+1`, retry;
     else `flElapsed[i+1] = flElapsed[i] + flTime[i]`.
   - **The read** (`0x102646c0`) — sampled at **time, not at the body's place on the path**:
     `speed = |m_vecVelocity|`; with more than one entry, the first `i >= 1` whose `flElapsed[i] >
     interval` (the tick's interval, `m_flMoveInterval`): `a = interval / flElapsed[i]` (**the
     divisor is `flElapsed[i]`, not the segment's own time**), `speed = (1 − a)·v[i−1] + a·v[i]`;
     no such entry (the interval outruns the script) → the current speed stands.
   - **The step** is retail's trapezoid, `dist = (|v_now| + speed) × interval × 0.5` (double
     `0x10449270`), and **lands on the waypoint by clamping** (`0x10264916`): `dist >=` the
     remaining distance to the waypoint → `dist =` that distance (the unused interval re-enters
     the navigator's loop in retail; under K1 the body's next tick takes it — say so at the
     line). `velocity = move direction × speed`. That is what `MaxWalkSpeed` is set from.
   - The profile to expect, one straight leg from standing: accelerate at `ideal + 50` to
     `ideal`, cruise, brake at `ideal + 50` to 0 at the goal; braking distance `ideal² / (2·(ideal
     + 50))` — for the female `walk_0` (53.16 u/s) 13.7 units = 26.1 cm over 0.515 s.
   No constant is left to read. Settled and not needed by this item (`packets-S5.md` item 2):
   `0x102e0bd0` is `CAI_Motor::MoveGroundStep(newPos, pMoveTarget, newYaw, bAsFarAsCan, bTestZ,
   pTraceResult, bNoTrace)` — answers 0 refused / 1 moved / 2 partial, NPC / 3 partial, world / 4
   hit the target; a refused step zeroes the motor's velocity (motor slot 10 `0x102e1440`). The
   body's own movement stands for it under K1.
3. **The crowd follower's slowdown at goal goes off** (`SetCrowdSlowdownAtGoal(false)` in
   `ApplyCrowdState`, beside `SetCrowdSeparation`) **because item 2's deceleration replaces it** —
   in the same change, never alone. Comment at the line: retail's stop is the velocity script's
   constant deceleration to the last waypoint (`0x102630b0`); Detour's distance-scaled slowdown is
   not retail's and is what made N13's creep (inferred until the B integrator's measured toggle).
4. **The arrival tolerance.** Retail's (navigator slot 16 `0x102ef510`;
   `docs/vtmb/navigation-jump-links.md` § "The arrival test and waypoint advance"): reached iff
   the distance is `<= 0.0625` units (double `0x10451f78`; **0.119 cm** at 1.905 cm per unit),
   **2-D** on a ground move — a constant, not the goal tolerance and not a hull — and the body
   lands on the waypoint by item 2's clamped step. `FollowerArrivalFloorCm` 1.0 is **not
   retail's**. Whether it is replaced by 0.119 cm or kept as a named divergence of the crowd
   follower (K1) is **the judge's call, not yours**: until it is ruled, leave the value, and
   rewrite the comment at its line to say exactly that (retail 0.0625 units 2-D, `0x102ef510`;
   1.0 cm is the crowd follower's floor, a K1 divergence pending the judge). Report it.
5. **The second accessor**, for B2 (you own the interface file this wave): `bool
   GetNpcMoveFacingYaw(float& OutYawDegrees) const` — the active move's direction along its path
   at the body's place (waypoint to next waypoint), false when no move is under way. Retail eases
   it through corners with the turn script `0x102627e0` (`packets-S1.md` item 6, last paragraph):
   entry 0 the current yaw; per velocity-script entry that carries a waypoint, `out` = yaw of
   (next − this) or, on the last, the navigator's arrival direction (`0x102ee5b0`), `in` = yaw of
   (this − the previous turn entry's location); `|AngleDiff(out, in)| <= 0.1` (double
   `0x104493d0`) → no entry; else yaw `= 0x1013d450(out, in, |diff| × 0.8)` (double
   `0x104491a8`); then backward, `i = n−1 .. 2`: `limit = flTime[i−1] × 150.0` (float
   `0x10457f60`, degrees per second of segment time); `|AngleDiff(yaw[i−1], yaw[i])| > limit` →
   `yaw[i−1] = 0x1013d450(yaw[i−1], yaw[i], limit)`; then `0x10262c20(i, i+1)` over the entries.
   **The two helpers are read** (`packets-S4.md` f.1; J14.6) — port the easing whole:
   - **`0x1013d450` = `UTIL_ApproachAngle(target, value, speed)`**: both angles reduced to
     0..360, `delta = target − value` wrapped to ±180, `speed` made positive; `delta > speed` →
     `value + speed`; `delta < −speed` → `value − speed`; else `target`. So
     `0x1013d450(out, in, |diff| × 0.8)` **starts from `in` and moves toward `out`**, and the
     backward pass moves `yaw[i]` toward `yaw[i−1]` by at most `limit`.
   - **`0x10262c20(i, j)`**: `yaw = VecToYaw(pos[j] − pos[i])`; `tIn = |AngleDiff(yaw, yaw[i])| /
     150`, `tOut = |AngleDiff(yaw[j], yaw)| / 150` (f32 `0x10457f58` = 1/150); `T = time[j] −
     time[i]`. With 0.01 (f64 `0x1044e658`) and 0.8 (f64 `0x104491a8`): both `>= 0.01` and `tIn
     + tOut <= T` and `< 0.8 T` → **two** inserted entries (`0x10262ea0(i, tIn)`,
     `0x10262ea0(i, tOut)`), each with `yaw`, return 2; only `tIn >= 0.01` and `tIn <= 0.8 T` →
     **one** at `tIn`, return 1; `tIn < 0.01` and `tOut <= 0.8 T` → **one** at `T − tOut`,
     return 1; else 0. (`0x101d2c70` / `0x1013d580` as `VecToYaw` / `AngleDiff` are inferred
     from their use; **`0x10262ea0(i, t)`, settled (`packets-S5.md` item 2)**: from entry `i`, the
     first entry `k` with `t <= time[k]`: `a = t / time[k]`; `time[k] −= t`; a new entry at `k+1`
     with `time = t`, elapsed and location lerped by `a` between `k` and the old `k+1`; returns
     `k+1` (0 when it runs off the end). The two durations are swapped against the SDK — as read;
     the lerp reads `k+1` before the shift, so guard the last entry and say so.)
   The accessor answers the turn script sampled at the tick's interval, as `MoveGroundExecute`
   reads the yaw "the same way" as the speed (item 2's read).
6. **Tests**: `Elysium.Arm.NpcKernelMotor.VelocityScript` (`0x102630b0`) on fixture paths — a
   straight leg accelerates at `ideal + 50`, cruises at the ideal (pass 4's two inserted points)
   and reaches 0 at the last waypoint in finite time, braking over `ideal² / (2·(ideal + 50))`;
   **a 90° corner slows to `ideal × 0.2`**; an ideal of 0 runs at 50.0; pass 3 leaves a slowing
   pair that does not fit its segment uncorrected (retail's `dv > 0`); the read is `a = interval
   / flElapsed[i]` and an interval past the script's end keeps the current speed; the trapezoid
   step clamped to the remaining distance. No test of the crowd follower. Delete a test that
   pins the body's own speed as the authority, listed in your report.

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
the constants and the cell each came from, the accessors' exact signatures and header, tests
(the turn script's insert rule among them: two entries, one at `tIn`, one at `T − tOut`, none),
the arrival floor left for the judge, what stayed unrecovered (`0x102e0bd0` and `0x10262ea0`
are settled, `packets-S5.md` item 2).
