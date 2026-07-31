# Research (`research/`)

This tree contains reproducible research intent and authored instruments, never evidence
copied from the game or a decompiler.

- `tooling/ghidra/scripts/` contains tracked analysis scripts.
- `tooling/ghidra/driver/` contains the headless runner, context builder, and parsers.
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

Research commands run through `dev/elysium.ps1 research <case>`. A command that modifies
the original VtMB install must require an explicit install action and provide a verified
uninstall path.
