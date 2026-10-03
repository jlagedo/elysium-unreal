# Brief T4 + T3's `pytest` half — the runner, the lease, the waits, the Python test budget

Read `README.md` here first (the wave's rules), then spec § T3 and § T4.

## Measured

- **Lease** (findings-C §1, findings-E §1): `build` / `test` take `WorkspaceLease(export_root)`
  without blocking (`cli.py` ~749-757, `workspace_lock.py` ~153-189); one lease across checkouts
  refused 1,659 builds and 836 test runs in 12 days; retry wrappers cost 9.2 h; `run play` holds
  it for a whole session.
- **Polling** 18.9 h: agents loop `until grep …; sleep` on build/test logs because builds (p90
  280 s) outrun the shell tool's default timeout and the commands end in long logs.
- **Several prefixes in one boot:** UE accepts a `+`-joined filter, but the report slug is built
  from the whole filter (`unreal.py` ~944): a 310-char path, `index.json` never written, "matched
  no test".
- **Shell floor:** `bash -lc true` 0.35 s against `bash -c true` 0.03 s (the login profile), over
  37,408 shell calls.
- **`pytest`** (findings-F §4): 4,600 tests, 384 s serial; 150 corpus-reading tests in 35 files
  are 326 s; 12 test files write `ELYSIUM_WORK_ROOT` at import (`os.environ.setdefault(...)`), and
  `test_gen_contents_signatures::...as_the_probe_does` then fails in one process; two tests fail on
  `main`: `test_oracle_citations` (three briefs cite `npc-kernel/population.md` /
  `npc-ai/index.md` under `docs/vtmb`, which do not exist: `consolidation/findings-C-tooling.md`,
  `0018-world-ai-infrastructure/story8/brief-R1-corpus.md`, `0019-npc-kernel-rework/story-6/brief-S.md`)
  and `tools/elysium_glb_review` `test_core_seams::test_every_declared_family_admits_only_root_extensions`
  (`ai-schedules` declares `ELYSIUM_vtmb_ai_schedule`, absent from `ROOT_EXTENSIONS`).

## Job — T4

1. The lease keyed per checkout (`repo_root`) for `build` / `test` / `run play` / `run editor` /
   `gr`; the export-root lease only for commands that write exports or bakes; a build exclusive
   against anything holding its checkout's binaries; waiting by default (bounded at 30 min, one
   line naming the holder when it starts waiting) with `--no-wait`; the Unreal-process liveness
   check that clears stale leases kept.
2. `build` and `test` end with a one-screen summary (result, duration, failures by name, the report
   path) and exit codes that mean something, so a caller runs them in the foreground (or in the
   background and waits for the notification) and never polls a log.
3. `uv run elysium test A B C` runs every prefix in one boot; the slug capped at ~40 chars + an
   8-hex hash of the full filter; the summary per prefix.
4. The research watchdog in the `research` command (`cli.py` ~4016): a research tool past 10 s
   prints one warning line and appends to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv` (columns:
   time, source, query, seconds — the same file T2's MCP writes); past 60 s it is stopped with a
   message to optimize it. Tools that are agent runs, not queries (`kernel_packet`), are exempt by
   an explicit marker in the tool (a module constant the command reads) — never by a flag a caller
   adds to dodge the stop.
5. The shell floor: measure the login profile (`~/.bash_profile`, `~/.bashrc`, `~/.profile`,
   anything they source) step by step and name the slow step(s) in your report. Do not edit them —
   machine configuration is the owner's.

## Job — T3's `pytest` half

6. A `corpus` marker registered in `pyproject.toml` and deselected by default (`addopts`), applied
   to the corpus-reading tests outside the kernel files (the kernel files are T1's; the list is
   `E:\elysium-work\scratch\findings-f\per_test_categories.tsv`).
7. The 12 import-time `ELYSIUM_WORK_ROOT` writes replaced by a fixture or `monkeypatch`, so no test
   module mutates the process environment at import.
8. `pytest-xdist` added to the dev dependency group; make `-n auto` the default only if it measures
   faster for the default selection (record both).
9. The two failing tests fixed at their cause: the three briefs cite files that exist (find what
   each meant; never create a stub file to satisfy the test); `ROOT_EXTENSIONS` / the declaration
   brought into agreement the way the glb-review contract says.

## Acceptance

Tests for the lease keying, the wait, `--no-wait`, the slug cap, the multi-prefix filter and the
watchdog. You may run the test files you changed; the integrator runs the default `pytest`
(budget ≤20 s wall, zero failures) and one three-prefix `elysium test` against the existing
binaries.

## Files you own

`pipeline/src/elysium_pipeline/cli.py`, `unreal.py`, `workspace_lock.py`, `process.py` if needed,
`pyproject.toml`, a new `pipeline/tests/conftest.py`, every `pipeline/tests/` file not owned by T1
(`test_kernel*`, `test_gen_kernel_*`, `test_cli_oracle_argv.py`, `test_oracle_source.py`,
`test_sdk_layout*`, `_kernel_build.py`) or T2 (its new files), `tools/elysium_glb_review/`, and the
three briefs named above. Not `research/tooling/` (T1, T2).
