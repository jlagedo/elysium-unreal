# Packet 0p — existing DeathSound generation collision

Start: step-0 baseline, `cb115e31` plus preserved documentation index; no C++ changes.
Executor: Codex desktop / GPT-6, integration owner; variant/effort not exposed. Plan: Astra / High.
Outcome: recognize the existing generated declaration's deliberate schedule-runner override;
reconcile the separately discovered stale hull census label with the already reviewed overlay.
Owns: `research/tooling/gen_kernel_shape.py`, `pipeline/tests/test_gen_kernel_shape.py`, and the
generator-owned `ElysiumNpcKernelShape.cpp` (only through regeneration).
Inputs: `IElysiumScheduleRunner::DeathSound`, `FElysiumNpc`'s runner inheritance, generated slot
488 declaration, handwritten `FElysiumNpc::DeathSound`, ledger `0x10293ec0`, and
`lifecycle.md`'s death route (`0x10265cb8`) / `TASK_SOUND_DIE`.
Hull-label evidence: `kernel_fields.tsv`'s `CAI_BaseNPCTroika +0x156c` overlay and
`navigation-jump-links.md`'s two-hull recovery: `CAI_Navigator::SetGoal` reads it at
`0x102ecd2c`, `Move` refreshes it at `0x102effe1`; it is the pathing hull, not a default hull.
Preserve: slot signature, generated declaration, hand body, species dispatch and death event order.
Permitted changes: diagnostic/tooling classification only. No runtime behavior correction.
After removing the collision, `--check` exposed an independent stale output inherited from
`c0d8bced`: `CBaseCombatCharacter +0x156c` still says `m_eDefaultHull` in the census whereas the
reviewed overlay/ledger say `m_ePathingHull`. Regenerate that label and its row digest. This
prerequisite is named before regeneration; do not change the recovered field or its runtime store.
Deferred: migration to final owners belongs to later steps.
Checks: generator checks, existing generator Python tests and a targeted interface-override test;
build and all three runtime suites for the generated census change.
Done: both formerly failing generator checks pass; slot declarations and bodies are unchanged;
the only census differences are the documented label and digest. Runtime gate must pass.

Handoff: validated; included in the owner-requested step-0 commit. Prerequisite named before editing. Baseline results are in
`E:/elysium-work/research/npc-kernel/story-5/baseline/checks.json`:
three ledger checks pass; two generators exit 7; Python 53 passed / 5 setup errors, all for the
same slot-488 collision. A temporary indentation error caused one focused test collection to fail;
it was corrected before the full checks. The first resolved generator run then exposed the stale
hull census and the old test's missing explicit-override case (18 passed / 2 failed), both fixed.
These failed intermediate results are retained in `generator-prerequisite/` and the CLI journals.

Final results: `gate-0p/checks.json` (five checks pass; 84 Python tests, two existing SyntaxWarnings),
`runtime-results.json` (build and all suites exit 0), `run.json` (inputs, reports and hashes),
`output-audit.json` (ten expected generated-output rewrites) and `delta-from-historical.json`
(1,283 tests, zero unmatched differences, zero expectations).

Build observed 37 compile actions / 40 total actions in 152.38 seconds. Suite wall times were
180.25 / 43.90 / 19.38 seconds; report completions were 1,268 / 14 / 1. Substrate still records
791 successes with warnings. Generated slot declarations and bodies are unchanged; the census
diff has only `m_eDefaultHull` → `m_ePathingHull` at `+0x156c` and the row digest. No named gameplay
modernization was introduced. This build does not stand in for step 0's representative migration.
