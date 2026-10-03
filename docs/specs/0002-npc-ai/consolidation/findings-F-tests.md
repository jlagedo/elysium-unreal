# Findings F: the whole test corpus (C++ and Python), censused and cut (2026-10-03)

- **Scope:** all 243 files in `Source/ElysiumUE/Private/Tests/` (1,778 tests) and all of `uv run pytest`'s `testpaths`: `pipeline/tests/` 210 files (4,417) plus `tools/elysium_glb_review/tests/` 10 files (183).
- **Data:** `findings-F-tests.tsv` (463 rows, one per file). Per-test categories, `per_test_categories.tsv` (6,378 rows), and every script are in `E:\elysium-work\scratch\findings-f\`. Nothing was edited, built or booted.
- **Method:** a brace-matching parse of every `IMPLEMENT_*_AUTOMATION_TEST` body (categories by pattern, reusing A's: ±10-15% at the arm/unit line); durations from the newest reports (2026-09-30 07:25; source test names match the Substrate report exactly, 1,713 + 9 Mover cases); Python timed fresh on 143 files and from findings-C's 09-30 per-test log for its 77 corpus files, plus an `open`/`scandir`/`sqlite3.connect` audit hook per test.

## 1. Totals today

| | files | tests | lines | summed test time | wall of the default run |
|---|---|---|---|---|---|
| C++, `uv run elysium test` (filter `Elysium.`) | 243 (19 support) | 1,778 (1,786 cases) | 175,605; ~25,100 assertions | 87.7 s | 121 s modelled; **122 s measured median** (33 green runs) |
| Python, `uv run pytest` | 220 | 4,600 | 71,322 | 384.0 s | serial 384 s; 8 workers about 48 s (xdist not installed; one test is 44.6 s) |

C++ wall = editor boot and shutdown 19 s (median of 536) + 8 ms framework cost per test (14 s) + test bodies 87.7 s. Test bodies are 72% of it, and **one test is 50.6 s of that**.

## 2. C++ default-run budget by top-level prefix

| prefix | tests | cases | seconds | note |
|---|---|---|---|---|
| `Elysium.Substrate` | 1,714 | 1,722 | 79.6 | `MapActorTeardown` 50.6 + `SkeletalStageOwners` 12.2; the other 1,720 take 16.8 |
| `Elysium.Content` | 33 | 33 | 7.8 | baked content; hub/tutorial maps and nav |
| `Elysium.Visual` | 10 | 10 | 0.14 | NPC body move facts, motor seam |
| `Elysium.Map` | 5 | 5 | 0.03 | |
| `Elysium.Policy` / `UI` / `Session` / `Audio` / `PlayerWorld` | 6 / 6 / 2 / 1 / 1 | 16 | not measured (no report; counted 0.01 s each) | Policy needs a generated `/Game` package |
| editor boot + shutdown | | | 19 | 96% of any small prefix run |
| framework cost, 8 ms per test | | | 14 | |
| **total** | **1,778** | **1,786** | **121** | |

Of 1,762 reported tests, 1,241 pass "with warnings" (28,102 warning entries in the Substrate report). Eight messages are 70% of them: 6,573 `npc_VHumanCombatant has no eye offset in .qc!`, 2 x 3,180 `FollowerDistance... overlap`, 3,681 `Stub fired`, 1,013 occluded-reaction percentages, 1,003 squad, 1,001 model admission. The "with warnings" status carries no signal.

## 3. C++ categories (A's, extended to the whole corpus)

| category | cases | seconds | what it is | A | B |
|---|---|---|---|---|---|
| census | 32 | 0.3 | generated shape / bindings / override / factory / tunable / hull / id-space tables | keep | keep |
| arm | 988 | 8.6 | cites a retail address, asserts what one function writes | keep | tier 2 |
| unit | 484 | 4.8 | port rules with no address (camera, dialogue, terminal, melee, inventory, NPC rules that cite prose) | keep | tier 2 |
| scenario | 104 | 1.0 | ticks a world with awake entities (96 tests; 16 of them in NPC scope) | keep | **default** |
| infra | 61 | 51.4 | UWorld / editor-asset tooling, no corpus read (`MapActorTeardown` is 50.6) | keep, but `MapActorTeardown` out | tier 2 |
| content | 54 | 20.8 | reads baked maps / assets / `/Game` packages (33 `Elysium.Content`, 6 Policy, 1 PlayerWorld, 14 Substrate) | opt-in | opt-in |
| port-only | 21 | 0.3 | body-owner arbiter (16 of A's), ambient / external executor reads, dead `PlayActivity` | **delete** | delete |
| seam | 16 | 0.1 | at least 20% of the assertions say "seam", "unrecovered", "no source" | **delete** | delete |
| stub-count | 13 | 0.1 | assert the `ElysiumStub` tally (7 NPC, 6 in EntityIO / Discipline / WorldEffects / DialogueEntry) | **delete** | delete |
| duplicate | 13 | 0.1 | every address it asserts is owned by a better file (older suites vs the `NpcKernel*` family) | **delete** | delete |

- **Seam lines inside arms:** 113 tests carry a seam assertion (93 in NPC files; A counted 75 with a narrower rule); 130 seam lines sit in the 97 that are not seam tests. Delete the lines, not those tests.
- **Overlap:** 88 addresses are asserted in 2+ files (17 in 3+); 38 more arm tests overlap partly and stay.
- **By family** (file name; cases / seconds): npc-kernel 956 / 8.2 (800 arm, 25 census, **6 scenario**); npc-world 282 / 3.5; entity-substrate 184 / 1.9 (52 scenario); map-world 105 / 58.5; bake-assets 67 / 13.5; combat-player 67; dialogue 34; ui 26; camera 20; terminal 18; other 21; save 6.
- **The boundary that broke:** 59 of the 62 `NpcKernel*` files hold no scenario test. `Elysium.Arena` has no test in the tree. `DO_INTEREST_ACTIVITY` appears in one file (`NpcKernelMisc2`). Neither proposal below adds this coverage; the arena scenarios do.

## 4. Python categories and the work root

| category | tests | seconds | what it is |
|---|---|---|---|
| corpus | 150 | 326.1 | opens files under `$ELYSIUM_WORK_ROOT` / the VtMB install (35 files) |
| pipeline-unit | 4,249 | 46.8 | decoder, importer, stager, CLI logic on synthetic input |
| census | 67 | 4.7 | byte-for-byte / golden / `--check` against a committed table or a second run |
| scenario | 39 | 2.4 | drives the CLI or a whole command through a temp tree |
| arm | 81 | 0.4 | cites a retail address (kernel-ledger facts) |
| doc-lint | 14 | 3.5 | reads `docs/` (citations, contracts) |

| group | tests | seconds | corpus-reading tests | their seconds |
|---|---|---|---|---|
| map staging / bake / nav | 387 | 225.6 | 79 | 224.9 |
| kernel ledger, generators, oracle | 206 | 56.6 | 22 | 51.5 |
| other decoders | 424 | 26.7 | 3 | 23.3 |
| importers | 287 | 19.3 | 6 | 11.3 |
| materials / textures / lookdev | 387 | 17.0 | 36 | 11.9 |
| glb formats | 1,600 | 15.3 | 1 | 0 |
| cli / orchestration / contracts | 425 | 11.6 | 2 | 3.2 |
| model / character / rig decoders | 701 | 11.1 | 1 | 0 |
| glb review tool | 183 | 0.8 | 0 | 0 |

- **Who reads `$ELYSIUM_WORK_ROOT`:** the audit finds **150 tests in 35 files**. A grep finds the name in 92 files (C: 77): the rest point it at a temp dir. Biggest: `map_decals` 12 tests / 49.0 s, `map_weather_stage` 7 / 66.0, `map_ai_infra` 6 / 50.3, `map_geometry` 21 / 26.6, `gen_kernel_shape` 5 / 21.7, `kernel_shape` 4 / 20.4, `sounds_bake` 1 / 19.2, `kernel_ledger` 11 / 9.3, `lookdev_set` 23 / 8.4. The audit is a lower bound (stat, memory maps and native readers are not hooked). The 75 tests over 0.3 s in C's corpus files were not re-run; 7 of 7 sampled confirmed.
- **Census-like by file:** `model_catalogues_stage` 9, `vdata_glb` 6, `corpus_index_glb` 5, `vdata_corpus_import` 4, then 1-3 each across 20 more files.
- **Time:** 54 tests over 1 s are 306 s (80%). Count is not the cost: the other 4,450 tests take 57.9 s serial (13 ms each).
- **Failing on main now:** `test_oracle_citations::test_source_path_cites_name_existing_oracle_files` (two briefs cite `npc-kernel/population.md` and `npc-ai/index.md`, both missing); `glb_review/test_core_seams::test_every_declared_family_admits_only_root_extensions` (`ai-schedules` declares `ELYSIUM_vtmb_ai_schedule`, absent from `ROOT_EXTENSIONS`). Both are doc/census drift.
- **Order hazard:** 12 test files run `os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())` at import. `test_gen_contents_signatures::...as_the_probe_does` fails when run in one process after them and passes alone (20/20). Fix before adopting xdist.

## 5. The two proposals

| | today | A | B |
|---|---|---|---|
| C++ default: tests (cases) | 1,778 (1,786) | 1,660 (1,668) | **162 (170)**: 32 census + 96 scenario + 34 smoke |
| C++ test-body seconds | 87.7 | 15.6 | 1.7 |
| C++ wall (19 s boot + 8 ms/test) | 121 | **48** (98 if `MapActorTeardown` stays) | **22** |
| C++ out of the default | | 63 deleted, 55 opt-in (content 54, `MapActorTeardown`) | the same, plus 1,498 in a story-close tier (13.9 s of bodies; full run 48 s) |
| Python default: tests | 4,600 | 4,450 | **160**: 67 census + 39 scenario + 14 doc-lint + 40 smoke |
| Python serial / 8 workers | 384 s / ~48 s | 58 s / ~10 s | 11 s / ~4 s |
| Python out of the default | | 150 corpus tests (326 s) behind `-m corpus` | the same, plus 4,290 units in the tier (46.7 s) |
| Both, tests | 6,386 | 6,118 | **330** |
| Both, serial / C++ and Python concurrent | 505 s / 121 s | 108 s / 48 s | 35 s / 22 s |

- **A is a time cut, not a count cut:** 6,386 to 6,118 tests, 505 s to 108 s. Almost all of the 397 s saved is the 205 tests moved to opt-in (71 s C++, 326 s Python); the 63 deleted tests save about 1 s.
- **B is the count cut:** 95% of tests leave the default. Its floor is the editor boot: 19 of the 22 C++ seconds. For Python it saves 47 s serial but only about 6 s on 8 workers over A, and costs 4,290 decoder-logic tests, so it does not pay; keep the Python units in the default and take only A's cut.
- **Modelled, not run:** every A / B time. Counts are exact for the stated rules. The smoke set is a mechanical pick (largest assertion count, three per family, six for npc-kernel; names in `cpp_summary.json`), to be replaced by hand.
- **Mechanism, unverified:** tag the default tier `SmokeFilter` and run `Automation RunFilter Smoke`; keep `ProductFilter` for the full tier. Python: a `corpus` marker off by default.

## 6. What the default run no longer catches

**A**
- **Port-only (21):** the body-owner arbiter's claims, the ambient / external-executor reads, `PlayActivity`. These pin machinery V3 deletes; `NpcCombat.Chase` and `AiScriptedSchedule.MoveToGoal` assert that the blocking owner is correct.
- **Stub-count (13):** a stub that stops reporting, or an input that silently fires nothing (EntityIO stub inputs, stub-class spawn, the chain-slot tally).
- **Seam (16 whole tests, 130 lines):** a seam that starts answering something else (squad, sequence lookup -1, ConVars, sector, attack coordinator, the weapon capability word). The ledger's seam list is the only record.
- **Duplicate (13):** nothing; the owning file asserts the same address.
- **Content, opt-in (55):** baked-map parity (hub / tutorial collision, hint lists, claims and cooldowns, places and crosswalk pairs, nav areas and jump links, `CreatePhysicsMeshes`), UI strings and art, Policy material masters, input assets; the `EndPlay` crash guard on a map actor's owned query proxy (`MapActorTeardown`); shared-rig owner builds (`SkeletalStageOwners`).
- **Python corpus, opt-in (150):** every decoder-vs-real-corpus claim: map staging on the real maps (decals, weather, AI infra, geometry, ropes), the kernel-ledger and shape oracle counts, sound-bake key addressing, lookdev sets.

**B, additionally**
- **NPC arms (889 kernel + 233 NPC-world tests):** one function writing the wrong field, or two arms in the wrong order, is found at story close. The default holds 25 kernel census tests (shape, bindings, overrides: they catch an arm removed or renamed, not one that is wrong), 16 NPC-scope scenarios, and 9 NPC smoke tests.
- **Player and world rules (371 tests):** combat, camera, dialogue, terminal, inventory, movement, melee, saves, except 25 smoke tests and the ~80 non-NPC scenarios that touch them.
- **Python:** every decoder, importer and stager unit (4,290), if the Python half of B is taken.
- **Still uncaught in both:** the path-task-then-activity defect, and any bug in the kernel that no scenario crosses.

## 7. Cheaper than either, any order

1. `MapActorTeardown`: 50.6 s, 58% of test time, 15 reports in a row (48-89 s). One test; find where it spends the time. Alone it takes the default from 121 s to 70 s.
2. Silence the fixture NPC warnings (8 messages are 70% of 28,102 entries); the status then means something again.
3. A `corpus` marker for Python: 384 s to 58 s with no test deleted.

## Not determined

No `Elysium.` run report exists (only per-prefix reports, 09-30; 13 test files changed in `9a1bec0d` after them, test names unchanged). Policy / UI / Session / Audio / PlayerWorld durations. xdist timings (not installed). The arm/unit line and the scenario signal are pattern reads; whether any scenario crosses a path-then-activity boundary was not re-verified beyond a grep.
