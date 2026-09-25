# Story 5 — step 1 accepted

**Step 1 passed its acceptance gate on 2026-09-25. Step 2 has not started.**
`uv run elysium research kernel_migration --check step1` exits 0 (it re-verifies the step-0
receipt against the accepted tree `aa1c3c86`). Evidence is pinned in
[acceptance-step1.json](acceptance-step1.json); step 0's stays in [acceptance.json](acceptance.json).

Checkout: `E:/dev/elysium-unreal`, branch `0019-5-class-tree`, base `aa1c3c86` (step 0 accepted).
Manifest phase 1, revision `step-1-dead-species-deletion`; the verdicts pin is refreshed and the
step-0 pin kept under `history.step0`.

Completed:

- [Step 0](packets/step-0-inventory-and-rehearsals.md): manifest, factory identities, inventory,
  rehearsals.
- [Step 1, packet 1a](packets/1a-deletion-manifest.md): deletion record, step-1 decisions,
  historical `--check step0`, `--check step1` with a live-definition guard.
- [Step 1, packets 1b–1g](packets/1b-1g-deletion.md): the dead species subset deleted, overlay and
  generated outputs regenerated, full gate. Record: [deletions-step1.tsv](deletions-step1.tsv).

Validation: build green; **1,255 Substrate + 14 Content + 1 PlayerWorld**, zero failures;
`test_delta` against step 0's final gate passes with the 29 reviewed expectations in
[expectations/step-1.json](expectations/step-1.json); five generator checks and 104 Python tests
pass. An independent review of `ecfa9d82` found no blocker; its follow-up (stricter checker, record
and comment corrections) was re-gated green.
Reports: `E:/elysium-work/research/npc-kernel/story-5/step1/gate/`.

Carried forward (see [decisions-step1.json](decisions-step1.json)):

- Generated activity-table and hull-table rows of dead classes are retained census until step 11.
- Seven live dispatchers still lead-cite a removed dead address, so the inventory lists them as
  step-1-scope candidates; they are live and not deletion candidates.
- `CharTemplateModelName` is a retained seam: the port's `Spawn` does not yet make retail's
  `FUN_10207e60` call from `CAI_BaseNPCTroika::Spawn` `0x10298d30` (a pre-existing unported rule).
- Slot 525: `OverrideMove` dispatches neither live arm — ManBat `0x1038b120` (unported flight
  step) nor the VampireBoss family's `0x103c5fe0` (`m_bJumping != 0`). The latter was mis-judged
  `present` and is now a `rule` (8 contracts, the reviewed `rule_identity_delta`), so steps 3–4 and
  story 8 carry both; the flag's writer is Troika `StartTask` `0x102a1910`'s jump arms (story 8).
- For step 2: `ClassHolstersOnState` lists `npc_ProneDialog`, but retail's classnames are
  `npc_VProneDialog` and `npc_VMercurio`.

**Next:** step 2 (species shells and factory correction) when separately requested. Read the
reviewed plan's step 2, `factories.tsv`, and the compatibility inventory in
`step1/inventory-step1.json` first. Do not tick story 5 or tracker 06b until step 11.
