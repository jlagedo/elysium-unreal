# Step 5 — separate `CAI_BaseNPC` (`FElysiumNpcBase`) from `CAI_BaseNPCTroika` (`FElysiumNpc`)

Base: `03c0d030` (step 4 closed out).
- 5a: records and checker, committed `571561aa`.
- 5b: pure rename, committed `4e91d0d8`.
- 5c–5d: committed as the `wip` checkpoint `826f4bb1`.
- 5e–5h: committed `e0a71ee3`, the step-5 commit.

Manifest phase 5, revision `step-5-base-troika-split`.

## Packets

- [5a-preflight.md](5a-preflight.md): the move, binding and consumer records and the checker.
- [5b-troika-family-rename.md](5b-troika-family-rename.md): the Troika family files renamed
  `ElysiumNpc<Family>`.
- 5c: the class shell, `AsNpcBase()`, the identity API and the `CAI_BaseNPC` descriptor.
- 5d: the base words, the split components and the `NpcBase` binding class
  (`progress.md`).
- [5e-5f-bodies-and-slot-split.md](5e-5f-bodies-and-slot-split.md): the base bodies, renames,
  pairs, consumers, retail corrections, and the per-layer generated slot surface.
- **5g**: tests.
  - `Elysium.Substrate.NpcKernelBaseSplit.{TypeWords, BaseBodies, Pairs, Bindings}` — a
    base-only probe (an `FElysiumNpcBase` with no Troika object, `AsNpc()` null), the pair order
    (`TaskFail`, `Serialize` base-first), and the descriptor chain with base rows inherited
    uncopied.
  - `test_gen_kernel_shape.py::test_the_slot_surface_splits_by_layer`.
- **5h**:
  - The overlay retargets (`FElysiumNpcBase::` targets, the component-method rows).
  - Ledger, shape and lists regenerated; the oracle (`docs/vtmb/npc-ai/shape.md`: the two layers,
    `+0x94`/`+0x98`, the base direct calls).
  - The plan and spec status.
  - The phase pin in `test_kernel_migration.py`.
  - The checker's `collapse:` branch, its file-level consumer rows, and the struct parser's
    inline-body fix.

## Acceptance ([acceptance-step5.json](../acceptance-step5.json))

- Runtime gate (`step5/gate`):
  - Build green.
  - **1,274 Substrate + 14 Content + 1 PlayerWorld, zero failures.**
- `test_delta` against step 4r's gate passes with 12 reviewed expectations
  ([expectations/step-5.json](../expectations/step-5.json)):
  - the four new tests;
  - eight map-epoch-only diagnostic shifts caused by the new fixture worlds.
  - No stub observation or diagnostic changed otherwise: Troika instances dispatch and tally
    exactly as before.
- Checks gate: the five generator checks and the kernel tooling pytest pass, and
  `--check step0..4` and `factories` pass. `--check step5` is **PASS** at phase 5.
- Map smoke (`step5/smoke.json`):
  - `sp_tutorial_1`, `ch_fishmarket_1`, `sp_giovanni_2b`, `hw_warrens_4` and `sm_pawnshop_1`
    load with no Elysium errors.
  - The rats' eight keys read back, with class chain `… CAI_BaseNPCTroika < CAI_BaseNPC < …`.
  - There was no in-game save/load (the MCP surface has no verb for it). The `Pairs` case
    round-trips the base record half instead.

## Carried forward

- Step 6: the entity-chain relocation of the bodies now on `FElysiumNpcBase`, the 8 entity-method
  merges, the stub dispositions and diagnostic remaps.
- Step 7: the FrenzyShadow `BestEnemy` census arm (a transitional Troika override).
- Fold 9: the senses object (`SensesObject()` null on the base) and `GatherSight`'s `AsNpc()`
  route.
- The base's generated `GetClassScheduleIdSpace` (slot 580) stays a stub beside the typed
  `ClassScheduleIdSpace`.
- `ElysiumNpcCamera`'s own `NPCInit` copy still writes `DesiredMoveYaw`.
- After the owner commits, record `history.step5.commit` in the manifest (the checker then reads
  the accepted tree).
