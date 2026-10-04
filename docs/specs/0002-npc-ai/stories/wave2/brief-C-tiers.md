# Brief C — T3's C++ half: the test tiers

Read `README.md` here first, then spec § "The test budget" and § T3, and
`consolidation/findings-F-tests.md`.

## Inputs

- `E:\elysium-work\scratch\findings-f\per_test_categories.tsv`: one row per test with its category
  (census, arm, unit, scenario, infra, content, port-only, seam, stub-count, duplicate). The
  categories are pattern reads (±10–15% at the arm / unit line): check a test's body before you
  delete it.
- `consolidation/findings-F-tests.tsv` (per file) and `findings-A-tests.tsv` (the NPC scope, with
  the duplicate addresses and their owning files).

## Job

1. **Delete** the seam tests (16 whole tests, and the ~130 seam assertion lines inside other
   tests — delete the lines, keep the tests), the stub-count tests (13) and the duplicates (13;
   the owning file keeps the assertion). **Do not delete the 21 port-only tests** (the arbiter,
   the executors, the owner enum): they go in step 2 with their mechanisms.
2. **Tiers by name.** A test's tier is its name's prefix, so a prefix filter selects a tier:
   - arm, unit and infra tests → `Elysium.Arm.…` (opt-in, run at a story's close): rename
     `Elysium.Substrate.<Rest>` to `Elysium.Arm.<Rest>` and `Elysium.<Group>.<Rest>` to
     `Elysium.Arm.<Group>.<Rest>` for the other groups;
   - content tests not already under it → `Elysium.Content.…`;
   - `MapActorTeardown` and `SkeletalStageOwners` → `Elysium.Slow.…`;
   - census tests, scenario tests and the smoke set keep their names: they are the default tier.
   Generated test files keep their names (do not touch a generator). Where names are built by a
   macro or a shared prefix constant, change the constant, not every use.
3. **The smoke set**: at most three arm tests per family (six for the NPC kernel) stay in the
   default tier — the arm whose absence a shipped program would notice, chosen by reading, not by
   assertion count. List them with one line each on why.
4. **The default run.** `uv run elysium test` with no prefix runs the default tier: the explicit
   `+`-joined list of the default-tier groups (everything under `Elysium.` except `Arm`,
   `Content`, `Slow`), kept in one place in the pipeline with a test that fails when a new
   top-level group appears in `Source/ElysiumUE/Private/Tests/` and is in neither list;
   `uv run elysium test --all` runs `Elysium.`; a named prefix runs as today. The summary names the
   tier.
5. **`MapActorTeardown`'s 50 s** (58% of all C++ test time): read the test and what it drives and
   find where the time goes (no run). If the cut is inside `Tests/**`, make it. If it is in
   production code, write the exact change for the integrator in your report and leave the code.
6. **The warnings.** Eight messages are 70% of 28,102 warnings (`findings-F` § 2): the NPC fixture
   has no eye offset, follower-distance overlap, `Stub fired`, the occluded-reaction percentages,
   the squad and the model admission. For each: if the fixture causes it, fix the fixture (give it
   the data) or declare the message expected in the fixture's scope; a message that carries signal
   in the live game keeps its verbosity. State per message what you did. "Passed with warnings"
   must be rare enough to read.
7. **The pointers**: `AGENTS.md`'s `uv run elysium test` lines describe the tiers; pipeline tests
   that name C++ tests follow the renames. Old records under `docs/` are history: leave them.

## Files you own

`Source/ElysiumUE/Private/Tests/**` (not the generated ones' generators), the `test` command in
`pipeline/src/elysium_pipeline/cli.py`, the test launcher and report code in `unreal.py`, their
pipeline tests, the test lines of `AGENTS.md`. Not `Debug/**`, `Substrate/**`, `Public/**`, `Map/**`
(lanes A and B, and nothing in this wave changes behaviour), not the `arena` command (lane B).

## Report

Counts before and after per tier, the deletions by category, the smoke set, the `MapActorTeardown`
finding, the warnings table.
