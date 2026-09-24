# Packet 0a — pinned factory evidence

Start: `0019-5-class-tree` at `cb115e31`; pre-existing index tree and patch in [progress](../progress.md).
Executor: Codex desktop / GPT-6; active variant/effort is not exposed in this task's metadata.
Plan recommendation: Astra / High. One integration writer.
Outcome: a reviewed classname/allocation/constructor/final primary-vtable map, separate from
liveness, without activating the step-2 census change.

Owns: `manifest.json`, `classes.tsv`, `factories.tsv`, `schema.md`;
`research/tooling/ghidra/driver/kernel_factory_map.py`, `kernel_migration.py`;
`pipeline/tests/test_kernel_migration.py`; factory recovery in `population.md`.
Existing staged content must be preserved.
Inputs: kernel `classes.md`, `functions.md`, `fields.md`, `slots.md`, `entries.md`;
`population.md` factory/liveness recovery; pinned image, corpus and listings;
`E:/elysium-work/scratch/0019-5/factory_map.py` as a hypothesis generator only.
Preserve: all runtime registrations, census output, liveness verdicts and C++ sources.
Permitted changes: preflight tools, authored decisions and evidence only.
Deferred: survey activation (step 2), executable deletion (step 1), all runtime folds (steps 7–10).
Checks: module/input pins; instruction boundaries, receiver flow and thunks; all 68 original
observations plus three directors and three makers; nine census differences; negative tests for
ambiguous/unresolved rows. Baseline cheap checks recorded separately.
Done: every factory observation is either evidenced or explicitly unresolved and rejected;
aliases cannot hide multiple factory answers; no proximity rule is treated as evidence.

Handoff: validated; included in the owner-requested step-0 commit. All 74 mappings replay against pinned module/corpus/listings;
55 constructor-final-write and 19 inline-final-write paths. The 68 original observations retain
the nine census differences; directors/base maker add four census omissions. Current registry
dispositions distinguish 15 shared-NPC names from the 30 additional ordinary names due in step 2.
All 77 classes remain inventoried, with the ten deferred classes named explicitly.

Thirteen tests pass, covering aliases, inline construction, ambiguous answers, receiver changes,
conditional writes, thunk cycles and the real pinned input. Sibling-field and branch-slot key tests
are synthetic schema checks; actual field/body dispositions are still owed by subsequent packets.
`gate-0p/pytest.log` contains the combined 84-test result. Factory evidence is
`factory-reviewed.json` plus its `.listings.txt`, with hashes in `checkpoint/evidence-hashes.json`.

The population oracle corrects two prior readings: `npc_TestBaseHumanoid` finishes with
`CAI_BaseHumanoid`'s table, and `aiscripted_schedule` allocates `0x6098`. Constructor/member calls
are distinguished by receiver offset. Every failed-allocation arm is retained and checked.
No liveness change is inferred; the next-step shared-target/direct-caller deletion audit is open.
Early proposal-only runs rejected every row until the corpus's string annotations were stripped
from instruction operands; no rejected proposal was promoted to a reviewed mapping.
