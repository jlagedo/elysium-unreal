# Step 5, packet 5a — preflight, move/binding/consumer records, checker

Start: `0019-5-class-tree`, `03c0d030` (step 4 closed out; 4r accepted), clean tree, manifest
`step-4-species-bodies-words-bindings` (phase 4).
Executor: Claude Code, Opus 5.5 (three read-only review agents for the retail evidence).
Outcome: step 5's scope is a re-derivable record, and the checker that will accept the step exists.
No C++ change. Lands as its own commit.

## Records

- `manifest.json`: `history.step4.commit = 03c0d030…` (the accepted, closed step-4 tree), and
  `step5_packets`. `ACCEPTED_PHASES` gains 5; the phase stays 4 until the step is accepted.
  `--check step4` now reads its accepted tree, as `--check step3` does.
- [moves-step5.tsv](../moves-step5.tsv), the **move manifest**: 1,951 rows, covering every member
  `FElysiumNpc` declares (the generated slot surface aside, which 5f splits by slot) and every word
  of the mixed aggregates (`ScheduleHost`, `Senses`, `Senses.Memory`, `Senses.Perception`).
  - Identity columns are re-derived by `--check step5` from the accepted step-4 tree: member, kind,
    declaring file, defining files, cited retail addresses, and the layer those addresses and
    bound words name.
  - Dispositions:
    - 518 `base`;
    - 23 `rename:<slot>`, the `Base*` helpers that become the base's own slot bodies,
      including the two step 3 recorded;
    - 3 `pair` (`TaskFail`, `Serialize`, `ClassScheduleIdSpace`);
    - 1 `collapse` (`FUN_1027e0f0`, a second port body of `BaseSlot532`'s `0x1027e0f0`);
    - 39 `split` component words: 22 to `BaseScheduleHost`, 14 to `BaseMemory`, and 3 senses
      words that become base members;
    - 2 `chain` (the float-sound words to `FElysiumCombatCharacter`);
    - 1,365 `stay`, of which 11 are named transitional homes.
- [fields-step5.tsv](../fields-step5.tsv), the **binding manifest**: the 98 named `CAI_BaseNPC`
  datamap records in the pinned replay: 53 save, 4 key, 16 outputs, 1 input, and 24 unbound with
  the generator's reason. Every bound or saved word names its `FElysiumNpcBase` storage.
- [consumers-step5.tsv](../consumers-step5.tsv): the 118 `AsNpc()` call sites in 104
  definitions.
  - 8 sites (7 rows plus one mixed row) port a retail `+0x94 m_pBaseNPC` test and move to
    `AsNpcBase()`.
  - The rest keep `AsNpc()`: 32 Troika, 16 port-only, 27 with no retail evidence either way,
    and 26 test definitions.
- [decisions-step5.json](../decisions-step5.json) records the owner decisions (file layout, slot
  surface split, commit split), the rules, packets 5b–5h, the findings, 7 retail corrections (one
  open), the transitional homes and the unported base bodies.

## Findings that change the plan

- **`+0x98` is the Troika pointer.** Retail base bodies reach Troika state through
  `CBaseEntity +0x98 m_pBaseNPCTroika` without a cast (`0x1027de00`, `0x10272130`,
  `0x102e19e0`). The port's `AsNpc()` is that word. `+0x94 m_pBaseNPC` becomes `AsNpcBase()`.
- **Merged carriers.**
  - `ScheduleHost.DesiredMoveYaw` stands for three retail words: base `NPCInit` writes the
    motor's `m_IdealYaw`, the stop-moving task sets the `move_yaw` pose parameter, and only Troika
    bodies (and the motor through `+0x98`) write `+0x63ec`.
  - `Senses.Memory.LastDamageAttacker` stands for `+0x5b7c` (base) and `+0x660c` (Troika).
- **The senses object stays on the Troika.** `m_pSenses +0x5cdc` is a base word, but the port
  runner reads Troika words throughout. Moving its 17 base words out costs a third of the edits
  that moving its 60 Troika words out would. The base declares `SensesObject()` (null until fold 9).
- **Port routing is Troika-side.** The port's `RunAi` replaces base `MaintainSchedule` with its
  dialog, script, policy and autonomous arms. `RunAi`, those arms and `RunConditionPass` stay on
  the Troika (transitional, fold 9).
- **Reconciliation.** Appendix B's "172 + 157 bodies / 263 unnamed helpers" has no reproducible
  source. The CAI_BaseNPC datamap has 98 records, not 160 (the 160 counted layout tiers).
  203 overlay targets name `FElysiumNpc::` for base addresses.

## Checker

`kernel_migration_step5.py` (`--check step5`):
- **Always:** binding-manifest identity against the pinned replay. Move-manifest and consumer
  identity against the accepted step-4 tree.
- **At phase 5:**
  - no `investigate` row;
  - a `base`/`rename` member is declared on `FElysiumNpcBase` (its header or an included `.inl`),
    defined `FElysiumNpcBase::` in an `ElysiumNpcBase*.cpp`, and gone from `FElysiumNpc`;
  - a `pair`'s Troika side is an `override` of a base virtual;
  - a component word resolves at its final path and no longer at its old one;
  - every base binding row is generated in `AddNpcBase*Fields` with base storage, and not in the
    Troika's functions;
  - each `AsNpcBase()` consumer spells it;
  - the descriptor chain is `CAI_BaseNPC` → the combat character → `CAI_BaseNPCTroika` beneath;
  - no overlay target names a moved member on `FElysiumNpc`;
  - `--check step4` passes on its accepted tree;
  - the pinned acceptance receipt verifies.

## Checks

- `pytest pipeline/tests/test_kernel_migration_step5.py`: 12 passed. All migration, ledger and
  shape tests: 132 passed.
- Five generator `--check`s: exit 0.
- `kernel_migration --check step4`: PASS (accepted tree). `--check step5`: PENDING; all three
  records match.

Evidence and drafting tools: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step5/`
(`tools/draft_moves.py`, `tools/draft_fields.py`, `asnpc-consumers.tsv`, `review-transitional.json`).

Disposition: complete; ready to commit as the step-5 prerequisite.
