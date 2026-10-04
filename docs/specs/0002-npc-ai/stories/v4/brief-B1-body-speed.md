# Brief B1 — V4b: the body walks at the kernel's ideal speed (coder; no build) — PROVISIONAL until R1

Read `README.md` here (§1 "Turning and the walk"; §2 M5–M6; §7 K1; §8 Q2), `packets.md` § R1 (all of
it: **this brief is final only once R1's verdict on N13 is in**, and the planner's or the
coordinator's fill-in below replaces the marked parts), `stories/v1/triage.md` N13. After V4a's
commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.cpp` (`CommandedTravelSpeed` ~:335-342, the
  `MaxWalkSpeed` write ~:475-482, `ApplyFacingTarget` ~:1085-1092, the orient mode ~:107)
- `Source/ElysiumUE/Private/Visual/ElysiumAnimationDriver.cpp` (the fan read ~:597-612, the move-yaw
  slew ~:663-666)
- `Source/ElysiumUE/Private/Visual/ElysiumLocomotionSample.cpp` (the body's measured `move_yaw`)
- the embodiment interface through which the body reads the kernel (one accessor: the kernel's
  `GetIdealSpeed`, and the kernel's `move_yaw` if R1 needs it) — name the header in your report
- tests: the body's gait tests that pin the body-owned speed (Grep `CommandedTravelSpeed`), if any

## The job (as far as is honest before R1)

1. **The motor's speed input is retail's.** Under K1 (the motor on the body's tick), the commanded
   speed is the kernel's `GetIdealSpeed 0x10091740` = `m_flGroundSpeed +0x654` (V4a writes it every
   advance at the kernel's pose parameters, and B2 makes the kernel's `move_yaw` retail's), not the
   body's own fan read at its own measured `move_yaw`. The body reads it through the embodiment
   accessor each tick; the conversion to cm/s is stated at the line. A body with no live kernel
   (a non-NPC) keeps today's path.
2. **N13's cause — TO BE FILLED from R1 § item 4.** Expected shapes:
   - *the lead holds* (body yawed ~90° off its path): remove the cause R1 names on the body side
     (e.g. a focal point left set with an empty queue, orient-to-movement off), so the body yaw
     follows the kernel's heading (B2) and the measured `move_yaw` ≈ the kernel's;
   - *the lead is refuted, the fan's cell or scale is wrong in the baked data*: not this lane — the
     judge's ruling decides (README §8 Q2); this lane then only does item 1;
   - *other*: as R1 states it.
3. **Tests**: none new of a port mechanism; the acceptance is the records (B integrator). Delete a
   test that pins the body's own speed as the authority, listed in your report.

## Not yours

The kernel's `move_yaw`, the facing queue, the ideal yaw (B2); the clock (V4a); the weighted pick.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.
