# Story 5 — step 2 accepted

**Step 2 passed its acceptance gate on 2026-09-25. It is uncommitted on `0019-5-class-tree`
(base `b2261eca`). Step 3 has not started.**
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

**Next:** commit step 2 when the owner asks, then step 3 (replace introduced-species dispatch).
Read the plan's step 3, `decisions-step2.json` and the compatibility inventory first. Do not tick
story 5 or tracker 06b until step 11.
