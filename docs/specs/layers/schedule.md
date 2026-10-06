# Schedule

447 stories, 588 slice briefs, bundled into **306 worker runs** (`<layer>/runs.md`): one
Codex worker per run, one build at a time per checkout. Durations: 25 min fixed + 20 min per KB of
retail code per run, a 5-min gate after each, 45 min to close each layer (0002's measured runs,
2026-10-04/05: a diagnosed cause closed in 40-60 min).

| stage | runs | functions | worker-hours | runs ready now |
|---|---|---|---|---|
| L0 | 81 | 372 | 73 | 81 |
| L1 | 14 | 56 | 12 | 8 |
| L2 | 104 | 396 | 87 | 75 |
| L3 | 50 | 178 | 44 | 20 |
| L4 + L5 | 57 | 168 | 45 | 42 |
| **all** | 306 | 1170 | 265 | 226 |

Without bundling (one worker per slice brief, ~60 min each) the same work is ~588 worker-hours.

## Elapsed time by lanes

| lanes | strict layer order (R1 today) | a run starts when what it calls below is done (D1) |
|---|---|---|
| 1 | 265 h | 265 h |
| 2 | 136 h | 133 h |
| 3 | 97 h | 90 h |
| 4 | 83 h | 68 h |

More than one lane needs a second checkout or worktree (`decisions.md` D2); lanes take runs with
disjoint files. Hours are worker hours at the measured pace, not calendar time.

