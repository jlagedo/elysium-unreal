# Findings E — where the time went, measured (2026-09-19 → 09-30)

Sources: 579 Claude Code transcript files, main + subagent (58,994 tool calls, 73% from subagents; duration = result ts − use ts, ±1 s) and 7,612 run journals in `$ELYSIUM_WORK_ROOT/logs` (exact). No transcript or journal exists for 10-01..10-02.
Wall h = summed foreground waits in agent-hours (parallel agents overlap; section 2 gives wall-clock). `run_in_background` calls (1,160) are excluded; 138 foreground calls that hit the 120/600 s tool timeout are counted at their wait (17.0 h). Full per-family rows: `findings-E-time.tsv`.
**Result: the corpus MCP is 0.18 h of 90.7 h. The waiting is build, test, polling and the 1 s Bash call floor; `kernel_*` is 10%, and 5.4 of its 14.6 journal machine-hours are one function chain (`kernel_shape.build`).**

## 1. Tool wait by family (agent-hours)
| family | n | wall h | % of wait | med s | p90 s | max s | >10 s | >60 s |
|---|---|---|---|---|---|---|---|---|
| Bash build: elysium build +wrappers | 632 | 20.5 | 23% | 109.59 | 426 | 602 | 416 | 274 |
| Bash poll: until/while+sleep polling | 696 | 18.9 | 21% | 13.82 | 483 | 603 | 344 | 185 |
| Bash test: elysium test +wrappers | 405 | 11.0 | 12% | 62.15 | 200 | 602 | 348 | 184 |
| Bash text tools: rg/grep/sed/cat/ls over source and other | 22163 | 10.0 | 11% | 0.95 | 2 | 592 | 246 | 41 |
| Bash test: pytest | 472 | 7.0 | 8% | 7.63 | 182 | 602 | 209 | 118 |
| Bash ad-hoc python: python: other | 4648 | 4.3 | 5% | 1.17 | 2 | 505 | 154 | 40 |
| Bash ad-hoc python: python: docs/tsv | 1851 | 3.9 | 4% | 1.19 | 5 | 601 | 109 | 38 |
| Bash kernel tooling: kernel/gen --check loop | 80 | 2.5 | 3% | 78.90 | 200 | 494 | 77 | 56 |
| Bash kernel tooling: kernel_gate | 89 | 1.8 | 2% | 57.79 | 212 | 330 | 76 | 41 |
| Bash text tools: rg/grep/sed/cat/ls over docs, *.md, *.tsv | 4566 | 1.8 | 2% | 0.88 | 2 | 304 | 43 | 5 |
| Bash kernel tooling: kernel_migration | 59 | 1.3 | 1% | 38.18 | 197 | 488 | 42 | 25 |
| Bash kernel tooling: kernel_ledger | 64 | 1.2 | 1% | 33.64 | 121 | 601 | 52 | 16 |
| Bash pipeline: elysium bake/export/import | 92 | 0.7 | 1% | 17.42 | 76 | 601 | 43 | 8 |
| Bash research other: elysium research (non-kernel) | 446 | 0.7 | 1% | 1.89 | 7 | 601 | 34 | 3 |
| Bash kernel tooling: kernel (combined --check) | 61 | 0.6 | 1% | 28.66 | 62 | 85 | 58 | 12 |
| Bash kernel tooling: kernel_shape | 66 | 0.6 | 1% | 19.05 | 38 | 480 | 61 | 3 |
| Bash kernel tooling: gen_kernel_* (shape/bindings/tunables) | 117 | 0.6 | 1% | 18.66 | 25 | 96 | 86 | 1 |
| built-in: Grep | 2636 | 0.1 | 0% | 0.12 | 0 | 3 | 0 | 0 |
| built-in: Read | 4468 | 0.1 | 0% | 0.02 | 0 | 5 | 0 | 0 |
| built-in: Glob | 113 | 0.0 | 0% | 0.91 | 2 | 3 | 0 | 0 |
| vtmb-corpus MCP, all 14 tools (section 5) | 7639 | 0.2 | 0.2% | 0.01 | 0.2 | 38 | 1 | 0 |
| elysium game MCP, 20 tools (`status`: 76 of 313 calls took 10-63 s, game thread busy) | 1512 | 0.7 | 0.7% | 0.04 | 1.3 | 85 | 98 | 2 |
| AskUserQuestion + ExitPlanMode (human, excluded) | 67 | 5.7 |  | 122.73 | 614.7 | 3436 | 60 | 49 |

- Bash floor: 37408 Bash+PowerShell calls, p5 0.62 s, p50 1.03 s (= 6.4 h at p5) against Read 0.02 s and Grep 0.12 s per call. Here `bash -lc true` costs 0.35 s, `bash -c true` 0.03 s.
- Docs queries: 4566 text-tool calls over docs/*.md/*.tsv = 1.8 h, med 0.88 s, 43 over 10 s, 5 over 60 s. Python docs/tsv scripts: 1851 calls = 3.9 h, of which 53 calls ≥30 s = 2.9 h (agent-written `gate*.py checks` hitting the 600 s limit).
- Read of docs/vtmb: 227 reads, 167 of files over 200 KB (all 167 bounded by offset/limit, 0 errors, largest reply 40541 chars).
- Lease: 1659 build + 836 test invocations refused instantly (97% on 09-27); 94 build/test calls ran through retry wrappers = 9.2 h wall.

## 2. Per day: tool waiting vs everything else
| day | active wall h | tool-blocked wall h | share | tool agent-h | model agent-h | human h | bg-wait h | refused build/test |
|---|---|---|---|---|---|---|---|---|
| 09-19 | 9.3 | 1.0 | 11% | 1.0 | 12.5 | 3.6 | 6.2 | 0 / 0 |
| 09-20 | 15.6 | 5.4 | 35% | 5.6 | 7.6 | 3.4 | 3.9 | 8 / 3 |
| 09-21 | 18.8 | 6.8 | 36% | 7.6 | 14.7 | 2.9 | 1.9 | 1 / 4 |
| 09-22 | 4.7 | 1.8 | 37% | 1.8 | 2.0 | 0.9 | 0.8 | 1 / 0 |
| 09-23 | 5.6 | 0.4 | 7% | 0.4 | 3.2 | 2.6 | 0.8 | 0 / 0 |
| 09-24 | 3.8 | 1.6 | 42% | 1.6 | 1.4 | 0.5 | 1.8 | 0 / 0 |
| 09-25 | 6.4 | 3.1 | 48% | 3.2 | 4.6 | 0.9 | 0.7 | 1 / 0 |
| 09-26 | 16.7 | 7.9 | 47% | 8.2 | 6.9 | 2.3 | 1.5 | 2 / 2 |
| 09-27 | 24.0 | 14.2 | 59% | 37.5 | 33.8 | 1.0 | 16.6 | 1601 / 816 |
| 09-28 | 24.0 | 12.4 | 52% | 14.5 | 10.7 | 4.3 | 12.4 | 38 / 5 |
| 09-29 | 20.1 | 5.2 | 26% | 6.7 | 20.4 | 3.0 | 13.7 | 7 / 5 |
| 09-30 | 5.5 | 1.6 | 29% | 1.9 | 6.3 | 0.6 | 2.4 | 0 / 1 |
| total | 154.6 | 61.5 | 40% | 89.9 | 124.1 | 26.0 | 62.6 | 1659 / 836 |
Active = union over transcripts of event gaps ≤30 min; tool-blocked = union of foreground tool calls; bg-wait = main sessions idle for subagent/background notifications. Tool agent-h = 42% of tool+model agent-h.

## 3. `kernel_*` invocation shapes, top 15 by total time (run journals, exact)
| shape (script + flags) | runs | total h | median s | p90 s | max s |
|---|---|---|---|---|---|
| kernel_migration --check | 301 | 4.13 | 27.5 | 133.0 | 202 |
| kernel_ledger --check | 162 | 1.83 | 19.7 | 29.8 | 427 |
| kernel_ledger | 89 | 1.13 | 19.7 | 31.4 | 425 |
| kernel_gate --family | 25 | 0.89 | 146.2 | 168.3 | 211 |
| kernel_shape --check | 148 | 0.71 | 16.7 | 19.1 | 32 |
| gen_kernel_shape --check | 138 | 0.69 | 17.2 | 21.3 | 28 |
| kernel_gate --family --no-residue | 39 | 0.68 | 55.7 | 90.6 | 150 |
| kernel_gate --allow-hot --family --no-residue | 14 | 0.64 | 210.3 | 228.5 | 229 |
| gen_kernel_bindings --check | 126 | 0.63 | 17.0 | 20.4 | 37 |
| kernel | 45 | 0.41 | 39.4 | 47.2 | 57 |
| kernel_shape | 67 | 0.31 | 16.5 | 18.9 | 23 |
| kernel --check | 52 | 0.30 | 20.4 | 23.9 | 32 |
| gen_kernel_shape | 58 | 0.29 | 17.5 | 20.4 | 27 |
| kernel_shape --unported | 58 | 0.29 | 17.7 | 22.0 | 26 |
| kernel_lists --check | 152 | 0.28 | 6.3 | 7.8 | 16 |
| all 65 kernel*/gen_kernel* shapes | 1960 | 14.6 | | | |
Transcripts see 641 kernel-tooling calls = 9.1 agent-h (journals also hold Codex/opencode workers). Pre-fix runs: ledger 1.7 h in 19 runs >100 s (363 s median on 09-28, unrebased lanes; fixed by `937d975d`, 8 s on 09-29), gate 1.6 h in 32 runs >100 s (fixed by `b87058da`).

## 4. Where the top 5 shapes by journal hours spend it (one idle-machine run each at HEAD `9a1bec0d`; cProfile shares; imports 0.07-0.12 s)
| shape | what it does | wall now | where the time goes | dominant functions |
|---|---|---|---|---|
| `kernel_migration --check stepN` (4.1 h, **deleted** in `7d819129`) | step receipts: re-reads and comment-masks all of `Source/ElysiumUE` and re-verifies steps 0..N-1 from historical trees (`git archive`+tar, 1.2 s each); steps 1 and 6 also build a Ledger | not reproducible (receipts moved; replay at `a00cd11b` refuses in 7 s) | static: step1 50 s, step2 58, step3 85, step4 115, step5 154, step6 175 (journal medians) = cost grows with N | `check_stepN` → earlier checkers; `mask_cpp` per file |
| `kernel_ledger --check` | 13 tables from corpus.sqlite 342 MB + listing.sqlite 91 MB + 1,639 source files | 8.2 s | load 46% (16,010 SQL queries), citation scan 29%, render 17%, directions 7% | `load` 5.2 s (`_load_family` 2.0, `_load_functions` 1.3, `_load_bases` 0.9), `_scan_citations` ×1,639 = 3.1 s, `render` 1.9 s |
| `kernel_gate --family StartTask19` | 7 mechanical checks of a port family | 21.7 s | residue 20.0 s = a full `kernel_shape --unported` build (92%); index 1.5 s; the 7 checks <0.2 s | `make_residue` → `write_unported` → `kernel_shape.build` |
| `kernel_shape --check` | field layout + signatures vs the 2013 SDK | 18.3 s | `sdk_layout.preprocess` 52% (1,992 calls), ledger build 20%, gather 7%, signatures 6% | `sdk_layout._strip` char loop (36.8M `startswith`), `sdk_members` 272 calls = 36% (`kernel_shape.py:243`, no cache) |
| `gen_kernel_shape --check` (+ `gen_kernel_bindings --check`, `kernel_story8_shape`) | census + bindings generators | 18.2 s (bindings 18.0 s; story8 19.7 s journal median, not profiled) | `kernel_shape.build` is 94% (95%) of the run | the same `_strip` / `sdk_members` chain; these shapes plus `kernel_shape`, `kernel` and un-flagged gate = 5.4 h (852 runs) |
Root cause unchanged since findings-C: `functools.cache` on `sdk_members` (4.1 s → 0.57 s measured there) and a regex for `_strip` are still not applied.

## 5. vtmb-corpus MCP: 7639 calls, 0.18 h total, 1 call over 10 s, none over 60 s
| tool | n | med s | p90 s | max s | reply chars med | reply chars max | reply chars total |
|---|---|---|---|---|---|---|---|
| vtmb_code | 3047 | 0.01 | 0.03 | 38.3 | 1050 | 43937 | 6630743 |
| vtmb_asm | 2012 | 0.01 | 0.03 | 3.1 | 923 | 48818 | 5470371 |
| vtmb_grep | 615 | 0.19 | 1.07 | 4.6 | 635 | 33810 | 966483 |
| vtmb_callers | 352 | 0.01 | 0.03 | 2.3 | 274 | 5417 | 257911 |
| vtmb_func | 320 | 0.01 | 0.03 | 1.8 | 127 | 1297 | 60231 |
| vtmb_globals | 317 | 0.02 | 0.05 | 1.7 | 314 | 3688 | 211791 |
| vtmb_fields | 314 | 0.01 | 0.26 | 1.6 | 112 | 17589 | 214165 |
| vtmb_string | 246 | 0.02 | 0.07 | 4.7 | 387 | 12886 | 249191 |
| vtmb_slot | 164 | 0.01 | 0.20 | 2.0 | 26853 | 42849 | 3890297 |
| 5 others (vtable, readers, callees, stat, iface) | 252 | ≤0.82 | ≤0.29 | ≤1.2 | ≤2259 | ≤47721 | 718598 |
Slowest 15 calls (s, day, tool, arguments); the 38 s call is a single outlier (next slowest overall 4.7 s), the rest are regex `vtmb_grep` at 2-5 s:
| s | day | tool | arguments |
|---|---|---|---|
| 38.3 | 09-28 | vtmb_code | `{"reference": "0x103692c0"}` |
| 4.7 | 09-29 | vtmb_string | `{"text": "worldspawn"}` |
| 4.6 | 09-29 | vtmb_grep | `{"pattern": "FCAP_MUST_SPAWN/& 1\\) != 0\\) \\{\\s*\\(\\*\\*\\(code \\` |
| 3.6 | 09-29 | vtmb_grep | `{"pattern": "thunk_FUN_102ee140/thunk_FUN_102ee620/thunk_FUN_102ee680"` |
| 3.4 | 09-29 | vtmb_grep | `{"pattern": "SetNPCTransparent/BlocksTraces/m_bBlocksTraces =/IsNPCTra` |
| 3.1 | 09-29 | vtmb_code | `{"reference": "102f0e80"}` |
| 3.1 | 09-29 | vtmb_asm | `{"reference": "102f06e0"}` |
| 3.0 | 09-29 | vtmb_grep | `{"pattern": "Node Graph/node_graph/NodeGraph", "limit": 10}` |
| 2.8 | 09-29 | vtmb_grep | `{"pattern": "TASK_SET_ROUTE_SEARCH_TIME/RouteSearchTime/RouteRebuild/P` |
| 2.6 | 09-30 | vtmb_grep | `{"pattern": "LAB_10085480/FUN_10085480/thunk_FUN_10085480", "limit": 1` |
| 2.4 | 09-26 | vtmb_grep | `{"pattern": "thunk_FUN_103cb920/CheckStuck\\("}` |
| 2.3 | 09-29 | vtmb_callers | `{"reference": "102e2d70"}` |
| 2.2 | 09-29 | vtmb_code | `{"reference": "CTraceFilterSimple::ShouldHitEntity"}` |
| 2.2 | 09-19 | vtmb_grep | `{"pattern": "thunk_FUN_10367680/thunk_FUN_103b2630/thunk_FUN_103c59d0/` |
| 2.2 | 09-19 | vtmb_grep | `{"pattern": ",\\*\\(\\w+ \\*\\)\\(\\w+ \\+ 0x2c\\),\\*\\(\\w+ \\*\\)\\` |

## 6. Tests per day (`elysium test` from journals; `pytest` from transcripts)
| day | etest runs | refused | etest wall h | group-level | narrow prefix | failed | pytest runs | pytest full | pytest wall h |
|---|---|---|---|---|---|---|---|---|---|
| 09-19 | 27 | 0 | 0.30 | 4 | 23 | 3 | 7 | 1 | 0.13 |
| 09-20 | 49 | 3 | 0.97 | 42 | 7 | 6 | 55 | 18 | 0.80 |
| 09-21 | 62 | 4 | 0.85 | 34 | 28 | 16 | 132 | 36 | 1.99 |
| 09-22 | 48 | 0 | 0.97 | 23 | 25 | 37 | 0 | 0 | 0.00 |
| 09-23 | 6 | 0 | 0.16 | 6 | 0 | 0 | 37 | 0 | 0.07 |
| 09-24 | 20 | 0 | 0.42 | 17 | 3 | 0 | 5 | 0 | 0.12 |
| 09-25 | 34 | 0 | 0.47 | 17 | 17 | 5 | 39 | 0 | 0.56 |
| 09-26 | 70 | 2 | 1.24 | 51 | 19 | 8 | 33 | 0 | 0.55 |
| 09-27 | 323 | 816 | 3.02 | 55 | 268 | 46 | 65 | 2 | 1.11 |
| 09-28 | 164 | 5 | 2.07 | 56 | 108 | 88 | 40 | 6 | 1.11 |
| 09-29 | 75 | 5 | 1.42 | 54 | 21 | 12 | 46 | 6 | 0.35 |
| 09-30 | 10 | 1 | 0.24 | 7 | 3 | 4 | 10 | 3 | 0.21 |
| total | 888 | 836 | 12.14 | 366 | 522 | 225 | 469 | 72 | 7.00 |
Filters: `Elysium.Substrate` 157 runs 5.9 h med 129 s; `substrate` 36 runs 1.3 h med 128 s; `Elysium.Content` 82 runs 0.8 h med 40 s; 148 narrow prefixes = 522 runs, 3.5 h, median 18.5 s, p10 17.2 s (boot-dominated). Only 1 run was the whole suite (`Elysium` ≈ 40 s) and 23 were `+`-joined groups; "group-level" = a whole top-level group (`Elysium.Substrate` ≈ 129 s). pytest "full" = no path filter: 72 runs, 3.0 h, median 245 s; 379 file-targeted runs have p90 110 s.

Caveats: Bash waits include any permission-prompt wait (not separable); explicit background tasks ran builds 16.1 h, tests 1.8 h, subagents 106.8 h outside these figures; Bash waits calibrate to journals at +1-2 s (kernel_shape 19.0 s vs 16.8 s). Not shown above (in the tsv): kernel_lists 40 calls 0.17 h, kernel_skeleton 26 / 0.04 h, kernel_packet 1 call, kernel_reach none.

## 7. The five biggest sinks (agent-hours, share of 90.7 h wait)
1. **`elysium build` 20.5 h** (632 calls; journals: 667 builds 20.4 h, median 46 s, p90 280 s, max 1456 s). Incremental C++ rebuilds behind one export-root lease shared by 5 checkouts (findings-C): 1659 refusals and retry wrappers.
2. **Polling loops 18.9 h** (696 calls, 52 hit the 600 s limit): `until grep -q …; do sleep N; done` on build/test logs (6.3 h), bg-task outputs (6.7 h), other logs (5.9 h); builds and tests have no blocking `--wait` (findings-C).
3. **Tests 18.0 h** (etest 11.0 + pytest 7.0): 366 whole-group runs = 8.7 h (`Elysium.Substrate` alone 193 runs, 7.2 h, ≈128 s each) and 522 narrow prefixes each paying an editor boot (≈18 s); 225 failing etest runs = 3.5 h; 72 full pytest runs of ≈245 s (findings-C: 77 test files read the 17 GB export tree).
4. **Text and Python one-liners 11.8 + 8.2 h** (33355 calls, median 0.9-1.2 s): per-call Bash floor, not query cost; 53 docs/tsv python calls ≥30 s = 2.9 h.
5. **`kernel_*` 9.1 h** (journals 14.6 h): 5.4 h (852 runs) is the one `kernel_shape.build` chain (sdk preprocess + ledger), plus the deleted `kernel_migration` (4.1 h) and pre-fix ledger/gate runs in unrebased worktrees (3.3 h).
