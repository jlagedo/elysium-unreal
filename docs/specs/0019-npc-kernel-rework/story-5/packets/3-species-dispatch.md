# Step 3, packets 3b–3j — species dispatch as overrides

Start: `0019-5-class-tree`, `48c6ef29` (3a committed on step 2 `870360e9`), manifest
`step-2-species-shells-and-factories`.
Executor: Claude Code, Opus 5.5, one integration owner across three sessions; patch checkpoints
under the evidence root between sessions (`checkpoint-3c3d` … `checkpoint-3i1`).
Outcome: every species body an introduced class runs is reached through a C++ override on its
introducing class. The census string arms, member-pointer tables, species rows, slot-keyed species
tables and `IsRetailClass` self-tests that chose them at run time are gone, except the listed
survivors. Calls a species body makes to what it replaces are exact calls to the retail callee's owner.
Record: [overrides-step3.tsv](../overrides-step3.tsv), [decisions-step3.json](../decisions-step3.json),
[expectations/step-3.json](../expectations/step-3.json), [acceptance-step3.json](../acceptance-step3.json).
Evidence root: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step3/`.

| Packet | Result |
|---|---|
| 3b | virtual surface: the hand-written dispatchers species override become `virtual` (one header batch); `SLOT_PORT_MAP` rows; `BodyDirection2D/3D` exception for the rat |
| 3c | the plan-named corrections: Zombie 510 (retail body, `BaseShouldPlayFloatSound` tail), slot 482 standalone copies (Animal, Human, MingXiao, Tzimisce), Bach 606 and Tzimisce 593 as overrides with qualified Troika calls |
| 3d | `SpeciesSlotRows` Troika-helper rows → overrides (21–26, 588, 497/506, 488, 599–602, 609, Cop 597 prologue); Yukie 600/601/602 and 363 wired; `MeleeSlotLine` deleted |
| 3e | member-pointer tables → overrides: `NPCInit` 420, `StartNPC` 422, `OnRestore` 130, `Precache` 104, `Save`/`Restore` 126/127, `UpdateOnRemove` 180, `SelectIdealState` 461, `OnStateChange` 463, 105/310/375 |
| 3f | `OverrideOf`/`BodyOf`/Strcmp arms and slot-keyed tables by family: conditions/senses, schedule, combat/damage (615/141/292), anim/facing/motor (370/371, 465, 509, 516, 68/69, jump, 259, 525), debug (76/123/124/408/620), geometry/misc/squad/social/hints (24, 193, 337, 424–430, 545/546, 561, 563, 566, 590/592, 295/366, 332), translate/state/lifecycle (440, 461/463, 174/175, 78/511, 434/435, positions, pickups) |
| 3g | vocalisation rows → hook overrides (`SpeciesVocalize`; Camera 488–508 silent, Tzimisce 490/491/487, Werewolf 491/500, SabbatLeader 620/621 own virtuals); footsteps → `HandleAnimEvent` overrides (MingXiao, Hengeyokai, TzimisceHeadClaw, TzimisceRunner; `SpeciesFootstepAnimEvent`) |
| 3h | type tests on other entities recovered: the ChangBros, boss-partner and runner-child checks are retail RTDynamicCasts and stay as typed survivors; the rat hostile-assessment compare stands (`Classify()==0xd`, `0x1017ff40`); runner slot 400 becomes an override |
| 3i | tests re-pinned on real factory instances; stale comments; `shape.md`/`conditions-and-states.md` corrections; matrix and decisions filled; verdict targets retargeted (83) and the ledger regenerated |
| 3j | full gate, `test_delta`, map smoke, acceptance |

## Records

- **Override matrix:** 611 rows, all resolved with a note: 373 `override`, 3 `own`, 72
  `data-query`, 161 `residue` (150 unported species bodies, 11 ported-but-unwired helpers; story 8)
  and 2 `step4` (wrappers equivalent to the inherited body). The checker finds each override
  declared `override` on its class header and defined in its `.cpp`.
- **Direct calls:** 187 recorded caller → required qualified call rows, each from `vtmb_callees`
  same-slot direct calls (`step3/direct-calls.tsv`) or the planned rows. One elided: Guard1's call
  to `CAI_BaseNPC::BuildScheduleTestBits` `0x10280fb0`, an empty body.
- **Step-5 renames:** `BaseShouldPlayFloatSound` (`0x1027a530`) and `BaseDrawDebugStatOverlays`
  (`0x102775e0`) become `FElysiumNpcBase` members.
- **Surviving sites:** 49 rows, 56 gate tokens: 29 deferred (22 step 7 controller line, 3 step 8
  makers, 1 step 9 cine, 3 step 10 TestHull), 11 census lookups, 5 retail type tests
  (RTDynamicCast), 4 class-keyed data queries.
- **Deleted dispatch infrastructure:** `SpeciesDispatchingSlot`, `FSpeciesDispatchScope`,
  `SpeciesDispatchRow`, and the species tables and self-tests the packets name.
- **Overlay targets:** 83 `kernel_verdicts.tsv` targets retargeted to the moved bodies
  (`step3/retarget.tsv`); the verdicts input pin is refreshed in `manifest.json`.

## Retail corrections

Each has evidence, a test and a `docs/vtmb` record (`decisions-step3.json` `retail_corrections`):

- **Zombie 510** `0x103e1080`: the retail gates, the row-3 distance (250, inclusive) and the
  `CAI_BaseNPC` tail `0x1027a530`.
- **Slot 482**: the four species copies are standalone and keep 1/2 in the SCRIPT state.
- **Yukie 600/601/602**: the ported bodies are dispatched.
- **Slot 465**: MingXiao, SabbatGunman and Werewolf `OnChangeActivity` now run.
- **Vocalisation rows**: a camera is silent, and the Tzimisce and Sabbat leader rows take effect.
- **Slot 561**: `GatherEnemyConditions` `0x10270b20` dispatches through `vtable+0x8c4`, so Bach's
  `0x10363db0` block runs.
- **Slot 400**: only the runner `0x103c3060` bypasses knockback eligibility, read through the vtable.

## Gate

- **Build:** green.
- **Runtime suites:** 1,264 Substrate (1,262 + `NpcKernelAnim10.RunnerKnockbackBypass` +
  `NpcKernelSpecies.WiredYukieMelee`), 14 Content and 1 PlayerWorld, zero failures. The focused
  Bach 606 (`WiredSlot606`, `BachGates`), Tzimisce 593 (`WiredSlot593`), Zombie 510 (`Zombie`,
  `WiredSlot510ShouldPlayFloatSound`) and slot-482 (`WiredSlot482CanPlaySequence`,
  `CanPlaySequence`) cases pass.
- **Regression comparison:** `test_delta` against step 2's final reports passes with 22 reviewed
  expectations: 2 additions and 20 diagnostics. The diagnostics are "no eye offset" lines from cases
  now standing their subject through the retail factory, the known zombie `Hide` `0x1009d2a0` stub
  on spawn, the rat's and combatant's facing stubs (`0x10331a40`, `0x10331cb0`) now reached through
  the vtable, and map-epoch shifts. No test was removed or renamed.
- **Tooling checks:** five generator `--check`s, pytest (120), `kernel_migration --check
  step0/step1/step2/factories/step3`.
- **Map smoke** (`step3/smoke.json`): `sp_giovanni_2b` (maker zombies, cameras, Sabbat gunman),
  `ch_fishmarket_1` (Hengeyokai, Yukie, cameras), `hw_warrens_4` (Tzimisce, runners, head claws,
  rats), `sm_pawnshop_1` (Bach, Werewolf, CameraSecurity through the retail script path) and
  `sp_tutorial_1`. Nothing logged an Elysium error; the only stubs are the ones step 2 recorded.
- **Not exercised in game:** no combat was driven, so the 561 gather block, the 510 ALERT float
  sound and the 400 knockback bypass ran only in the automation cases. Bach and the Werewolf are
  placed only in unbaked maps, so they stood through the script path.

Record fixes during 3j: 17 matrix rows named the first method of a slot family (`Slot599` for
600–602, `Slot21` for 22/23, `Slot25` for 26, `Slot497` for 506) and now name their own slot's
override; the step-5 rename list lost a duplicate row; `retail_corrections` name exact tests.

Disposition: accepted with step 3 (see `progress.md`).
