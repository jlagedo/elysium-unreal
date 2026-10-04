# Brief — V4b's integrator

Runs after B1 and B2 report. Read `README.md` here, both briefs (as filled from R1), `packets.md`
§ R1, the judge's ruling on Q2 if there is one, `stories/v1/triage.md` N13, `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **Build once** (`uv run elysium build`). Fix only integration breaks. A second build only for an
   integration break of your own fix; a third means stop and report.
3. **Run, by name**: `uv run elysium arena patrol_sentry2_pingpong patrol_monk_loop
   input_clearpatrolpath places_pedestrian_visit face_enemy_turn anim_footsteps_walk cover
   map_tutorial_idle`. Then the family filters once: `uv run elysium test Elysium.Arm.NpcKernelMotor.
   Elysium.Arm.NpcKernelFacing. Elysium.Arm.NpcKernelAnim.` and the body's gait tests
   (`Elysium.Substrate.` filter the lanes name).
4. **Acceptance** (README §4, V4b):
   - The three patrols and `places_pedestrian_visit`: green, `known_red` removed. Add to each, with
     H18, a `speed2d` probe mid-way along its longest straight leg, `greater` 123 and `less` 150
     (retail `walk_0` 136.7 cm/s ± 10 %; state the source in the record's `about`). A residual red
     is read against the retail chain and placed under the bug protocol, not loosened.
   - `face_enemy_turn`: green within R1's bound.
   - `anim_footsteps_walk`, `cover`, `map_tutorial_idle`: unchanged verdicts (`map_tutorial_idle` is
     boot-dependent, Q-V13a — note, do not fix).
5. **Story close**: the arm tier once, the whole arena once, the default tier once; moved verdicts
   in `stories/v4/report-b.md` (record, before, after, why). Every record green before V4b stays
   green; walk timings everywhere move — a record whose deadline was tuned to the slow walk is a
   record error to correct with its retail source, a record now failing for a new reason is triaged.
6. **Commit once**: `fix(npc): V4b -- the body walks at the kernel's ideal speed; slot 18's
   move_yaw; N13`. Tick nothing. Do not push.

Rules: wait for a build or run by its completion notification, never a sleep or polling loop. The
query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text through Grep / Read /
Glob. Report ≤300 words: the build's wall time, verdicts before and after, the measured mid-leg
speeds, totals, what is left red and where.
