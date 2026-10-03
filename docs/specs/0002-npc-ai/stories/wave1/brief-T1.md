# Brief T1 — the `kernel_*` tools under budget

Read `README.md` here first (the wave's rules), then spec § T1.

## Target

Every `kernel_*` command answers in under 10 s cold and under 1 s when nothing changed, with
byte-identical output.

## Measured (findings-E §3–4, findings-C §2; profiled at `9a1bec0d`)

- `kernel_shape.build` is rebuilt from scratch by `kernel_shape`, `gen_kernel_shape`,
  `gen_kernel_bindings`, `kernel_story8_shape`, `kernel` and `kernel_gate`'s residue: 852 runs,
  5.4 h, ~17–18 s each. Inside it: `sdk_layout.preprocess` 52% — `_strip` is a char-by-char loop
  (36.8M `startswith` calls, `sdk_layout.py` ~97); `sdk_members` 36%, uncached, 272 calls
  (`kernel_shape.py` ~243; a `functools.cache` measured 4.10 s → 0.57 s with identical output).
- `kernel_gate --family` 146 s median: 92% is a full `kernel_shape --unported` build for the residue.
- `kernel_ledger --check` 8.2 s: SQL load 46% (16,010 queries; `_load_family` 2.0 s,
  `_load_functions` 1.3, `_load_bases` 0.9), `_scan_citations` over 1,639 files 29%, render 17%.
- The corpus stage (`load` + `walk` + `directions`) pickles to 50 MB and reloads in 0.22 s
  (findings-C; first convert `fields` / `species_fields` from `sqlite3.Row` to `dict`).
- `kernel_lists --check` 6.3 s, of which its own work is ~0 (a full ledger rebuild).
- `sdk_layout._condition`'s `eval` prints a `SyntaxWarning` every run.

## Job

1. The regex `_strip` and the cached `sdk_members` (identical output).
2. One shared build: the built shape, the corpus stage and the SDK index pickled under
   `$ELYSIUM_WORK_ROOT/cache/kernel/` (read `.elysium.local.env`), keyed on a hash of their inputs
   (the SQLite files, the datamap JSON, the overlays / TSVs, the 2013 SDK headers, and the code of
   the modules that build them). Every consumer above reads it; a stale key rebuilds once.
3. `kernel_ledger`: a per-file citation cache keyed on `(path, mtime, size)`; the load from the
   cached stage.
4. `--check` short-circuits on an unchanged stamp hash (all inputs + all outputs), answering in
   under 1 s.
5. `kernel_gate`'s residue from the cached build, not a fresh `kernel_shape --unported`.
6. `--reach` joins the combined gate (`uv run elysium research kernel --check`).
7. The `SyntaxWarning` gone.
8. Time `kernel_skeleton` and `kernel_packet` (one realistic invocation each, under the 60 s
   rule); bring any per-query step over 10 s under budget. `kernel_packet` launches LLM workers —
   that part is an agent run, not a query; only its own lookups count.
9. Tests: in the kernel test files you own, mark every test that reads `$ELYSIUM_WORK_ROOT` or the
   corpus with `@pytest.mark.corpus` (the marker is registered by T4's lane in `pyproject.toml`;
   the list is `E:\elysium-work\scratch\findings-f\per_test_categories.tsv`), and replace the three
   per-module ledger builds with one process-wide cached builder in a helper module (e.g.
   `pipeline/tests/_kernel_build.py`) — no `conftest.py`, which T4's lane owns.

## Acceptance (you measure, the integrator re-measures)

- A before/after table for every command above: cold, warm, unchanged.
- Every `--check` output byte-identical to before (keep a copy of the generated outputs from
  before your change and diff); `uv run elysium research kernel --check` green (7/7 plus reach).

## Files you own

`research/tooling/ghidra/driver/kernel_*.py`, `sdk_layout.py`, `research/tooling/gen_kernel_*.py`,
the `kernel` combined-gate module wherever it lives, and in `pipeline/tests/`: `test_kernel*.py`,
`test_gen_kernel_*.py`, `test_cli_oracle_argv.py`, `test_oracle_source.py`, any `test_sdk_layout*`,
plus the new `_kernel_build.py`. Not `corpus.py` / `corpus_mcp.py` (T2), not `cli.py` /
`unreal.py` / `workspace_lock.py` / `pyproject.toml` / `conftest.py` / `test_oracle_citations.py`
(T4).
