# Step 6, packet 6a — preflight: slot and member records, checker

Start: `0019-5-class-tree`, `33fcd525` (step 5 accepted as `e0a71ee3` and recorded), clean tree,
manifest `step-5-base-troika-split` (phase 5).
Executor: Claude Code, Opus 5.5 (three read-only survey agents).
Outcome: step 6's scope is a re-derivable record, and the checker that will accept the step exists.
No C++ change. Lands as its own commit.

## Records

- [slots-step6.tsv](../slots-step6.tsv), the **slot record**: 824 rows.
  - One row per generated slot row of the accepted step-5 tree: 552 base, 170 Troika.
  - One row per chain row step 6 adds: 102 chain classes' own bodies at slots where the NPC runs a
    more-derived body.
  - Identity columns are re-derived by `--check step6`: slot, port name, signature, current owner,
    the retail class whose body the row is, body, kind, verdict and introducer. They come from the
    step-5 tree's generated files, the pinned verdict overlay and the pinned corpus's primary vtables.
  - Dispositions:
    - 374 `keep`: the 205 NPC-owned base rows and all 170 Troika rows;
    - 256 `move`: chain-owned stubs and defaults;
    - 84 `move-hand`;
    - 102 `new-chain-row`;
    - the eight integrations (5 `adapter`, 1 `seam`, 1 `implemented`, and `Weapon_Switch` /
      `GetModelIndex` as stubs that move);
    - 1 `delete-dead` (`ReportOverThinkLimit` 582).
- [moves-step6.tsv](../moves-step6.tsv), the **member record**: 130 members the chain-owned hand
  bodies reach, each with the class it follows. The closure rule is in
  [decisions-step6.json](../decisions-step6.json) `rules.hand_closure`. By destination:
  | Destination | Members |
  |---|---:|
  | Entity | 42 |
  | Animating | 16 |
  | AnimatingOverlay | 14 |
  | Flex | 14 |
  | CombatCharacter | 44 |
- [decisions-step6.json](../decisions-step6.json) records:
  - the owner decisions (the two chain classes, owner-prefixed stubs, dead-only deletion, the
    commit split);
  - the rules;
  - the findings;
  - the two planned retail corrections;
  - the integrations and the obsolete machinery;
  - the 50 re-keyed `surviving_sites` (55 gate tokens, on the current file names).

## Findings that change the plan

- **The eight "merges" are not same-name C++ methods.** 6e gives each its audited disposition.
  `Weapon_Switch` `0x1032dde0` stays a counting stub on `FElysiumCombatCharacter`:
  `SetActiveWeapon` ports only its swap, so an adapter would publish a partial body under the retail
  name.
- **Retail chain.** The chain is `CBaseEntity` → `CBaseToggle` → `CBaseAnimating` →
  `CBaseAnimatingOverlay` → `CBaseFlex` → `CBaseCombatCharacter`.
  - `CBaseToggle` introduces no slot, but its own bodies sit at 9 slots. They stand on
    `FElysiumAnimating`, the port's stated no-toggle-node divergence.
- **Carriers.**
  - `AttackExtentsCm` (+0x50) and `SetAttackExtents` (slot 15) are `CBaseEntity`'s: step 5 put them
    on the base host and the Troika.
  - `NpcFlags` stands for `m_bfAINPCFlags`/`m_bfAINPCFlags2`, which are `CBaseCombatCharacter`
    words.
  - All three move up with their readers.
- **Closure.** Of the 84 hand bodies, three keep a retail `+0x98` arm through `AsNpc()`
  (`FireBullets`, `FInAimCone`). None is blocked.
- **Reconciliations.**
  - The headline is 269 (83 defaults), not 268/82.
  - Packet 5e-5f's base stub/hand counts are stale.
  - The "38 linker-folded" bodies are identified.

## Checker

`kernel_migration_step6.py` (`--check step6`):
- **Always:** checks slot-record identity against the accepted step-5 tree and the pinned corpus,
  plus record shape (the owner rule, `move-hand` needing a hand row, dead-only deletion, new rows on
  their own class).
- **At phase 6:**
  - every row is declared in its owner's generated slot file and in no other;
  - a deleted stub is gone and its overlay target is `-`;
  - adapter/seam members exist;
  - an implemented slot has no generated declaration;
  - every moved member is in its owner's scope and gone from `FElysiumNpcBase`;
  - the dispatch gate equals `surviving_sites`;
  - the retired tables are gone;
  - `kernel_shape --unported` equals `unported-step6.tsv`;
  - `--check step5` passes;
  - the `acceptance-step6.json` receipt verifies.

## Checks

- `pytest pipeline/tests/test_kernel_migration_step6.py`: 9 passed.
- `kernel_migration --check step6`: PENDING; the records match.

Evidence and drafting tools: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step6/`
(`chain-tables.json`, `slots-draft.tsv`, `closure6-transitive.json`, `sites-rekeyed.json`,
`tools/`).
