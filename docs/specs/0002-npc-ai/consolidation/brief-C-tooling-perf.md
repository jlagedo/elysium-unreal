# Brief C — the tooling, audited for time

Scope: `pipeline/` (Python, `uv run elysium ...`), `research/tooling/ghidra/driver/` (the kernel
ledger: `kernel_ledger.py`, `kernel_lists.py`, `kernel_shape.py`, `kernel_reach.py`, `kernel_gate.py`,
`corpus.py`, `corpus_mcp.py`), `pipeline/tests/`, and the automation-test runner in
`pipeline/src/elysium_pipeline/unreal.py` (+ `cli.py`, `tasking.py`, `workers.py`).
The owner reports: "several hours wasted by python scripts taking 15 minutes to validate";
`uv run elysium research kernel --check`, `kernel_lists`, the ledger regeneration and
`pytest pipeline/tests` are the suspects; `uv run elysium test <prefix>` boots the editor every time.

You MAY run timings that finish under 2 minutes each (`time uv run elysium research kernel --check`,
`python -X importtime`, `pytest --durations=20 pipeline/tests -x -q` on a SUBSET, `cProfile` on one
ledger command). Do NOT build the C++ project, do NOT launch the game or editor.

Deliver `findings-C-tooling.md` (≤2 pages) with a table: command | what it does | inputs read
(file, MB, rows) | measured or estimated time | where the time goes | concrete fix (cache keyed by
mtime / incremental / parallel / lazy import / skip) | expected time after. Then:

1. The automation-test path: how `uv run elysium test` launches (commandlet? `-ExecCmds`? editor?),
   the boot cost, whether a cooked/`-game` or `UnrealEditor-Cmd` run with `-nullrhi` `-unattended`
   is used, whether a test subset can run without re-cooking, and whether tests could be run in the
   already-running game through the MCP server (`elysium_console_exec` `Automation RunTests`).
2. The kernel ledger: what `--check` recomputes each time; can the verdict/shape/reach passes be
   cached on the inputs' hashes; can `kernel_lists` and `kernel_shape` share one parse.
3. `pipeline/tests`: total count, the 20 slowest, which are integration tests over the corpus that
   should be opt-in (`-m slow`).
4. The corpus MCP (`vtmb_*` tools) and the game MCP (`elysium_*`): response sizes that blow context
   (`elysium_entity_get` on an NPC ~15k tokens is known); propose a `fields=` filter or a `brief`
   variant for each oversized tool.

Report ≤300 words with the top five fixes ranked by hours saved per week.
