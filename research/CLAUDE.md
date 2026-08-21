# Research (`research/`)

This tree contains reproducible research intent and authored instruments, never evidence
copied from the game or a decompiler.

- `tooling/ghidra/scripts/` contains tracked analysis scripts.
- `tooling/ghidra/driver/` contains the headless runner, context builder, and parsers; its
  `README.md` is the workspace manual — read it before running a Ghidra pass.
- `tooling/ghidra/driver/corpus.py` holds the whole-body corpus: every module's functions
  decompiled, its strings, its named globals, its class vtables, its class structures and the
  field ledger, in one SQLite database, with the disassembly beside it in a second.
  **Query it before running a headless pass** — `uv run elysium research corpus`, or the
  `vtmb_*` MCP tools. A pass is for changing the project, not for asking it a question. The
  corpus reports itself stale when a naming or typing pass has run since it was dumped.
  `callers` answers virtually as well as directly, in labelled sections — VtMB dispatches its
  game logic through vtables, so for most class methods the direct section is empty and an
  unlabelled "none" would be a wrong answer rather than a missing one.
- `tooling/ghidra/driver/repair.py` fixes what Ghidra's analyzer got wrong — function
  boundaries, abandoned jump tables, invented parameter lists — and must run **before** a corpus
  dump, which photographs the project as it stands.
- `tooling/capture/` contains live hook/injector source and capture analysis.
- `tooling/probes/` contains focused surveys and static probes.
- `cases/` contains hash-pinned specifications grouped by topic.
- `experiments/` is for reusable, game-independent spikes.

Ghidra distributions and projects, decompilation, dumps, captured matrices, reports,
models, extracted game trees, and third-party reference source live under
`ELYSIUM_WORK_ROOT/research` or `ELYSIUM_WORK_ROOT/cache`. They must never be committed.

A case records input hashes and provenance, the question, eliminated leads, open
questions, and its consuming `docs/vtmb/` topic or RE identifier. Findings are written
into the owning topic document; generated evidence is not copied into Git.

Research commands run through `uv run elysium research <case>`. A command that modifies
the original VtMB install must require an explicit install action and provide a verified
uninstall path.

That wrapper is not optional: the CLI is what loads `.elysium.local.env`, so invoking a
tool as `uv run python <path>` dies on `ELYSIUM_WORK_ROOT is not configured`. Dispatch
resolves `research/cases/<name>/` first and falls back to a unique
`research/tooling/**/<name>.py`, so a case and a tool may not share a name. Remaining
arguments pass through to the tool, which means `--help` is consumed by the CLI and never
reaches it.
