# Lane T report: ledger tooling for closed rows

(Saved by the orchestrator from the lane's final answer.)

Files changed: `research/tooling/ghidra/driver/kernel_ledger.py`, `kernel_lists.py`, `kernel_shape.py`,
`research/tooling/gen_kernel_shape.py` (lives one level above `ghidra/driver/`),
`pipeline/tests/test_gen_kernel_shape.py`, `pipeline/tests/test_kernel_closed.py` (new). `SLOT_PORT_MAP` untouched.

**Grammar** (`kernel_ledger.target_problem`, `closed_target`)
- A `dead` row takes `-` (closed) or a port target.
- A `mechanism` row takes a service word from the README set, spelled exactly (closed), or a port target. It may also take one of 11 older spellings (`UStruct`, `UClass`, `FProperty`, `RTTI:GetDataDescMap`, …), which count as open.
- A `rule` or `present` row may not take `-` or a service word.
- An empty target now fails on every verdict except `unsettled`.
- Every rejection names the row's `0x<addr>`.

**Emission** (`gen_kernel_shape.close_layers`; the same table is in the module header comment)

| Verdict and target | Layer | Reached* | Emits |
|---|---|---|---|
| `dead` at `-`, or `mechanism` at a service word | introducer | yes | the generated `default:` body: retail's literal if the whole body is one (`Default`, with a probe), else `return {}` (`Closed`) |
| same | introducer | no | nothing (slot kind `CLOSED`, empty port callable) |
| same | override | either | nothing; the class inherits |
| open row, or `rule`/`present` | any | either | unchanged (stub, hand or default) |

\*Reached means at least one of: the slot has a dispatch site in the ledger's `slot_dispatch_sites` (the *Sites* column story 1 used), a layer that stays, an unclosed species body at the slot, or an `OVERRIDDEN_BELOW` row.

- A `CHAIN_HAND` body over a closed row stops generation with an error.
- A closed species row gets no override and never enters `unported.tsv`. A `dead` row at `-` never entered it.

**Meter**
- `kernel_lists --check` now fails when a closed row still has a port site or a test citation, and names the address and the cites. The only exception is a comment in `Tests/` that records the removal.
- `kernel_lists --closed` on HEAD prints: dead 282 / 657, mechanism 81 / 380.

**HEAD anomalies**
- **59 closed rows are still cited at HEAD, not zero.** Most cites are comments. The code cites are: the dev print `EmitDevMsg` for 0x1027efb0 in `ElysiumNpcBaseDebug10.cpp:114`; the `DumpDebugLogRing` call-site comment in `ElysiumNpcThinkSpecies.cpp:138`; address comments on `RunAI` call sites for 0x10385a10 and 0x10360160; rows in the activity tables, the hull table and a test. To keep `--check` usable, the 59 are exempted in a pinned list, `CITED_WHEN_CLOSED`, which should only ever shrink. In the working tree 56 are still cited.
- **The gate was already red at HEAD**: slot 172 `GetNextTarget` collides with `FCornerChainTestCorner` in the 0018/5 navigator tests, and the committed ledger tables are stale against the current corpus. With the lane's changes: the ledger tables and the two lists render byte-identical to before; `gen_kernel_shape` output changes only for 12 layers whose rows were already closed at HEAD (seven stubs become silent bodies: five on `FElysiumEntity`, two on `FElysiumNpcBase` 0x101a65a0 / 0x101a65e0; two `FElysiumNpcBase` overrides dropped, slots 5 and 82; three rows leave `unported.tsv`).

**Pytest:** 21 new tests in `test_kernel_closed.py`, all passing. Across the owned test files 133 pass; the 2 failures and 5 errors all come from the two HEAD anomalies.

## Needs another owner
- **`ElysiumNpcKernelShape.h`:** add `Closed` after `Hand` in `EElysiumNpcSlotBody`, with the comment "closed by 0019/6 but still reached: answers the value-initialised default, tallies nothing, no probe". Without it, the next regeneration at HEAD does not compile.
- **`SLOT_PORT_MAP`:** slot 534 is `PORT` (`FElysiumCombatCharacter::EyeLookTargetHandle`) over 0x1026b270, closed at `-`: needs a `DELETED` row or a re-verdict; the only slot that still does. Slot 582's `DELETED` row is now redundant.
- **`kernel_verdicts.tsv`:** re-spell the 41 rows that use the 11 older mechanism targets.
- **`docs/vtmb/npc-kernel/unported.tsv`:** re-pin; it loses the rows for slots 5, 82 and 79.
