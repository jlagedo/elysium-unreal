# Step 3, packet 3a — preflight, override matrix and checker (3-pre)

Start: `0019-5-class-tree`, `870360e9` (step 2 committed and accepted), clean tree, manifest
`step-2-species-shells-and-factories` (phase 2).
Executor: Claude Code, Opus 5.5.
Outcome: step 3's scope is a re-derivable record, and the checker that will accept the step exists.
No C++ change.

## Records

- `manifest.json`: `history.step2.commit = 870360e9…`, `step3_packets`; `ACCEPTED_PHASES` gains 3
  (the phase stays 2 until the step is accepted).
- [overrides-step3.tsv](../overrides-step3.tsv), the **override matrix**. It has one row per introduced
  `(introducing class, slot, body)` of the census `GOverrides` table.
  - A census row that repeats its base's body at the same slot (the vtable copy) belongs to the
    topmost class holding that body, which is where the C++ override stands.
  - A row enters when the port runs the body (verdict rule, present or mechanism, with an overlay
    target the port defines), or when hand-written source dispatches on its address. The second
    criterion keeps the dead and `registry:` rows that address arms still reach, so a structural
    move cannot silently drop them.
  - **611 rows**, plus draft dispositions:
    - 428 `override` and 8 `own`. An `own` row is a branch virtual, past a base's table, introduced
      by the class itself: 619 `SetSchedule`, 620/621.
    - 175 `investigate`: bodies the port reaches without an address arm (class tables,
      `IsRetailClass`, species-named helpers, or the Troika body standing in). Each resolves in its
      owning packet.
  - Identity columns are re-derived from the accepted step-2 tree by `--check step3`.
- [decisions-step3.json](../decisions-step3.json): the conversion rules, the planned direct calls
  (Zombie 510, Bach 606, Tzimisce 593, Cop 597), and the empty `step5_renames`,
  `retail_corrections`, `residue` and `surviving_sites` that later packets fill.

## Findings that changed the plan

- The census `registry:` constant rows (`GOverrides.Default`) have no production reader. They are not
  dispatch, so step 4 moves them as bodies. Only those that a source arm still reaches are in the
  matrix.
- The spec's vocalisation job names the one-byte silent bodies (Camera, CameraSecurity, Newscaster)
  as overrides, so 3g converts them rather than leaving them residue.
- The step-0 inventory scanner (`kernel_migration_inventory.py`) is not extended. The step-3
  checker's own scanner (`gate_sites`) is the site inventory and the static gate: dispatch tokens
  counted per enclosing definition.

## Checker

`kernel_migration_step3.py` (`--check step3`):
- **Always:** matrix identity against the step-2 tree.
- **At phase 3:**
  - no `investigate` rows;
  - every `override`/`own` is declared on its class and defined in its cpp;
  - the static gate equals `surviving_sites`;
  - recorded direct calls are spelled in their callers;
  - overlay targets name no removed step-2 symbol;
  - `--check step2` (historical);
  - pinned artifacts, and every expectation consumed.

`kernel_migration_step2.check_step2` reads `history.step2.commit` once it is recorded.

## Checks

- Five generator `--check`s pass.
- `pytest` passes: `test_kernel_migration*.py`, `test_kernel_shape.py`, `test_kernel_ledger.py` and
  `test_gen_kernel_shape.py`, 108 tests.
- `kernel_migration --check step2` passes on `870360e9`. `--check step3` is PENDING: the matrix
  matches, with 611 rows.

Disposition: complete; ready to commit as the 3-pre prerequisite.
