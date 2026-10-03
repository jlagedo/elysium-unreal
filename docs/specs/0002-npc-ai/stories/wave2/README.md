# Step 1, wave 2 (C++) — briefs

Three coders on disjoint files in the one checkout (spec rule 8), then one integrator who builds.

| brief | story | model |
|---|---|---|
| `brief-A-scenarios.md` | T5: the scenario record, the runner, the headless arena run, `gr_scenario <name>` | Opus/high |
| `brief-B-trace.md` | T5: the trace-event seam and its taps, the `arena` launcher; T2's C++ half (the `elysium` MCP caps) | Opus/high |
| `brief-C-tiers.md` | T3's C++ half: the test tiers, the deletions, the slow tests, the warnings | Sonnet/high |
| `brief-integrator.md` | one build, the default run, the arm tier once, the arena suite, one commit | Opus/high |

`seam.md` is the interface lanes A and B share. It is fixed: B adds it verbatim, A codes against it.

## Rules every coder in this wave follows

- Read `CLAUDE.md`, `.claude/rules/cpp.md`, `docs/specs/0002-npc-ai/spec.md` (§ standing rules, § Step 1),
  `docs/harness/green-room-arena.md`, `seam.md` and your brief.
- **You do not build, launch the editor or the game, or run a suite.** Nothing in this wave is
  compiled until the integrator builds, so write to compile: include what you use, check every
  signature you call by reading its declaration, no guessed APIs. A coder may run the single
  Python test file of a Python module it changed.
- **The query budget:** every query command under a 60 s timeout; past 10 s note it; at 60 s stop
  and make it faster. Look an address up with `uv run elysium research where <address>` (also
  `section`, `verdict`, `cited`, `rows`; see `CLAUDE.md`) instead of searching `docs/vtmb`.
- **No polling, no shell for text:** search and read with the built-in Grep / Read / Glob tools.
- **Own only your files** (listed in your brief). A change you need in another lane's file goes in
  your report, not in the file. Two lanes edit `pipeline/src/elysium_pipeline/cli.py` (B appends
  the `arena` command, C edits the `test` command): re-read the file immediately before each edit
  and touch only your function.
- Nothing here may change what an NPC does: this wave builds instruments. A kernel edit is a tap
  behind `HasAiTraceSink()` or it is out of scope.
- Do not commit. No worktrees. Match the surrounding code's style and comment density.
- Your final message is your report, ≤300 words: files changed, what is unverified because nothing
  was built, what you need from another lane.
