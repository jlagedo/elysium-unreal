# Schedule

478 stories, 736 slice briefs, bundled into **337 worker runs** (`<layer>/runs.md`): one
Codex worker per run, one build at a time per checkout. Durations: 25 min fixed + 20 min per KB of
retail code per run, a 5-min gate after each, 45 min to close each layer (0002's measured runs,
2026-10-04/05: a diagnosed cause closed in 40-60 min).

| stage | runs | functions | worker-hours | runs ready now |
|---|---|---|---|---|
| L0 | 88 | 391 | 77 | 88 |
| L1 | 15 | 56 | 13 | 9 |
| L2 | 106 | 396 | 88 | 76 |
| L3 | 51 | 178 | 44 | 21 |
| L4 + L5 | 77 | 264 | 81 | 44 |
| **all** | 337 | 1285 | 307 | 238 |

Without bundling (one worker per slice brief, ~60 min each) the same work is ~736 worker-hours.

## Elapsed time by lanes

| lanes | strict layer order (R1 today) | a run starts when what it calls below is done (D1) |
|---|---|---|
| 1 | 307 h | 307 h |
| 2 | 157 h | 154 h |
| 3 | 111 h | 105 h |
| 4 | 95 h | 81 h |

More than one lane needs a second checkout or worktree (`decisions.md` D2); lanes take runs with
disjoint files. Hours are worker hours at the measured pace, not calendar time.

