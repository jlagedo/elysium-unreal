# Brief — V4b's integrator

**Final (amended after V4r and settling packet S1, 2026-10-04 — R1, J9, S1).** Runs after R1b, B1
and B2 report (B1 no longer waits on R1b: its constants, the five passes of `0x102630b0` and the
arrival tolerance are in its brief from `packets-S1.md` item 6; R1b now answers only why the
port's `task_face_enemy` ran 13.4 s, for B2 item 4). Read
`README.md` here, both briefs, `packets-R1.md`, `packets-S1.md` items 5–7, `packets-R1b.md`, the ruling J9
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it), `stories/v1/triage.md` N13,
`Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more (expected: the accessors'
   signatures between B1 and B2; a slow-turn line in a B1 file, or in a V11-1 file when V11
   shares the wave (`ElysiumNpcBaseStartTask.cpp`, `ElysiumNpcStartTask_2.cpp`: a
   `TASK_FACE_ENEMY` arm against `0x102a4417` / `0x102aae61`); `kernel_verdicts.tsv`'s row for
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
   - `face_enemy_turn`: green — `taskdone` within 0.5 s of `task_face_enemy` (retail: `RunTask`
     0x2e's Troika arm `0x102aae61`, 0.1 s on the first call then real elapsed time at 900°/s;
     `packets-S1.md` item 5). If R1b answered "not
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
   input) and anything left unrecovered that the lanes carried as a named seam: the turn
   script's `0x1013d450` and `0x10262c20` (B1 item 5, the unsmoothed direction if still unread),
   slot 526 on the Troika line (B2), R1b's "not determined" if so. **For the judge, filed in
   `report-b.md`, not decided by you or the coder**: `FollowerArrivalFloorCm` 1.0 cm against
   retail's arrival tolerance of 0.0625 units (0.119 cm, 2-D, `0x102ef510`) — replace it or keep
   it as a named K1 divergence; B1 left the value and the comment. A V4o note: `cover_move_shoot`
   can show `cond+ 0x61` (`NOT_FACING_ATTACK`) instead of `0x4f` if the running gunman's body does
   not turn to the facing target — that is B2's blend (`0x102e1a83`), read here if V4o reports it
   (`packets-S3.md` item 4.3).
7. **Commit once**: `fix(npc): V4b -- the body's speed is retail's velocity script and its stop;
   slot 15's influence and slot 18's move facing; the turn; N13`. Tick nothing. Do not push.

Rules: wait for a build or run by its completion notification, never a sleep or polling loop. The
query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text through Grep / Read /
Glob. Report ≤300 words: the build's wall time, the measured toggle, verdicts before and after,
the measured mid-leg speeds and `arrived` times, totals, what is left red and where.
