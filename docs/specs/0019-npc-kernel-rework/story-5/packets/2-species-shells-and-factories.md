# Step 2, packets 2a–2h — species shells and factory correction

Start: `0019-5-class-tree`, `b2261eca` (step 1 accepted), clean tree, manifest `step-1-dead-species-deletion`.
Executor: Claude Code, Opus 5.5; four fixture-migration workers and two re-pin workers edited disjoint
test files, one integration owner built and gated.
Outcome: the 44 step-2 species classes stand as C++ types, every ordinary-NPC classname builds the
class retail's factory builds, and the census answers the factories.
Record: [registrations-step2.tsv](../registrations-step2.tsv), [decisions-step2.json](../decisions-step2.json),
[expectations/step-2.json](../expectations/step-2.json), [acceptance-step2.json](../acceptance-step2.json).
Evidence root: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step2/`.

| Packet | Result |
|---|---|
| 2a | `kernel_migration_step2.py` + tests; `--check step1` reads the accepted tree once phase 2 is current; registration and decision records |
| 2b | `kernel_ledger.factory_classnames`: `classes.md` and `ElysiumNpcKernelShape.cpp` take classnames from `factories.tsv` (74 names, one claimant each); the most-derived claimant rule is deleted |
| 2c | `FElysiumNpc` loses `final`; `RetailClass()` = test latch or the virtual `OwnRetailClass()`; 44 `ElysiumNpc<Name>.h/.cpp` shells |
| 2d | abstract `CAI_BaseNPCTroika` projection + 44 abstract retail descriptors + 45 typed classname factories; `bAbstract` refusal in `Create`; `FElysiumEntityDef::InternalFactory`; `ClassHolstersOnState` ProneDialog names; `TransformModel` pending input |
| 2e | 133 `SetRetailClassForTests` sites migrated to real factories or the bare Troika line (`AddTroikaNpc`/`AddNpcOfClass`); loops split into independent instances; two deferred FrenzyShadow/PlayerController/WolfMorph sites remain; three new `NpcKernelClass` factory tests; wrong-answer pins re-pinned (`CensusFallThrough` → `CensusFactories`) |
| 2f | stale "npc_VCop resolves to nothing" and "one leaf for every classname" comments corrected (source and `gen_kernel_shape`) |
| 2g | identity audit recorded in `decisions-step2.json` (`audits`, `retail_corrections`) |
| 2h | map smoke over 13 baked maps; full gate; `test_delta` against step 1's reports |

## Findings

- **Cop:** a placed or maker-made `npc_VCop` is now `CNPC_VCop`, dispatching the rows its census
  chain carries. Its own rows are `IRelationType` `0x10372b70`, the slot 597 prologue `0x10372cc0`
  and the slot 123/124 debug bodies. It inherits slot 453 `0x10387520` from
  `CNPC_VHumanCombatant`, and slot 605 `0x10386560` and the melee bodies at 599–602 from
  `CNPC_VHuman` (the census row `{CNPC_VCop, 599, 0x10385ab0}` is that shared body). It also runs
  in `CNPC_VCop`'s schedule space.
- **Payphone:** a spawned `CPayphone` thinks on its first frame, so its slot-431 counters start
  non-zero. The case now reads deltas.
- **Zombie maker:** a zombie maker's `npc_VZombie` child is live, not an invalid child.
- **Unported stubs now reached:** newly active classes reach `Hide` `0x1009d2a0` (Zombie),
  `StudioFrameAdvance` (Payphone), `HasUsableRangedWeapon` (Yukie, Hengeyokai) and
  `task_wait_indefinite` (Camera). Hengeyokai's `StartTransformation` input has no binding until
  step 4.
- **`population.md` corrections:** `npc_VVampireBoss` is placed 3 times (not "never authored").
  `npc_VCop` has 16 placed and 119 maker rows across the 108 exported maps.

## Review follow-up (2026-09-25, packet 2r)

An independent review of the step found no runtime blocker, one acceptance blocker and ten
should-fix items. Resolved:

- **Retail recovery: the activation pass** (`decisions-step2.json` `retail_corrections.activate_pass`,
  `lifecycle.md`).
  - Retail: `ServerActivate` `0x1011aaf0` walks the tail-appended entity list, so what an earlier
    `Activate` creates is activated later in the same pass. Its only skip is `EFL_DORMANT`
    (`m_iEFlags & 2`), which only `MakeDormant` `0x100a8060` sets, and it never tests `EFL_KILLME`.
  - Port: new word `FElysiumEntity::bEflDormant`, set and cleared by `camera_animated`. The
    `globalname` arms are an empty seam, because no shipped map authors one. An entity killed
    during the pass is still activated (`ActivateListedEntity`). The port previously skipped every
    dead entity and activated dormant ones. Test: `EntityWorld.ActivatePass`.
- **Retail port: `TweakParam`.** `CAI_BaseNPCTroika::InputTweakParam` `0x1029ea40` is ported: it
  `strtok`s the argument on `", "` into two tokens for slot 585. Before step 2 only the
  `npc_VCamera` stub row answered this name, and retail scripts call it.
- **Seam: `SpawnTempParticle`.** `CBaseAnimating::InputSpawnTempParticle` `0x1008d8b0` is
  registered as an addressed stub surface on every animating entity. Previously only the
  `npc_VLasombra` stub answered it.
- **Record correction:** `npc_VVampireBoss` was already resolved by the old census, so 6
  identities are corrected, not 7. `factories.tsv` `prior_census_class` is fixed and propagated.
- **Tests:**
  - The Lifecycle19 `NPCInit` assertions now write a sentinel first, so they fail if the call does
    nothing.
  - State19 CameraPreSelect resets its selector.
  - The maker slot-104 arms are asserted on real maker classnames (`MakerArmCoverage`).
  - `MakerSpawnZombie` stands a real `npc_maker_zombie`. `FElysiumNpcMaker::SetZombieMakerForTests`
    is deleted.
  - The remaining species fixtures moved to real classnames, with dispatch assertions added:
    Debug, Debug10 (the cop's slot-123 body on a spawned cop), Conditions10, Dialogue, Senses and
    Squad. Yukie slots 363 and 602 stay direct-body until step 3 wires them.
  - The factory tests cover:
    - the 45 + 20 + 9 = 74 partition;
    - the deferred classes' prior bases;
    - per-classname input and field resolution, `TransformModel` on exactly the vampire-boss line;
    - runtime-path refusal and Cop space ≠ Troika.
- **Checker hardening:**
  - The latch scan is multi-line, covers the whole module, counts duplicate sites and refuses
    production callers.
  - The registrar check verifies both registration loops.
  - The census check verifies each row's array binding and count.
  - Newly active or corrected rows must have stood in the game, and the smoke and creation-path
    evidence hashes are pinned.
- **Smoke:** every one of the 45 classnames has stood live in the running game. The 21 not placed
  in baked maps, or not visited there, stood through the retail script path
  (`CreateEntityNoSpawn` + `CallEntitySpawn`) on `sm_pawnshop_1`, with no errors.

## Gate

- **Build:** green.
- **Runtime suites:** 1,262 Substrate (1,255 + 7 new), 14 Content and 1 PlayerWorld, zero failures.
- **Regression comparison:** `test_delta` against step 1's final reports passes with 244 reviewed
  expectations: 236 diagnostics, 7 additions and 1 rename. The diagnostics are receiver renames,
  extra instances, the stub receivers above and the added species `TaskFail` dispatch. An engine
  HTTP-probe timeout from an earlier run did not recur, and its expectation was removed.
- **Tooling checks:**
  - five generator `--check`s pass;
  - the Python tests pass (105);
  - `kernel_migration --check step2`, `step1` and `step0` pass.

Corpus baseline:
- **Rebuild:** `corpus.sqlite` was rebuilt during the review follow-up by a separate knowledge apply
  on `vampire.dll`: 1,811 names and 1,975 prototypes. Function rows are identical, call edges changed
  by +89/−3 and field accesses by +909/−885.
- **Pin:** refreshed per §3.1, with the previous hash kept in `history.step2_pins`.
- **Regeneration:** the ledger docs and the metadata in `ElysiumNpcKernelShape.cpp` (per-slot counts
  and story bands) were regenerated against it.
- **Generator fix:** `gen_kernel_shape` refused slot 33 `0x10026690`, because the re-applied
  prototype prints `return -1;` where the verdict records `default:0xffffffff`. It now compares
  the two as one 32-bit return word (`same_word`), so the emitted literal is unchanged.
- **Test fix:** `test_kernel_migration` now counts 8 changed prior identities, not 9, following the
  VampireBoss record correction.

Deviation from the reviewed plan: 2-pre and the C++ step were not split into two commits. The
ledger switch makes the committed census stale until the C++ census is regenerated, so the two
cannot pass their gates separately.

Disposition: accepted with step 2 (see `progress.md`).
