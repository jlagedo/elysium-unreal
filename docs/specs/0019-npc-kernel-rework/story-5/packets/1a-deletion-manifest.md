# Step 1, packet 1a — the deletion manifest and step-1 checker

Packet: step 1 / reconcile the no-instance deletion review
Start: `0019-5-class-tree`, `aa1c3c86` (step 0 accepted), clean tree, manifest `step-0-reviewed-preflight-v2`.
Executor: Claude Code, Opus 5.5, medium effort; the C++ packets 1b–1e run serially under one writer.
Outcome: an authored deletion record (`deletions-step1.tsv`), step-1 identity decisions
(`decisions-step1.json`) and a `--check step1` gate; the step-0 receipt stays checkable.
Owns: `kernel_migration.py`, `kernel_migration_step1.py`, `test_kernel_migration_step1.py`,
`decisions-step1.json`, `deletions-step1.tsv`, `expectations/step-1.json`.
Inputs: `inventory-final.json` `deletion_candidates` (40 dead-only / 26 shared-live / 42 outside
step 1), the 246 `dead=class … no instance` verdict rows, the port-side caller sweep.
Preserve: every live body's definition, every dead census row, the live rule identity
`000873c0…838f`.
Deferred: factory registration and survey activation (step 2); dispatch replacement (step 3).

## Findings that change step 0's review

- Two identities were joined to the wrong definition. `0x1025fa50` is
  `BaseHumanoidMaintainEyeDirection`, not its cycler-arm helper. `0x10260dc0` is `Slot584Species`,
  not the live `Slot584` dispatcher.
- `SpeciesWorldSpaceCenter` is shared: the live werewolf `ApplyFakeHullPush` calls it. Only its
  Crow branch is dead.
- Step 0 computed callers from retail edges only. The port-side sweep adds the dead arms inside
  live dispatchers (Spawn, NPCInit, SelectIdealState, OverrideMove, sounds, member-pointer tables)
  and the bodies reachable only through them.
- `npc_bullseye` has a retail factory (`0x10356630`); the port comments saying otherwise were stale.

## Checker

`--check step0` reads the accepted tree (`manifest.json` `history.step0.commit`) once a later phase
is current. `--check step1` holds the current tree to `deletions-step1.tsv` against that accepted
tree, refuses the loss of any definition implementing a non-dead or live-receiver step-0 body
whatever the record says, requires every dead class's census row, re-derives the rule identity from
a regenerated step-1 inventory, and verifies the step-1 delta, expectations and runtime receipts.

Disposition: accepted with step 1 (see `progress.md`).
