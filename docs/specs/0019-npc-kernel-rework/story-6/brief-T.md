# Lane T · the ledger tooling for closed rows

Read `README.md` in this directory first. You own, and only you may edit:

- `research/tooling/ghidra/driver/kernel_ledger.py`
- `research/tooling/ghidra/driver/kernel_lists.py`
- `research/tooling/ghidra/driver/gen_kernel_shape.py` (everything except the rows of `SLOT_PORT_MAP`)
- `research/tooling/ghidra/driver/kernel_shape.py`
- `research/tooling/ghidra/driver/kernel_gate.py` if it needs the new check
- `pipeline/tests/test_kernel*.py`, `pipeline/tests/test_gen_kernel_*.py`

You may run `uv run pytest` on those tests and the `--check` / `--report` modes of the tools
above (they read; `--check` must not write). Do not run a bare render that rewrites
`docs/vtmb/npc-kernel/*.md` or the generated C++.

## Why

Story 6 closes 657 `dead` rows (target → `-`) and 380 `mechanism` rows (body → seam). Survey
findings the tooling must absorb:

1. `gen_kernel_shape.apply_verdict` (`gen_kernel_shape.py:778-810`) emits a firing
   `ElysiumStub` (`:1554-1580`) for any target it does not recognise, including `-` and a bare
   service name. So a dead slot whose body is removed must not silently become a stub unless
   the slot is still dispatched. Today only a `SLOT_PORT_MAP` `DELETED` row (slot 582, `:447`)
   removes a slot.
2. `kernel_lists --check` (`kernel_lists.py:317-363`) does not fail a closed row that still has
   a port site or a test (`:245-249` finds cites by grepping `Source/` for the retail address).
3. `kernel_ledger.py:375` allows an empty target only on a `dead` row.

## Job

1. **Target grammar.** Accept, on a `mechanism` row, a bare service word from this closed set
   as a valid target: `CMC`, `UNavigationSystem`, `UPathFollowingComponent`, `TraceRetail`,
   `Chaos`, `Replication`, `CRT:operator delete` (already accepted), `FElysiumSaveArchive`
   (already), `Bake`, and the spec/story spellings `0010`, `0015`, `0018/3`, `0018/16`,
   `0002/28`. Reject anything else with a message that names the row. Keep `hand:` as is.
2. **Generation.** For a slot row whose target is `-` (dead) or a service word (mechanism):
   emit **nothing** for that slot when the slot has no dispatch site (the ledger knows
   dispatch: use the same fact `population.md` / story 1 used — "slots with no dispatch site";
   find where `kernel_ledger` records callers per slot and reuse it), and the generated
   `default:` body when the slot is dispatched and the row is the family's only body. A firing
   stub is emitted only for `rule` / `present` rows, as today. Species override rows with `-`
   emit no override (the class inherits). Document the rule in the generator's header comment.
   If a per-slot `DELETED` row in `SLOT_PORT_MAP` is still the only sound way for some slots,
   say which and why; the orchestrator writes those rows.
3. **The meter.** `kernel_lists --check` fails when a row whose target is `-` or a service word
   still has a port site or a test outside `Tests/`-only deprecation notes; the failure names
   the address and the cites. Add `kernel_lists --closed` printing four numbers: dead rows
   closed / total, mechanism rows closed / total. `kernel_lists --check` stays green on HEAD
   (nothing is closed yet; 282 dead rows are already `-`, verify they carry no cites — if any
   do, list them in your report, do not change the C++).
4. **`unported.tsv`.** A mechanism row at a service word is not "unported" (`kernel_shape.py:1050-1131`).
   Make sure it leaves the residue list, and that a dead row at `-` never enters it.
5. **Tests.** A pytest for each of 1–4 using a small synthetic overlay (there are fixtures in the
   existing test files — reuse their pattern). `uv run pytest pipeline/tests/test_kernel*.py
   pipeline/tests/test_gen_kernel_*.py` green. `uv run elysium research kernel --check` green
   on HEAD (it must be: no data changed).

## Deliver

`report-T.md` (≤300 words): the accepted grammar, the emission rule per (verdict, target,
dispatched?) as a small table, which slots still need `DELETED` rows, the pytest count, and
any HEAD anomaly found (a `-` row with cites). No `targets-T.tsv`.
