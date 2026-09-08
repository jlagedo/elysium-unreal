# 0000 play-tier — the beat-script driver that plays every other spec's witness from real input

## Witness
`uv run elysium test Play` drives a real headless world through a beat script and reports pass/
fail: a fourth automation tier, distinct from unit/Substrate/Content tiers, that walks
`sp_tutorial_1`'s tutorial opening unassisted on injected input and fails loudly on a beat
regression. This is the acceptance harness every other landed spec's played-beat witness cites —
"beat-scripted in the Play tier" is the project's definition of played acceptance, as opposed to
a dev-console shortcut, which is never acceptance evidence.

## Scope
- Roadmap rows absorbed: 11.10 Play test tier (the beat-script driver; PP0's finish).
- Out of scope (belongs to another spec or is parked):
  - 11.13 Reconstruction camera director (11.13d–h) — consumes this spec's harness for its
    played-camera acceptance (11.13h explicitly depends on 11.10) but is specified elsewhere.
  - CCC8 Played acceptance (the 3 C's slice finish line) — owner-played, cites this harness.
  - 13.1–13.5 tutorial mechanics' played lessons and 13.4's Play-tier shot baselines/beat script —
    each mechanics spec authors its own beat script against this driver.
  - PP2/PP3/PP6 rung acceptance (theatre, tutorial landing, tutorial completion) — each stages its
    own beats on this driver; not specified here.

## Requirements
1. A beat-script format with `do` / `wait` / `assert` / `shot` verbs, evaluated over the command
   registry (11.6), that can: inject input, wait on a condition, assert a predicate, and capture/
   compare a shot baseline.
2. Predicates read `G` (global state), quest state, and entity state to gate `wait`/`assert`
   steps.
3. Injected input drives the world exactly as a live player would — the harness is real input
   into the command registry, not a state write. Dev shortcuts are never acceptance evidence
.
4. Command-stream replay: a recorded input stream can be replayed deterministically against a
   fresh world.
5. A save round-trip step: save mid-beat, reload, and continue the beat script against the loaded
   state.
6. Shot baselines integrate with the existing screenshot-regression harness (2.9,
   `-ElysiumShots` + `shots_diff.py`).
7. Matching MCP tools expose the driver to agent-driven runs: `input_inject`, `beat_run`,
   `save`/`load`, `time`.
8. Acceptance: `uv run elysium test Play` walks the tutorial opening (`sp_tutorial_1`) unassisted
   and fails loudly on a beat regression — P9's slice acceptance becomes a CI run.

## Design
A fourth automation tier alongside unit/Substrate/Content, built on the command registry (11.6)
and the MCP surface (2.7, `elysium_*` tools) that already exist. The beat script is the played
witness format the rest of the project's specs write to; this spec only owns the driver, its
verbs, and the MCP tool surface — not any specific beat script content, which belongs to the
spec whose behaviour it proves.

## Seams
- Consumes: 11.6 Command registry + user command (landed [x]), 2.7 Agent-facing MCP surface
  (landed [x], 20 `elysium_*` tools), 2.9 Screenshot-regression harness (landed [x],
  `-ElysiumShots` + `shots_diff.py`). All three are landed infrastructure, not open specs.
- Provides: the beat-script driver, its verbs (`do`/`wait`/`assert`/`shot`), command-stream
  replay, the save-round-trip step, and the MCP tools (`input_inject`, `beat_run`, `save`/`load`,
  `time`) that every 0001–0010 spec's played-beat witness is authored against.

## Tasks
- [ ] **11.10 Play test tier** — the beat-script driver (`do`/`wait`/`assert`/`shot` over the
  command registry, injected input, `G`/quest/entity predicates and the shot baseline),
  command-stream replay, the save round-trip, and matching MCP tools (`input_inject`, `beat_run`,
  `save`/`load`, `time`). Acceptance: `uv run elysium test Play` walks the tutorial opening
  unassisted and fails loudly on a beat regression — P9's slice acceptance becomes a CI run.
  Deps: 11.6, 2.7, 2.9 (all landed).

## Open questions
None stated in the plan beyond the task's own scope; the plan gives no further detail on the
beat-script grammar, file format, or storage location for authored beat scripts.
