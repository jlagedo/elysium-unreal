# Brief T2 (Python half) — the corpus MCP, the address index, the standing query verbs

Read `README.md` here first (the wave's rules), then spec § T2. The `elysium` game-MCP caps are
C++ and belong to wave 2, not to you.

## Measured (findings-E §1, §5)

- The corpus MCP is fast (7,639 calls, 0.2 h total, one call over 10 s: `vtmb_code
  0x103692c0` at 38 s; regex `vtmb_grep` 2–5 s) but its replies are large: `vtmb_slot` median
  27 KB, `vtmb_asm` up to 49 KB, `vtmb_closure` 128 KB, `vtmb_code` 60 KB (findings-C §4).
- The text tree is where queries cost: 1,851 agent-written Python scripts over `docs` / TSV took
  3.9 h, 53 over 30 s; 4,566 shell searches over `docs` 1.8 h. The scripts and their shapes are
  in the transcripts; findings-E's scratch (`E:\elysium-work\scratch\findings-e\`) has the
  extraction scripts. `AGENTS.md` points at an `npc-ai` index file that does not exist, so agents
  search where they should look up; `functions.md` is 1.4 MB, the checklists 0.7–0.9 MB.

## Job

1. **The 60 s stop, server-side.** `research/tooling/ghidra/driver/corpus_mcp.py`: every tool call
   runs under a deadline (an SQLite `set_progress_handler` that aborts past 60 s; a plain timer
   for non-SQL work) and returns `TIMEOUT after 60 s: <tool> <args> — narrow it with <param>`.
   Every call is journaled to `$ELYSIUM_WORK_ROOT/logs/corpus-mcp.tsv` (time, tool, arguments,
   seconds, reply chars) and every call over 10 s also to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`
   (the project's slow-query log; columns: time, source, query, seconds).
2. **Find why `vtmb_code 0x103692c0` took 38 s** and fix it at the query; regex `vtmb_grep`
   answered from the FTS index where the pattern allows.
3. **Default caps**, each truncation naming the parameter that gets more: `vtmb_asm
   max_lines=400` with `from=`/`to=`; `vtmb_closure sections=` and `brief`; `vtmb_code` 20 KB with
   `from_line=`/`lines=`; `vtmb_vtable slots=` and `overridden_only`; `vtmb_fields range=`/`name=`;
   `vtmb_slot limit=50`. No default reply over 20 KB.
4. **The address index.** A generated lookup from a retail address or name (`0x1028a380`,
   `102f06e0`, `CAI_BaseNPC::SelectSchedule`, `m_scriptState`, a slot number) to: its `docs/vtmb`
   locations (file, heading, line), its ledger rows (which `npc-kernel` tables), and the port's
   `file:line`s that cite it. Built on an input hash into `$ELYSIUM_WORK_ROOT/cache/`, answered in
   under 1 s by `uv run elysium research where <query>` (a new script under `research/tooling/`;
   the `research` command runs `research/tooling/**/<name>.py`) and by a new corpus MCP tool
   `vtmb_where`. A cold rebuild under 10 s.
5. **Standing query verbs.** From the transcripts, classify the 1,851 one-off docs/TSV scripts by
   the question they answered; implement the top recurring questions (aim for the 3–6 that cover
   most of the time) as `research` verbs over the index, each under 1 s.
6. **The pointer.** Replace `AGENTS.md`'s line naming the missing `npc-ai` index file with how to
   use `research where` / `vtmb_where`.

## Acceptance

A probe table: every `vtmb_*` tool on `CAI_BaseNPC`, `StartTask` and `0x1028a380`, plus `where` on
five addresses, with seconds and reply chars, all under 10 s and 20 KB by default. Tests for the
deadline (a forced slow query times out cleanly), the caps and the index.

## Files you own

`research/tooling/ghidra/driver/corpus_mcp.py`, `corpus.py`, the new index / verb scripts under
`research/tooling/`, their new tests under `pipeline/tests/` (new files only), and the one line in
`AGENTS.md`. Not the `kernel_*` modules (T1), not `cli.py` / `pyproject.toml` / `conftest.py` (T4).
