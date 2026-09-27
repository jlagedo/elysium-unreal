# Step 6, packet 6b — `FElysiumAnimatingOverlay` and `FElysiumFlex`

Start: `0019-5-class-tree`, `33fcd525` plus packet 6a (records and checker, uncommitted).
Executor: Claude Code, Opus 5.5.
Outcome: the port's entity chain has retail's two nodes between the animating class and the combat
character: `CBaseAnimating` → `CBaseAnimatingOverlay` → `CBaseFlex` → `CBaseCombatCharacter`, as
C++ classes and registry descriptors. No behaviour changes. Lands as its own commit.

## Changes

- `Public/ElysiumPlayer.h`:
  - declares `FElysiumAnimatingOverlay : FElysiumAnimating` and
    `FElysiumFlex : FElysiumAnimatingOverlay`;
  - `FElysiumCombatCharacter` derives from `FElysiumFlex`;
  - adds the chain-node names `ElysiumAnimatingOverlayClassName()` / `ElysiumFlexClassName()`.
- Both classes were declared in `ElysiumPlayer.h` beside the other two chain nodes. **Superseded by
  packet 6r:** `.claude/rules/cpp.md` puts one class per file. `FElysiumAnimating` moved verbatim to
  `ElysiumAnimating.h` (`bdcfc197`), and the two new classes live in `ElysiumAnimatingOverlay.h` and
  `ElysiumFlex.h`, which `ElysiumPlayer.h` includes. Their cpps are `Elysium<Class>SlotBodies.cpp`,
  which arrived with content in 6c/6d (the generated slot bodies and the moved hand bodies).
- `ElysiumPlayerClasses.cpp`, the chain's one registration site:
  - registers abstract descriptors `CBaseAnimatingOverlay` (base `CBaseAnimating`) and `CBaseFlex`
    (base `CBaseAnimatingOverlay`);
  - `CBaseCombatCharacter`'s base becomes `CBaseFlex`;
  - neither new descriptor carries a field, because their datamaps name no external.
- `ElysiumPlayerWorldTests.cpp` (`Elysium.PlayerWorld`) asserts the new chain:
  - `CBaseCombatCharacter` → `CBaseFlex` → `CBaseAnimatingOverlay` → `CBaseAnimating`;
  - `CBaseFlex` has no factory.

## Checks

- Build: green.
- Runtime gate (`step6/6b/gate/`): 1,274 Substrate + 14 Content + 1 PlayerWorld, zero failures.
- `test_delta` against step 5's gate: passes with **no** expectations. There is no difference,
  including in the PlayerWorld case, whose extra assertions add no diagnostic.
- `gen_kernel_shape --check`: still matches. The new classes add no name the generator reserves.
- The ledger and lists were regenerated: `ElysiumPlayer.h` citation line numbers shifted (slots,
  fields, functions, coverage, unnamed, seam-list). No row changed otherwise.
- Checks gate (`step6/6b/gate/checks.json`): five generator checks, pytest (167) and
  `kernel_migration --check factories/step0..5` pass; `--check step6` is PENDING.
