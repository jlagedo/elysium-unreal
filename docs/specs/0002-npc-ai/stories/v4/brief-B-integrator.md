# Brief — V4b's integrator

**Final (amended after V4r, 2026-10-04 — R1 and J9).** Runs after R1b, B1 and B2 report. Read
`README.md` here, both briefs, `packets-R1.md`, `packets-R1b.md`, the ruling J9
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it), `stories/v1/triage.md` N13,
`Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more (expected: the accessors'
   signatures between B1 and B2; a slow-turn line in a B1 file; `kernel_verdicts.tsv`'s row for
   `0x102e19e0`).
2. **Build once** (`uv run elysium build`). Fix only integration breaks. A second build only for an
   integration break of your own fix; a third means stop and report.
3. **First, before anything else is trusted: measure the toggle** (J9). Run
   `uv run elysium arena input_clearpatrolpath` and read its trace: before V4b `arrived` came at
   10.428 s against ~5.6 s of travel (`packets-R1.md` item 4). **If the creep survives** — the
   body still spends seconds on the last ~30 cm — **stop**: the cause is unread again, B1's items
   2–4 do not land (revert them, keep nothing on a guess), and you report the trace. If it is
   gone, write the measured `arrived` time into `stories/v4/report-b.md` and into R1's cause text
   in the four N13 records (the word "inferred" goes: it is toggled now).
4. **Run, by name**: `uv run elysium arena patrol_sentry2_pingpong patrol_monk_loop
   input_clearpatrolpath places_pedestrian_visit face_enemy_turn anim_footsteps_walk cover
   map_tutorial_idle`. Then the family filters once: `uv run elysium test Elysium.Arm.NpcKernelMotor.
   Elysium.Arm.NpcKernelFacing. Elysium.Arm.NpcKernelAnim.` and the body's gait tests
   (`Elysium.Substrate.` filter the lanes name).
5. **Acceptance** (README §4, V4b; §9 H18):
   - The three patrols and `places_pedestrian_visit`: green, `known_red` removed. Add to each, with
     H18, a `speed2d` probe mid-way along its longest straight leg, **within 10 % of that body's
     own `walk_0`** — the value the seam wrote into the record from the body's bank: sentry2
     (`vampire_hunter_chick`, female) 101.278 cm/s → `greater` 91.2, `less` 111.4; a male-bank
     body 136.683 → `greater` 123.0, `less` 150.4; state the bank and the cell in the record's
     `about`. Never 136.7 for a body on another bank. The probe guards the cruise speed (already
     right); what turns the record green is the leg's time, now without the creep. A body whose
     bank the seam could not read gets no speed probe, and the report says so. A residual red is
     read against the retail chain and placed under the bug protocol, not loosened.
   - `face_enemy_turn`: green — `taskdone` within 0.5 s of `task_face_enemy`. If R1b answered "not
     determined" and B2 left the turn undone, it stays red: retarget `known_red` to the unread
     cause with the measurement's line, and say so in the report.
   - `anim_footsteps_walk`, `cover`, `map_tutorial_idle`: unchanged verdicts (`map_tutorial_idle` is
     boot-dependent, Q-V13a — note, do not fix). The footstep events of a leg's last metre move
     with the retail deceleration: a moved `anim_footsteps_walk` is read, not loosened.
6. **Story close**: the arm tier once, the whole arena once, the default tier once; moved verdicts
   in `stories/v4/report-b.md` (record, before, after, why). Every record green before V4b stays
   green; arrival times everywhere move — a record whose deadline was tuned to the creep is a
   record error to correct with its retail source, a record now failing for a new reason is
   triaged. `report-b.md` states K1 as it now stands (the whole retail speed curve as the body's
   input) and anything R1b left unrecovered that the lanes carried as a named seam.
7. **Commit once**: `fix(npc): V4b -- the body's speed is retail's velocity script and its stop;
   slot 15's influence and slot 18's move facing; the turn; N13`. Tick nothing. Do not push.

Rules: wait for a build or run by its completion notification, never a sleep or polling loop. The
query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text through Grep / Read /
Glob. Report ≤300 words: the build's wall time, the measured toggle, verdicts before and after,
the measured mid-leg speeds and `arrived` times, totals, what is left red and where.
