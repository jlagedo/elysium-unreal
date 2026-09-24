# Packet 0d — exact regression bookkeeping

Start: step 0 at `cb115e31` plus preserved index and packets 0a/0p.
Executor: Codex desktop / GPT-6, integration owner; variant/effort not exposed. Plan: Astra / High.
Outcome: `uv run elysium research test_delta` compares all three suite reports; differences need
exact, consumed, evidenced expectations. Failed/incomplete runs cannot be allowed through.
Owns: `research/tooling/test_delta.py`, `pipeline/tests/test_test_delta.py`,
`expectations/step-0.json`. Descriptors and generated results belong under the work root.
Inputs: plan §3.4 and the three existing Unreal `index.json` reports.
Preserve: test identity, warning/error and stub counts, targeted deterministic diagnostic order;
retain module/address and receiver identity when the legacy message reports them.
Permitted changes: additions, removals with a coverage disposition, exact renames, diagnostic
remaps and exact diagnostic changes. Remaps cannot change a slot stub's retail/receiver identity.
Deferred: field/body manifest joins will supply declaring slot families that old logs omit;
rehearsal fixtures must select the deterministic tests for order comparisons.
Checks: pure report/expectation tests; self-comparison of all three saved reports; comparison to
the new prerequisite runtime gate, with historical source/content provenance limitations explicit.
Done: no unmatched differences or unused expectations; provenance travels with every result.

Handoff: validated; included in the owner-requested step-0 commit. Twelve unit tests pass; self-comparison and the new runtime gate
both compare all 1,283 tests without differences or expectations. The archived historical reports
and descriptor are in `baseline/`; current reports and descriptor in `gate-0p/`. The final delta's
SHA-256 is `7967f56acc095968084e162a187ddb38bc5740ff1bfcc07e859b0cff44c934f3`.
The command's PASS means regression comparison only, never step acceptance or retail equivalence.
Historical reports lack asset/RNG pins, explicitly retained as a limitation in their descriptor.
Current hashed inputs stayed unchanged; generated test/build outputs are accounted separately.
No deterministic-order fixture list is claimed yet: fixture classification/rehearsals must add it.
Legacy messages lacking slot-family/signature data keep that identity component explicitly null.
