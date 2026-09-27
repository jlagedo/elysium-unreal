# Story 5 — step 6 accepted (`7d63e7fa`); review follow-up 6r on the working tree

**Step 6 (every slot body on its retail owner), 2026-09-27.** Record:
[packets/6-slot-owners.md](packets/6-slot-owners.md).
- 6a: records and checker, committed `5ddaee2c`.
- 6b: `FElysiumAnimatingOverlay`/`FElysiumFlex`, committed `782a27e6`.
- 6c-6i, committed `7d63e7fa`:
  - the per-owner generated surface;
  - 84 hand bodies plus 129 members up the chain;
  - the eight integrations;
  - the dead stub 582 deleted;
  - `kernel_shape --unported` (987 rows pinned);
  - five new tests.
- Acceptance ([acceptance-step6.json](acceptance-step6.json)):
  - runtime gate 1,279 + 14 + 1, zero failures;
  - `test_delta` vs step 5 with 142 expectations;
  - five-map smoke clean.

**Review follow-up 6r (2026-09-27)**, [packet 6 § 6r](packets/6-slot-owners.md). It finishes the
plan as written.
- The pure move of `FElysiumAnimating` is committed as `bdcfc197`.
- The rest of 6r:
  - `ElysiumAnimatingOverlay.h` / `ElysiumFlex.h`;
  - `m_iIsOblivious`/`m_bfNPCFrenziedFlags` on `FElysiumNpcBase`;
  - per-class slot tables and per-owner probes;
  - the C++ census cases (`SlotOwners`, `FieldOwners`, `RegistryMatchesFactories`);
  - slot 580 typed;
  - `Weapon_Switch`'s verdict row;
  - every deferral mapped to a step or story.
- Gate on the working tree:
  - build green;
  - 1,281 + 14 + 1, zero failures;
  - `test_delta` vs step 6 with 12 expectations;
  - checks gate green.
- `acceptance-step6r.json` is verified by `--check step6`.

### Handoff

- `history.step6.commit` is recorded as the 6r commit (the 4r precedent). `accepted_6c_6i` keeps
  `7d63e7fa`, and the verdicts pin is refreshed under `history.step6r_pins`.
- Next: step 7 (the controller fold).

# Step 5 — accepted (`e0a71ee3`)

**Step 5 (separate `CAI_BaseNPC` from Troika), 2026-09-26.**
- **5a** (records and checker, no C++), committed `571561aa`:
  [packets/5a-preflight.md](packets/5a-preflight.md), [moves-step5.tsv](moves-step5.tsv),
  [fields-step5.tsv](fields-step5.tsv), [consumers-step5.tsv](consumers-step5.tsv),
  [decisions-step5.json](decisions-step5.json).
- **5b** (pure rename of the Troika family files `ElysiumNpcKernel<Family>` -> `ElysiumNpc<Family>`,
  collisions `…Bodies`): [packets/5b-troika-family-rename.md](packets/5b-troika-family-rename.md).
  Gate green (1,270 + 14 + 1), `test_delta` vs step 4r empty, all checks pass; `--check step5`
  PENDING. Manifest phase 4.

**Working tree (uncommitted; 5c-5h land as one commit):**
- **5c done** (checkpoint `step5/checkpoints/5c-done.patch`, sha256 `e92620b0…`):
  - `Substrate/ElysiumNpcBase.h/.cpp`: `FElysiumNpcBase : FElysiumScriptedCharacter,
    IElysiumScheduleRunner`, carrying the retail-class identity API (`RetailClass`,
    `OwnRetailClass`, `IsRetailClass`, `AsSpecies`, `OwnRetailClassDerivesFrom`, the test latch
    fields); `FElysiumNpc : FElysiumNpcBase`.
  - `FElysiumEntity::AsNpcBase()` (`+0x94`).
  - `CAI_BaseNPC` descriptor under the combat character, with `CAI_BaseNPCTroika` beneath it.
  - Factory tests use the two-layer chain, and `CAI_BaseNPC` is refused as abstract.
  - Build green; `Elysium.Substrate` 1,270, zero failures.
  - The seven identity rows of `moves-step5.tsv` are `base`/5c.
  - The `consumers-step5.tsv` switches wait for 5e: the consumers read members that move then.

- **5d done** (checkpoint `step5/checkpoints/5d-done.patch`, sha256 `130a4f22…`):
  - 193 declarations (fields and their nested types) moved onto `FElysiumNpcBase`: into
    `ElysiumNpcBase.h` and 19 `ElysiumNpcBase<Family>.inl` files (tool `step5/tools/relocate5.py`).
  - Components split: `BaseScheduleHost` (22 words, `SetWaitFinished`, the base `Serialize`);
    `BaseMemory` (15 words, `ESeen`/`Seen`/`Reset`/`Serialize`/`Rebase`); `PendingSounds` and
    `HeardConditions` onto the base (the senses runner stays Troika).
  - Float-sound words onto `FElysiumCombatCharacter`, and `floatfreq` binds on the combat
    character.
  - `gen_kernel_bindings` splits `NpcBase`/`Npc`, and `CAI_BaseNPC`'s descriptor carries its own
    rows (`BuildNpcBaseClass`).
  - Corrections: `+0x5cd8` had two carriers (merged); `+0x660c` rebinds to the Troika's
    `LastDamageAmount`.
  - `CDialog` and the conversation place stay Troika-side.
  - Build green; `Elysium.Substrate` 1,270, zero failures; step-5 source checks pass for all
    242 5c/5d rows, the binding manifest and the registry chain; five generator checks pass.

  - Full gate on this tree: 1,270 Substrate + 14 Content + 1 PlayerWorld, zero failures.
- **Checkpoint commit** (owner request, 2026-09-26): 5c-5d are committed as an intermediate
  `wip` commit so the work survives a context reset. Step 5 is NOT accepted. `test_delta`,
  map smoke and `acceptance-step5.json` belong to the final step-5 tree, and the manifest phase
  stays 4.

- **5e + 5f done together** (checkpoint `step5/checkpoints/5e5f-done.patch`; the slot split and
  the base bodies need each other to compile). Record: [packets/5e-5f-bodies-and-slot-split.md](packets/5e-5f-bodies-and-slot-split.md).
  - 5e: 309 base methods, 23 renames (two keep their `Base*` retail-id bodies behind typed slot
    bodies), the three pairs (`TaskFail`, `Serialize`, `ClassScheduleIdSpace`), the collapse;
    the eight `+0x94` consumers on `AsNpcBase()`; `SensesObject()`; the `+0x98` arms on
    `AsNpc()`; the step-3 base direct calls requalified; the recorded retail corrections plus two
    new ones (`TaskFail`'s base half, the base host words).
  - 5f: `gen_kernel_shape` emits `ElysiumNpcBaseSlots.*` / `ElysiumNpcSlots.*`; 172 hand slot
    bodies and a 135-member helper closure moved to the base; overlay retargeted (361 targets,
    23 renames, one new row); ledger, shape and lists regenerated.
  - Build green; `Elysium.Substrate` 1,270, zero failures; five generator checks, `--check step0..4`
    PASS, `--check step5` PENDING (records match); step-5 source checks: moves 5c-5e (726 rows),
    consumers, registry, overlay owners, fields all pass; kernel tooling pytest 132 passed.

- **5g + 5h done**: [packets/5-base-troika-split.md](packets/5-base-troika-split.md). Acceptance
  on the working tree: `--check step5` PASS at phase 5 (revision `step-5-base-troika-split`);
  1,274 + 14 + 1, zero failures; `test_delta` vs step 4r passes with 12 expectations; five-map
  smoke clean. [acceptance-step5.json](acceptance-step5.json).

### Handoff: next is step 6

- Step 5 committed as `e0a71ee3`; `manifest.json` `history.step5` records it, so `--check step5`
  now reads that tree.
- Next: step 6 (the plan's §5 step 6): the entity-chain relocation of the bodies on
  `FElysiumNpcBase`, the 8 entity-method merges, the stub dispositions and diagnostic remaps.

## Step 4 (accepted, `bb690d7a`; 4r `61aa2cd8`; closed `03c0d030`)

**Step 4 (species bodies, words and bindings), 2026-09-26.** 4a landed as its own commit
(`9f087e42`). Packets 4b–4i landed as one commit, `bb690d7a` on `0019-5-class-tree`, recorded in
[packets/4-species-bodies-words-bindings.md](packets/4-species-bodies-words-bindings.md) (with the
[4b](packets/4b-generator-and-bindings.md) and [4c](packets/4c-carriers-and-relocation.md) records),
[moves-step4.tsv](moves-step4.tsv), [fields-step4.tsv](fields-step4.tsv),
[decisions-step4.json](decisions-step4.json), [expectations/step-4.json](expectations/step-4.json)
and [acceptance-step4.json](acceptance-step4.json). Manifest phase 4, revision
`step-4-species-bodies-words-bindings`.

Outcome: every introduced species' bodies, words and datamap bindings live on its own class. The
class-keyed tables became override bodies (six survivors hold only Troika or deferred rows), and the
tutorial rats' eight keys land through `Construct`. Retail corrections: the MingXiao proxy gate, the
Werewolf `TaskFail` → own `CheckStuck`, the Scurrying flee march, and the merged and split carriers.

Validation on the final tree: build green; **1,270 Substrate + 14 Content + 1 PlayerWorld**, zero
failures; `test_delta` against step 3's final gate passes with 107 reviewed expectations; five
generator checks, pytest (142) and `kernel_migration --check step0/1/2/factories/step3/step4` pass.
Map smoke (`sp_tutorial_1`, `ch_fishmarket_1`, `sp_giovanni_2b`, `hw_warrens_4`, `sm_pawnshop_1`)
shows no Elysium errors. It ran before the last two-word move; that move changed no behaviour, and
the full gate reran afterwards. Combat was not driven in game.

**Review follow-up 4r (committed `61aa2cd8`):** the step-4 review's defects are fixed. Bach holy-light compare,
Werewolf `CheckStuck` body and `TaskFail` order, the MingXiao throw-mode merge and saved
`m_rbProxyRegistered`, the zombie gib/head-hit finding, the Sheriff seed unit, four stale overlay
targets, a stronger checker and the test gaps. Gate green (1,270 + 14 + 1), delta vs step 4 with 4
expectations, `--check step4` verifies [acceptance-step4r.json](acceptance-step4r.json). Record:
[packet 4 § 4r](packets/4-species-bodies-words-bindings.md).

Carried forward: the Scurrying callers `0x103ac500`/`0x103ac740` (story 8 pass I); deferred homes and
the surviving tables until steps 7-10; step 5 renames the two `Base*` helpers.

## Step 3 (accepted, `21bb56cf`)

**Step 3 (replace introduced-species dispatch) passed its acceptance gate on 2026-09-26 on
`0019-5-class-tree`.** Packet 3a is committed as `48c6ef29`; the C++ step (3b–3j) is committed as
"feat(npc-kernel): story 5 step 3 -- species dispatch as overrides". `uv run elysium research
kernel_migration --check step3` exits 0; it re-verifies step 2 against `870360e9`. Evidence is pinned
in [acceptance-step3.json](acceptance-step3.json); the packet record is
[packets/3-species-dispatch.md](packets/3-species-dispatch.md).

Manifest: phase 3, revision `step-3-species-dispatch`. The verdicts pin was refreshed after the 83
overlay retargets (repo-internal, as in `b2261eca`).

## Step 2 (accepted, `870360e9`)

Step 2 passed its acceptance gate on 2026-09-25 and is committed as `870360e9`.
`uv run elysium research kernel_migration --check step2` exits 0. It re-verifies step 1 against
`b2261eca` and step 0 against `aa1c3c86`. Evidence is pinned in
[acceptance-step2.json](acceptance-step2.json); earlier receipts stay in
[acceptance-step1.json](acceptance-step1.json) and [acceptance.json](acceptance.json).

Manifest: phase 2, revision `step-2-species-shells-and-factories`. Pins refreshed under
`history.step2_pins`: census `classes.md`, registry `ElysiumNpcClasses.cpp`, and corpus
`corpus.sqlite`. The corpus was rebuilt on 2026-09-25 outside this step by a knowledge apply on
`vampire.dll`: names and prototypes were re-applied, function rows are unchanged, and call edges and
field accesses moved. It is taken as the new baseline (plan §3.1). The ledger and
`ElysiumNpcKernelShape.cpp` metadata were regenerated against it, and the runtime gate was re-run.

Completed:

- [Step 0](packets/step-0-inventory-and-rehearsals.md): manifest, factory identities, inventory,
  rehearsals.
- [Step 1](packets/1b-1g-deletion.md): the dead species subset deleted
  ([1a](packets/1a-deletion-manifest.md) is its record and checker).
- [Step 2](packets/2-species-shells-and-factories.md): 44 species classes, 45 typed classname
  factories, abstract refusal, factory census, fixture migration, map smoke; review follow-up 2r
  (activation-pass port, `TweakParam` port, `SpawnTempParticle` seam, test and checker fixes).

Validation: build green. **1,262 Substrate + 14 Content + 1 PlayerWorld**, zero failures.
`test_delta` against step 1's final gate passes with the 244 reviewed expectations in
[expectations/step-2.json](expectations/step-2.json). Five generator checks and the Python tests
pass. All 45 classnames stood live in the running game. Reports: `E:/elysium-work/research/npc-kernel/story-5/step2/gate/`.

Carried forward (see [decisions-step2.json](decisions-step2.json)):

- Species dispatch still reads `RetailClass()` through census tables. **Step 3** turns those arms
  into overrides. Classname-keyed tables stay until then: `ClassHolstersOnState`, footsteps, and
  the activity aliases (until step 11).
- `SetRetailClassForTests` survives at two deferred-class sites (FrenzyShadow, PlayerController,
  WolfMorph; step 7). `FElysiumNpcMaker::SetZombieMakerForTests` is gone.
- `EFL_DORMANT`'s `globalname` arms (`DispatchSpawn` `0x101d1280`, restores `0x101a2e40`,
  `0x101a3c40`) are an empty seam: no shipped map authors a `globalname`.
- Yukie slots 363 (`0x103ddaa0`) and 602 (`0x103dda10`) are not dispatched to the Yukie yet
  (direct-body tests only); step 3.
- Newly active classes now reach unported stubs, with smoke evidence:
  - `Hide` `0x1009d2a0`
  - `StudioFrameAdvance`
  - `HasUsableRangedWeapon`
  - Camera `task_wait_indefinite`
  - Hengeyokai's unbound `StartTransformation` input (step 4)
- Smoke limits: 11 names are placed only in unbaked maps (they stood through the script path);
  `hw_warrens_1` does not activate from a bare load (no `info_player_start`); the VampireBoss
  protean-swap creation (`MakeNPC` `0x103c75f0`) is unported.
- Step 1's carried items stand: CharTemplateModelName seam, slot 525 OverrideMove rows, and
  retained dead census tables.

## Step 3 (accepted)

Record: [overrides-step3.tsv](overrides-step3.tsv), [decisions-step3.json](decisions-step3.json),
[expectations/step-3.json](expectations/step-3.json), [acceptance-step3.json](acceptance-step3.json).
Evidence root: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step3/` (gate, smoke, checkpoints,
tools).

- [3a](packets/3a-preflight-and-matrix.md): committed `48c6ef29`.
- [3b–3j](packets/3-species-dispatch.md): accepted.

Outcome:

- **Override matrix:** 611 rows, every one resolved with a note: 373 `override`, 3 `own`,
  72 `data-query`, 161 `residue` (story 8), 2 `step4`. Each override is declared `override` on
  its introducing class and defined in its `.cpp`.
- **Direct calls:** 187 qualified calls to the retail callee's owner (1 elided: an empty
  `CAI_BaseNPC` body); 2 step-5 renames (`BaseShouldPlayFloatSound`, `BaseDrawDebugStatOverlays`).
- **Surviving sites:** 49 rows, 56 gate tokens: 29 deferred arms (steps 7/8/9/10), 11 census
  lookups, 5 retail RTDynamicCast type tests, 4 class-keyed data queries.
- **Retail corrections (7):** Zombie 510; slot 482 SCRIPT state; Yukie 600/601/602 wired; slot
  465 wired; vocalisation rows; slot 561 through the vtable (Bach's block runs); runner slot 400.
  Each has a test and a `docs/vtmb/npc-ai` record.

Validation: build green. **1,264 Substrate + 14 Content + 1 PlayerWorld**, zero failures.
`test_delta` against step 2's final gate passes with 22 reviewed expectations (2 additions, 20
diagnostics: factory-spawn "no eye offset" lines, the known zombie `Hide` stub, two facing stubs
now reached through the vtable, and map-epoch shifts). Five generator checks, pytest (120) and
`kernel_migration --check step0/1/2/factories/step3` pass. Map smoke over `sp_giovanni_2b`, `ch_fishmarket_1`,
`hw_warrens_4`, `sm_pawnshop_1` (script path for Bach, Werewolf, CameraSecurity) and `sp_tutorial_1`:
no Elysium errors, only step 2's known stubs. Combat was not driven in game.

Carried forward:

- Residue for story 8: 150 unported species bodies and 11 ported-but-unwired helpers (Yukie 363
  among them: the substrate has no slot-363 dispatch point), plus the one-byte vocal bodies of
  Tzimisce 489/492–495, Newscaster 488–497 and Camera 482/509/510.
- Deferred arms stay listed in `decisions-step3.json` `surviving_sites` until steps 7–10.
- Step 4 moves bodies, words and bindings onto the classes; step 5 renames the two `Base*` helpers.

Do not tick story 5 or tracker 06b until step 11.
