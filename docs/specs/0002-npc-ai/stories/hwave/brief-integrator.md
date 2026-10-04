# Brief — wave H's integrator

Runs after coders A, B and C report (their reports are given to you). You are the only agent that
builds. Read `README.md` here, the three briefs, `stories/v1/triage.md` (§ "The harness wave",
§ "Records whose verdict depends on occlusion", § "The fix order"), `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported (an accessor, a kind string), nothing more.
2. **Build once**: `uv run elysium build`. Fix only integration breaks (compile, link, a wrong
   call across two lanes' files). A second build is allowed; a third means stop and report.
3. **Add the self-test records** the coders described under `Arena/scenarios/_selftest/`.
4. **Update the records the wave unblocks** (records state retail; change only what the harness
   now lets them say, and say so in `stories/hwave/report.md`):
   - `never` with `at_most` / `after` where the triage says a record needs it: the roll call's
     `taskfail` storm (`rollcall_vtaxidriver`, `rollcall_vrat` and the rest), `fail_route_*`,
     `input_disablethink`, `maker_respawn`, "nothing after death";
   - `verbs_stealth_kill`: the `player_crouch` action, the player probes, a `stealthkill`
     expectation;
   - `unknown_crouched_band`: `player_crouch` and `light_pin`;
   - `cover_armed`: a `player_weapon` probe.
5. **Run**: `uv run elysium arena` (the whole suite, once), then by name any record you changed
   after it. `uv run elysium test` (the default tier) once.
6. **Acceptance** (triage § "The fix order", row H): `rollcall_vcamera`, `rollcall_vcamerasecurity`,
   `sense_cone_enter`, `memory_occluded_kept` run and are green or carry a game red with its
   retail chain; no record errors because an earlier one failed to stage; no record's verdict
   changes with its position in the boot (run `sense_beyond_vision` after `verbs_stealth_kill`);
   `verbs_stealth_kill` is classified from its `stealthkill` events (bug protocol: record error,
   harness, or game red with the refusing gate's retail address); `cover`, `cover_armed`,
   `cover_reclaim` and `sense_bodies_transparent` re-read now that walls block sight, each
   verdict restated. `rollcall_vzombie` (H11) is not this wave's: leave it.
7. **Any verdict that moved** gets a line in `stories/hwave/report.md`: record, before, after,
   why. A new game red is filed in `stories/v1/triage.md` under the bug protocol (placed on a
   step-2 story); you do not fix game code.
8. **Commit once**, when the build, the default tier and the suite's harness self-tests are green
   and every other record is `pass` or `expected-fail`: message
   `feat(arena): step 2 wave H -- <what landed>`. Tick `H` in `docs/specs/0002-npc-ai/spec.md`
   and `docs/specs/TRACKER.md` with the measured result in the same commit. Do not push.

Rules: a build or a suite run blocks until done — wait for it (a generous timeout) or for its
completion notification, never a sleep or a polling loop. The query budget (10 s warns, 60 s
stops), text through the built-in Grep / Read / Glob tools. Report ≤300 words: the build's wall
time, the suite's totals before and after, the verdicts that moved, what is left red on the
harness.
