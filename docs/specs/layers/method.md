# Method — how a layer is executed

The unit of work is a **run** (`<layer>/runs.md`): consecutive slice briefs of one subsystem that one
worker finishes in one go. What follows replaces 0002's "method per story" (parked).

## Roles

- **The coordinator** — drives the layer. It picks the next ready run, hands its briefs to a worker,
  gates the result, commits it and moves on.
- **Workers** — one per run. A worker's input is the run's briefs, in order (`<layer>/briefs/`); its
  output is the report each brief asks for.
- **Readers** — read-only work when a brief's retail chain is not in `docs/vtmb/` yet: they return
  the walk with addresses; the run waits for it.

## What a worker uses

- The repository, `AGENTS.md`, and `uv run elysium build | test | arena`.
- Retail lookups: the `vtmb-corpus` and `elysium` MCP servers, or `uv run elysium research
  where|section <address>` and the PE reader in `AGENTS.md` § Gotchas.

One run at a time per checkout (one binary). More lanes need a second checkout (`decisions.md` D2).

## The gate after each run (the coordinator's, never the worker's)

1. `uv run elysium build`
2. `uv run elysium test` (default tier)
3. `uv run elysium arena <the run's records> <the records of every story the run touched>`
4. Compare the verdicts with the last gate: a record that passed and now fails is a regression — a new
   cause for a new worker, never an addition to the finished one.
5. Commit the run alone, staged by path, with the records and their verdicts in the message. Never push.

## Closing a layer (`<layer>/spec.md` § gate)

`uv run elysium test arm`, one `uv run elysium arena` of every record (the baseline's and the layer's), a
record that was intermittent in three boot orders, the contract checks (`contract-checks.md`), and the
layer's rows in `audit.tsv` re-read: each is done, or a named divergence the owner accepted. Then the layer
is ticked in `TRACKER.md` and its numbers recorded as the next baseline.

## Rules every brief carries

- Read the retail body before writing (`vtmb_code <address>`); port every arm in retail's order with
  retail's constants; cite each address at its port line.
- An input from the same or a lower layer is built now; a call into a higher layer stays a named hook
  (`hooks.tsv`). A missing lower-layer piece is a planning fault: stop and report it.
- Records state what retail does and use the shared pieces of `harness.md`.
- The worker never runs the full arena or the arm tier, never commits, and touches only what its run needs.
- The query budget: a search over 10 s is logged, one over 60 s is stopped and never retried as is.
