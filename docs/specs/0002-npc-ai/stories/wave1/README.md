# Step 1, wave 1 (Python) — briefs

Three coders, disjoint files, one checkout (spec rule 8), then one integrator.

| brief | story | model |
|---|---|---|
| `brief-T1.md` | T1, the `kernel_*` tools under budget | Opus/high |
| `brief-T2.md` | T2's Python half: the corpus MCP, the address index, standing query verbs | Sonnet/high |
| `brief-T4.md` | T4, the runner, the lease and the waits; T3's `pytest` half | Sonnet/high |
| `brief-integrator.md` | the wave's build-free integration: default `pytest`, every `--check` byte-identical, timings, one commit | Opus/high |

## Rules every coder in this wave follows

- Read `CLAUDE.md`, `pipeline/CLAUDE.md`, `docs/specs/0002-npc-ai/spec.md` (§ standing rules and
  § Step 1) and your brief. Your numbers come from `consolidation/findings-C-tooling.md`,
  `findings-E-time.md` and `findings-F-tests.md`.
- **The query budget:** run every query command with a 60 s timeout; past 10 s note it; at 60 s
  stop and make it faster, never retry it as-is.
- **No polling, no shell for text:** search and read with the built-in Grep / Read / Glob tools.
- **You do not build, launch the editor or run a suite.** You may run the single test file of the
  module you changed, and the tool you are changing itself (that is your work). The integrator runs
  the default `pytest` and the checks across the wave.
- **Own only your files** (listed in your brief). If you need a change in another lane's file, say
  so in your report; do not make it.
- Do not commit. Do not use git worktrees. Match the surrounding code's style and comment density.
- Your final message is your report, ≤300 words: what changed (files), measured before/after,
  what you could not do and why.
