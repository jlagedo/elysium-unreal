# Step 5, packet 5b — pure rename of the Troika family files

Start: `0019-5-class-tree`, `571561aa` (5a committed), clean tree, manifest phase 4.
Executor: Claude Code, Opus 5.5.
Outcome: the family files that stay with `FElysiumNpc` carry the class's name. No symbol and no
behaviour changes. This lands as its own commit, before the step-5 split
(`.claude/rules/cpp.md`: a pure move is a separate commit).

## What moved

124 files moved with `git mv`: `Substrate/ElysiumNpcKernel<Family>.{inl,cpp}` and
`…<Family>Shared.h` became `Substrate/ElysiumNpc<Family>…`. The full map is in
`$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step5/rename-5b.json`, and the tool is
`step5/tools/rename_5b.py`.

- **Collisions.** `Conditions`, `Dialogue` and `Senses` would take the names of the existing
  component files (`ElysiumNpcConditions.h`…). They become `ElysiumNpc<Family>Bodies`, and their
  `Shared.h` files follow.
- **Kept names.**
  - The infrastructure files: `Shape`, `ShapeMap`, `SpeciesShapeMap`, `Bindings`,
    `ClassLookup`, `Tunables` and `Slots`. `Slots` is split by 5f.
  - `ElysiumNpcKernelBaseHelpers`: its base-layer helpers become the base family
    `ElysiumNpcBaseHelpers` in 5e. Renaming it here would mislabel its Troika leftovers.
  - `Tests/ElysiumNpcKernel*Tests.cpp` and the automation test ids.
- **References.** 228 files were edited, and every changed line differs only in a family name
  (3,954 lines out, 3,954 in, symmetric after name normalisation):
  - the in-class `#include`s in `ElysiumNpc.h`, the other includes and the comments in `Source`;
  - the prose citations in `docs/vtmb`;
  - the file-qualified targets and evidence text in `kernel_verdicts.tsv`.

  The generated ledger (`docs/vtmb/npc-kernel`) was regenerated; its diff is only citation order
  and names. Four comments that name files step 4 already deleted (`ElysiumNpcKernelMotor2.cpp`,
  `…Anim10_2.cpp`, `…Bosses2.cpp`) keep their historical names.
- **Records not changed.** The accepted-step records under `docs/specs` and the historical checkers
  keep the old names, because they describe and read their own trees.
- **One tool fix.** `kernel_migration_audit.maker_shadows` globbed `ElysiumNpcKernel*.inl` on the
  current tree. It now follows the includes of `ElysiumNpc.h` / `ElysiumNpcBase.h`, and still
  finds the seven duplicate maker words.
- **Pins.** `manifest.json` `history.step5_pins`: `verdicts` (the file-qualified targets) and
  `registry` (one comment in `ElysiumNpcClasses.cpp`) are refreshed. No verdict and no target
  symbol changed.

## Gate

Build green (incremental). On the fixed tree:

| Suite | Tests |
|---|---:|
| `Elysium.Substrate` | 1,270 |
| `Elysium.Content` | 14 |
| `Elysium.PlayerWorld` | 1 |

Zero failures in all three. A first gate run was discarded: the tree was stashed and restored for
seconds during its Content suite, and the whole gate was rerun untouched.

`test_delta` against step 4r's gate (`step4r/gate/run.json`) passed with **zero expectations and
zero differences**, 1,285 tests before and after
([expectations/step-5b.json](../expectations/step-5b.json)). All of these pass:
- the five generator `--check`s;
- pytest, including `test_kernel_migration_step5.py`;
- `kernel_migration --check step0/1/2/factories/step3/step4`;
- `--check step5`, which reports PENDING.

Evidence is under `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step5/5b/gate/`:

| File | sha256 |
|---|---|
| `run.json` | `b9ecb544bf43595fb6fdd79872e12bafee3b6cb06b9fe6a7e7ba1aada9026100` |
| `runtime-results.json` | `0a5207a634c8526dfe183f0ae8dc18852784920372834ad740297857ac3a5196` |
| `checks.json` | `146681efa77ce7ca4e386ac7a359bf888497d9ef4cab1b5ea1bbe1fa4fe169c7` |
| `delta.json` | `b24138454f87c5aa97dd18cc473a8637ef5c67f2128179114290b8121ac849b9` |

Disposition: complete. Next: packets 5c–5h, the one step-5 commit.
