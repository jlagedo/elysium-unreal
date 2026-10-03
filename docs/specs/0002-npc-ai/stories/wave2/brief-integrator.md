# Brief — wave 2 integrator

Runs after the three coders report. Read `README.md`, `seam.md`, the three briefs and the coders'
reports (in your prompt). Nothing in this wave has been compiled: you hold the build.

## Job

1. `git status`: every changed file belongs to one lane's list; anything else is reported.
2. **Build** (`uv run elysium build`, foreground, it waits and ends with a verdict). Fix what stops
   it — compile and link errors, a signature two lanes disagree on (`seam.md` decides), a missing
   include. Fix only integration breaks; a lane's unfinished job is reported, not redone. Count the
   builds and report each one's duration.
3. **Run, each once:**
   - `uv run elysium test` (the default tier): ≤25 s wall, zero failures; the count of "with
     warnings";
   - `uv run elysium test Elysium.Arm`, `Elysium.Content`, `Elysium.Slow`: green; times;
   - `uv run elysium arena`: `cover` is `expected-fail` and its first unmet expectation is the one
     known red 1 predicts (the sequence at rate 0 / never finished), `control_sequence` passes, both
     self-tests pass, the suite's wall time (target ≤30 s for the arena host). If `cover` fails
     earlier or elsewhere, read its trace and say exactly where and why: that is a finding, not
     something to tune the record around;
   - the default `uv run pytest` (≤20 s, zero failures) and `uv run elysium research kernel --check`;
   - live: `uv run elysium gr --arena --headless` in the background, then over the `elysium` MCP
     tools (one call at a time): `elysium.gr_scenario cover` and `npc_trace_tail` agree with the
     headless trace; `elysium_entity_get` on `arena_gunman` with defaults, `brief` and `fields`;
     `elysium_console_exec` and `elysium_log_tail` defaults — no default reply over 20 KB. Quit the
     game when done.
4. Apply lane C's `MapActorTeardown` change if it handed one over, and re-time that test.
5. Compare against gate 1's numbers (spec § Step 1). A miss is reported with its measurement.
6. Tick T2, T3 and T5 in `docs/specs/0002-npc-ai/spec.md` and `docs/specs/TRACKER.md` for what is
   verified; write `stories/wave2/report.md` with the numbers.
7. Commit when green, staging explicitly: `feat(arena,tests): step 1 wave 2 -- …`, per lane what
   landed and the numbers. Do not commit a red wave; do not push.

No polling loops; every query under 60 s; text through Grep / Read. Report ≤300 words.
