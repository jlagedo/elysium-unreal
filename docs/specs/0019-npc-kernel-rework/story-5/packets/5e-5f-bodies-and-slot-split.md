# Step 5, packets 5e + 5f — base bodies, renames, pairs, and the per-layer slot surface

Start: `0019-5-class-tree`, `826f4bb1` (the 5c–5d `wip` checkpoint), clean tree.
Executor: Claude Code, Opus 5.5.
Outcome: every `CAI_BaseNPC`-layer body the port carries is an `FElysiumNpcBase::` body, the
generated slot surface is split by the layer that declares each slot, and a Troika instance
dispatches exactly what it did before. Lands in the one step-5 commit.

The two packets landed together. The base bodies call slot virtuals that only the split declares
on the base, and the split's base declarations need the base bodies as their hand definitions.

## 5f — the per-layer slot surface

- `gen_kernel_shape` computes a layer row per generated slot:
  - A slot the `CAI_BaseNPC` table holds (below its 583 slots) is declared on `FElysiumNpcBase`
    with that table's body (`ElysiumNpcBaseSlots.inl/.cpp`).
  - Where the Troika table holds another body, `FElysiumNpc` overrides it (`ElysiumNpcSlots.inl/.cpp`).
  - A Troika-introduced slot is declared on `FElysiumNpc`.
  - Each layer row reads its own body's verdict, address, layer and story.
- Counts:
  - Base: 552 slots — 248 stubs, 177 hand, 127 defaults.
  - Troika: 137 overrides and 33 introduced slots.
- Stub text is unchanged. Base stubs fire the base address, Troika overrides the Troika one. The
  census and the default probes read the merged row, the body a Troika instance runs.
- An override of one overload hides the base's others. The Troika surface therefore re-exposes
  `FInViewCone`, `GetEnemy`, `TraceMessage` and `TraceMessageBare` with using-declarations.
- `ElysiumNpcKernelSlots.*` are replaced by the two layer files. The ledger's citation scan,
  the migration inventory and the skeleton reader know the new names; the historical checkers
  keep reading their own trees.

## 5e — the base bodies

**Moves**
- 309 `base` methods moved (`step5/tools/relocate5_defs.py`, the step-4 definition and
  file-local logic keyed by destination file). Declarations went to the base family `.inl`
  files, definitions to `ElysiumNpcBase<Family>.cpp`.
- The 172 hand bodies of generated slots the Troika inherits (`B == T`) are base bodies now. They
  were matched by signature (`5e/gen/slot-def-plan.json`).
- 5a's closure never covered those slot bodies. A second closure (`closure5e.py`) over the
  Troika-only members they reach moved 135 more port helpers, fields and types. Their rows say
  "5e/5f closure".

**Renames**
- 21 of the 23 `Base*` bodies became the base's own slot body; callers spell
  `FElysiumNpcBase::X()`.
- Slots 460 and 461 return the typed `NPC_STATE`, which the retail-id bodies are not. They keep
  their `Base*` names (the rename rule's mismatch clause); typed base slot bodies wrap them, and
  new base virtuals `PreSelectIdealStateRetail`/`SelectIdealStateRetail` give `0x1026f4d0` its
  slot dispatch.
- Const differences took the virtual's signature.

**Pairs**
- `TaskFail`: the base half (`0x10273fc0`) is `FElysiumNpcBase::TaskFail`, and the Troika
  override calls it last, as `0x1029adb0` does.
- `Serialize`: the base record half leads, and the Troika override calls it first.
- `ClassScheduleIdSpace`: a base virtual and its Troika override.

**Collapse**
- `FUN_1027e0f0` is folded into `FElysiumNpcBase::Slot532`, and its test now calls the survivor.

**Consumers, `SensesObject()` and the `+0x98` arms**
- The eight `+0x94` sites call `AsNpcBase()`.
- `SensesObject()` is the `m_pSenses` accessor: null on the base, the Troika's `Senses` on
  `FElysiumNpc`.
- The `+0x98` arms (`OnDoorBlocked`, `ResolveActivityToSequence`, `FireBullets`' fake-reload
  decrement, motor slot 18, `PreSelectIdealState`) test `AsNpc()`.

**Direct calls**
- The step-3 base callees are requalified: `TraceAttack`, `GetShortConditionName` and
  `GatherAttackConditions`.
- `requalify5.py` also requalifies every `FElysiumNpc::X` whose `X` is base-only.

**Free functions**
- Free functions typed `FElysiumNpc&` that base bodies call were retyped to `FElysiumNpcBase&`
  where their bodies are base-layer (Enemy, Conditions, EnemyMemory and the family `Shared`
  helpers).
- `GatherSight`, the sight classifier over the senses runner, stays Troika-typed.

## Retail corrections (`decisions-step5.json` `retail_corrections`)

- `base-pre-select-ideal-state` (RESOLVED, from asm `0x1026f590`): the arm targets `this+0x98`, not a move
  parent. A base-only NPC takes `SquadNewEnemy`. The crash-guard counter is gone.
- `desired-move-yaw-carrier`:
  - `NPCInit` seeds the motor ideal-yaw seam (`MotorIdealYaw`).
  - The stop-moving arm writes the `move_yaw` pose parameter.
  - Motor slot 18 writes `+0x63ec` only through `AsNpc()`.
- `update-on-remove-hint`: the inline release, guarded only on a held hint.
- `last-think-slots`: the citations name the Troika overrides.
- New — `task-fail-base-half`:
  - `m_bShouldMove = 0` (the port wrote nothing at `+0x1a40`).
  - The `+0x5c50` failure code moves to the base host.
- New — `base-host-words`: `m_vecAttackExtents` (`+0x50`) and the hint reuse time join
  `FElysiumNpcBaseScheduleHost`.

## Transitional routes (named)

- `FElysiumNpc::BestEnemy` routes the FrenzyShadow census arm, then calls the base (step 7).
- Base `OnLooked` reaches `GatherSight` through `AsNpc()` (fold 9).

## Checks

- Build: green.
- Tests: `Elysium.Substrate` 1,270 of 1,270, zero failures.
- Generators: the five `--check`s exit 0, after `kernel_ledger`, `kernel_shape` and
  `kernel_lists` were regenerated.
- Migration checks: `--check step0` to `step4` PASS. `--check step5` is PENDING, with the records
  matching.
- Step-5 source checks:
  - `check_moves` over 5c–5e: 726 rows.
  - `check_consumers`, `check_registry`, `check_overlay_owners` and `check_fields`.
  - The checker gained a `collapse:` branch and file-level consumer rows.
- Tooling tests: kernel migration, ledger, shape and generator pytest, 132 passed.
- The pins were refreshed for the verdicts and the registry.
