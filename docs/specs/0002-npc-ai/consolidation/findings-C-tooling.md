# Findings C — the tooling, audited for time (2026-09-30)

**The "15 minutes" is already fixed on main.** `937d975d` (09-28 08:37) took `kernel_ledger` from 470 s to 7 s and the full gate from about 15 min to 20 s; `b87058da` fixed `kernel_gate`. Worktrees that had not rebased kept paying for it: on 09-28, `elysium-int-3` and `elysium-lane-C1` ran `kernel_ledger` 19 times at 365–427 s each, and `int-2` ran `kernel_gate --family StartTask19` about 12 times at 176–229 s each.

The time lost now is in build/test contention and editor boots, not in Python. Source: 6,520 run journals in `$ELYSIUM_WORK_ROOT/logs/*.json` over 7 days (09-23 → 09-30), plus timings taken today on an idle machine. Scratch scripts: `$ELYSIUM_WORK_ROOT/scratch/brief-c/`.

| command | what it does | inputs read | time (measured) | where the time goes | fix | after |
|---|---|---|---|---|---|---|
| `research kernel --check` | 7 generators in `--check` mode on shared in-process builds | `corpus.sqlite` 342 MB, `listing.sqlite` 91 MB, `Source/` 1,596 files / 65 MB, SDK 2013 2,388 headers / 64 MB, overlays 3.5k rows | **20.3 s** (21.2 s wall); journal median 20.6 s | ledger 7.7 s (load 4.0, citations 2.0, render 1.2, directions 0.6); shape 10.7 s (`Sdk()` 2.6, `rows` 4.1, gather 1.5, signatures 1.3) | memoize `sdk_members`; pickle the corpus stage and the SDK index; per-file citation cache; skip the render when the input hash is unchanged | ~5 s; <1 s when nothing changed |
| `kernel_ledger --check` (alone) | the 13 tables | same | **8.0 s** | the same build, not shared | the same cache | ~2 s |
| `kernel_lists --check` (alone) | 2 lists | same | **6.8 s** | a full ledger rebuild; its own work 0.0 s | the same cache | ~1.5 s |
| `kernel_shape --check` (alone) | layout and signatures | + SDK | **17.1 s** | ledger 6.2 s; `sdk_members` re-reads and re-preprocesses a header on each of 272 calls (3.5 s); `_strip` is a char-by-char loop (36.8M `startswith` calls) | `functools.cache` on `kernel_shape.py:243`: **rows 4.10 s → 0.57 s, identical output (measured)**; replace `_strip` (`sdk_layout.py:97`) with a regex | ~4 s |
| `kernel_gate --all` | 7 checks × 13 families | + `git diff` | **21.4 s** | residue 17.9 s (a full `kernel_shape --unported` build); index 1.5 s | the same caches | ~5 s |
| `kernel_ledger --check --reach` ×2 maps | the reach cut | + 2 `entities.glb`, schedules | **8.5 s** | the reach itself 0.3 s | — | — |
| `pytest pipeline/tests` | 4,417 tests | 77 files read `$ELYSIUM_WORK_ROOT` (`exports_v2` 17 GB) | **≈386 s summed** (6 parallel chunks) | 56 tests over 1 s account for 311 s (80%); corpus-reading files 308 s | `corpus` marker, off by default; `pytest-xdist`; one session-scoped kernel fixture | ~76 s serial, ~15 s with `-n 8`; full suite ~60 s with `-n 8` |
| `elysium test <prefix>` | editor boot, then `Automation RunTest` | built DLLs | **19 s median wall**; the tests 0.1–0.7 s | boot and shutdown 18.8 s median (p10 17.5, p90 47.3; n=536) | batch prefixes into one boot (§1) | 19 s per batch instead of per prefix |
| `build` / `test` refused by the lease | — | — | 0 s each | **2,470 refusals per week** (1,645 build, 825 test), so agents poll | key the lease per checkout, add `--wait` (§1) | — |
| CLI startup | — | — | 0.41 s (0.18 s import) | — | none | — |

## 1. The automation-test path

- **How it launches** (`unreal.py:919-970`, `_run` at `:123`): `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTest <filter>;Quit" -ReportExportPath=… -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput`, plus `-NoLiveCoding -noP4 -nosound -shaderworkingdir=…`. Not a commandlet, not cooked, not `-game`: a full editor-engine init with no RHI. No cook step, so any subset runs against the last `build`.
- **Where the time goes.** Boot is about 96% of every run. In 7 days: 1,528 invocations — 536 green, 161 editor non-zero exits (real failures, 2.2 h), 1 watchdog kill, 825 instant refusals.
- **Bug: several prefixes in one filter.** UE accepts a `+`-joined filter (run `20260928T135019` completed 273 tests that way), but the pipeline builds the report slug from the whole filter (`unreal.py:944`): a 310-character report path, over `MAX_PATH`, `index.json` never written, "matched no test". **Fix:** cap the slug at ~40 chars + 8-hex hash; accept `elysium test A B C` as one boot. The top filters (`Npc` 204, `StartTask19` 136, `Schedule` 128, `Select19` 95, `Spawn19` 88 runs/week) run one after another today.
- **The lease.** `test` and `build` take `WorkspaceLease(export_root)` without blocking (`cli.py:749-757`, `workspace_lock.py:153-189`). All five checkouts share `E:\elysium-work\exports`, so one checkout's build refuses every other checkout's test; a `run play` holds the lease for its whole session (median 410 s, max 2 h). **Fix:** key build/test/run-play on `repo_root`; keep the export-root lease only for commands that write exports; add `--wait`.
- **Tests through the game MCP.** Not possible headless: `-unattended` suppresses the server (`ElysiumMcpSubsystem.cpp:23-28`). In a live `run play` session `elysium_console_exec "Automation RunTests …"` is plausible (199 of 201 flag constants are `ApplicationContextMask`; ~40 inline registrations are `EditorContext`-only). Unverified: the `AutomationController` exec handler under `-game`, and result delivery (poll `log_tail`). The gain is small: a code change forces a build and relaunch anyway.

## 2. The kernel ledger

- **What `--check` redoes every time:** opens both SQLite files (~16k queries), walks 5,187 functions and 17,596 edges, scans 1,632 source files for citations, renders 13 files, byte-compares. Nothing is cached across processes; inside `kernel` the three builds are memoized (`kernel.py:61-69`).
- **Caching on input hashes: yes.** The corpus stage (`load` + `walk` + `directions`, 4.6 s) pickles to 50 MB and reloads in **0.22 s (measured)** — first convert `fields` / `species_fields` from `sqlite3.Row` to `dict`; key on the SQLite files, the datamap JSON, the overlays and the driver code hash. Pickle the SDK index. Cache citations per `(path, mtime, size)`. Memoize the verdict on the hash of all stamps, so an unchanged tree answers `--check` in under a second.
- **One parse:** `kernel_lists` and `kernel_shape` already share inside `kernel`; standalone each rebuilds, and agents run them standalone ~600 times a week. The disk cache makes separate processes share. Point agents at `kernel --check` (`gen_kernel_bindings --check` is 17 s standalone, 0.0 s inside `kernel`).
- **Drift:** `--reach` is not in the gate; the committed `reach/*.md` / `.tsv` are out of date against the tree today.
- **Noise:** `sdk_layout._condition`'s `eval` prints a `SyntaxWarning` on every run.

## 3. `pipeline/tests`

- **Shape:** 4,417 tests (collected in 1.55 s); no conftest, markers or xdist. **One failure on main:** `test_oracle_citations` — two briefs cited `npc-kernel/population.md` and `npc-ai/index.md` under `docs/vtmb`, neither of which exists. They meant `docs/vtmb/npc-ai/population.md` (the authored population census) and `docs/vtmb/npc-kernel/index.md` (the address-to-section map), and are corrected (CLAUDE.md also points at the missing `npc-ai/index.md`).
- **The 20 slowest** (seconds, 6-way parallel): `map_ai_infra::every_exported_map_stages` 44.6; `gen_kernel_shape::census_carries_the_shape_29b` 21.6; `kernel_shape::npc_state_and_schedule_state_types` 20.4; `map_weather_stage` bounds 20.4 / rain cover 19.9 / footprint 19.5 / only-rain-map 3.0; `sounds_bake::every_corpus_key_addresses_its_own_asset` 19.2; `map_decals` places `[sm_hub_1]` 13.8 / rows 13.3 / places `[sp_tutorial_1]` 7.6 / rows 6.8; `kernel_ledger::clear_schedule_has_twelve_callers` 7.8; `sound_corpus_import::lip_tree_matches_legacy_mirror` 6.8; `bake_map_sprites::every_env_sprite` 4.1 / 3.2; `scene_mirror_retirement` 3.9; `map_geometry` 3.4 / 3.2; `map_light_query::tutorial_v2_gameplay_light_inputs` 2.9.
- **Opt-in (`-m corpus`):** every test above plus the `*_working_corpus` / `*_exported_corpus` cases in `test_map_geometry`, `test_map_places`, `test_importers_models`. Kernel oracle tests build the ledger three times (one per module fixture); a session-scoped fixture cuts ~50 s to ~17 s.

## 4. MCP response sizes

| tool | default size | what makes it big | proposal |
|---|---|---|---|
| `vtmb_asm` (StartTask) | **148 KB / 42k tok** | no cap (`corpus.py:1509`) | `max_lines=400` default; `from=` / `to=` |
| `vtmb_closure` (CAI_BaseNPC) | **128 KB / 37k** | every field, method, string (`corpus.py:2460`) | `sections=`; `brief` |
| `vtmb_code` | 60 KB / 17k | `CODE_LIMIT` 60k × up to 3 rows | 20 KB cap; `from_line=` / `lines=` |
| `vtmb_vtable` / `vtmb_fields` (CAI_BaseNPC) | 58 / 55 KB | 591–625 slots, 620 fields | `slots=430-450`, `overridden_only`; `range=`, `name=` |
| `vtmb_slot` 1 | 39 KB / 11k | 1,742 classes | `limit=50` |
| `elysium_entity_get` (NPC) | **~40 KB per entity; a classname fans out to 10 (~400 KB)** | ~430 accessor fields (`ElysiumMcpTools.cpp:560-580`), `described` repeats `value` | `fields=`, `sections=`, `brief`, `nondefault_only`; drop `described`; `limit=1` on a classname |
| `elysium_wire_report` | ~40 KB | 100 rows × 22 fields | `limit=25`; `fields=` |
| `elysium_console_exec` | unbounded | the log-tap delta, up to 2,000 lines | `max_lines=200`; 300-char lines |
| `elysium_g_dump` / `entity_list` / `log_tail` | 34 / 20 / 15 KB | — | `keys=` / `sections=`; `limit=25`; `grep=` |

Doubled payload: every game-tool result is sent as text and as `structuredContent` (UE plugin `ModelContextProtocolToolResults.cpp:183-194`). A failing `research` tool is echoed twice by the CLI (for `kernel_gate --all`, 2 × 1,100 lines).

## Top five, ranked by hours saved per week

1. **Per-checkout lease plus `--wait`:** ~8–15 h of agent time (2,470 refusal-and-poll turns a week; five checkouts serialize on one lock).
2. **Kernel caches:** ~3 h (~1,000 kernel-tool runs a week at 15 → 3 s).
3. **Test batching** (the slug cap, several prefixes per boot): ~1.5 h.
4. **pytest `corpus` marker, xdist, the session fixture:** ~1.5–2 h (6.4 min → ~15 s by default).
5. **MCP caps** (`vtmb_asm`, `vtmb_closure`, `entity_get fields=` / `limit=1`): ~1 h; the saving is context — one classname `entity_get` is ~110k tokens and forces a compaction.
