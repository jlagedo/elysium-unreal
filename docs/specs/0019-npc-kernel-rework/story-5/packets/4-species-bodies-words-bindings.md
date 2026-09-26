# Step 4, packets 4b–4i — species bodies, words and bindings

Start: `0019-5-class-tree`, `9f087e42` (4a committed), manifest `step-3-species-dispatch`.
Executor: Claude Code, Opus 5.5, one integration owner across several sessions; patch checkpoints
under the evidence root (`step4/checkpoints/4b-done` … `4h-green`).
Outcome: every introduced species' bodies, words and datamap bindings live on its own class
(`Substrate/ElysiumNpc<X>.h/.cpp`). Deferred classes (steps 7-10) keep their bodies and words on
`FElysiumNpc` in the listed homes, and Troika/base bodies do not move.
Record: [moves-step4.tsv](../moves-step4.tsv), [fields-step4.tsv](../fields-step4.tsv),
[decisions-step4.json](../decisions-step4.json), [expectations/step-4.json](../expectations/step-4.json),
[acceptance-step4.json](../acceptance-step4.json).
Evidence root: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/step4/`.

| Packet | Result |
|---|---|
| 4b | class-qualified species shape map; 27 species binding classes generated onto the abstract descriptors; 15 pending species inputs; duplicate-field guard ([4b record](4b-generator-and-bindings.md)) |
| 4c | 11 merged carriers folded into their survivors; the relocation tool ([4c record](4c-carriers-and-relocation.md)) |
| 4d–4g | bodies, words and file-scope locals relocated per branch (Animal/Camera/misc, BaseBoss, Human, Vampire); forwarding overrides collapsed; adapters and gates kept as own helpers; the three split carriers split; `AsSpecies<T>()`; `FElysiumNpc`'s private section protected |
| tables | class-keyed tables became override bodies (slots 337, 546, 566, 408, 461, 292, 366, `SetupJump`); four record-only tables deleted (615, 141, 516, 68/69); six survivors listed in `decisions-step4.json`; 119 empty banners and three emptied kernel files removed |
| 4h | `Elysium.Substrate.NpcKernelSpeciesBindings` (Counts, Chain, MergedAndSplitCarriers, SaveRoundTrip, TutorialRat); `NpcKernelSpecies.Zombie` moved onto a real zombie |
| 4i | 283 overlay targets retargeted, verdicts pin refreshed, ledger regenerated; oracle updates; decisions folded; the two unlisted words moved; full gate, delta, smoke, acceptance |

## Records

- **Moves:** 1,504 rows: 680 `move`, 256 `collapse` (32 `collapse:table`, 11 `collapse:merge`),
  544 `stay` (249 Troika API, 152 virtual surface, 104 Troika body, 20 data-query table, 13 shared
  helper, 6 Troika reader), 24 `deferred`. The checker finds each moved member declared on its
  owner's header and defined in its `.cpp`, and each collapsed or moved member absent from
  `FElysiumNpc`.
- **Bindings:** 248 datamap records: 158 bind, 12 declare, 8 shadow, 44 absent (each with the map's
  reason), 15 input seams, 11 outputs.
- **Placement rules added in execution:** base seams stay; adapters and gates stay own helpers;
  typed species access; protected Troika state; splits land with the last reader; record-only
  tables are deleted (`decisions-step4.json` `rules`).

## Retail corrections

- **MingXiao proxy gate** `0x10397b40` resolves through `m_rhSeveredTentacles` with the tentacle's own
  `m_iTentacleID`; `0x10397a50` takes a proxy (`lifecycle.md`).
- **Werewolf `TaskFail`** `0x103ce750` calls its own `CheckStuck` `0x103cb920` (`shape.md`).
- **Scurrying flee march** `0x103acba0`: horizontal march, allsolid/startsolid blocking, plane-normal
  deflection, bare node after five refusals, y-then-x jitter (`senses.md`). Detection `0x103acac0`
  arm citations corrected.
- **Rat keys effective:** the tutorial rats' eight keys land through `Construct` (confirmed in game).
  Their callers stay story-8 residue, so no in-game detection yet.
- **Merged and split carriers:** each retail word has one port member per declaring class
  (`fields-step4.tsv`, [4c record](4c-carriers-and-relocation.md)).

## Gate

Build green; `Elysium.Substrate` 1,270, `Elysium.Content` 14, `Elysium.PlayerWorld` 1, zero failures.
`test_delta` against step 3's final gate passes with 107 reviewed expectations: 6 additions and 101
diagnostics. The diagnostics are the spawn "no eye offset" warning naming the real species the
re-pinned cases stand, the known zombie `Hide` stub, `TaskFail` reasons from the species' own
overrides, stubs now on the real instance, and 8 map-epoch shifts. Cheap checks: five generator
`--check`s, pytest (142), `kernel_migration --check step0/1/2/factories/step3`, and
`--check step4` = PASS. Map smoke over `sp_tutorial_1` (rats' bound keys read back),
`ch_fishmarket_1` (Hengeyokai `StartTransformation` input), `sp_giovanni_2b`, `hw_warrens_4` and
`sm_pawnshop_1`: no Elysium errors. Combat was not driven in game.

## Carried forward

- The Scurrying callers `0x103ac500` / `0x103ac740` (story 8 pass I).
- Deferred homes on `FElysiumNpc` until steps 7-10 (controller line, makers, cine, TestHull), and the
  surviving tables.
- Step 5 renames the two `Base*` helpers.
