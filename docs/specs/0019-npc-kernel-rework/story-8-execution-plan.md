# Story 8 execution plan — passes I and C without the story-5 waste

Proposed 2026-09-27 (Fable), not yet accepted. Replaces the pass-I paragraph's *method* in
[spec.md](spec.md) story 8; the *deliverable* (every `rule` arm in retail order, one test per row
with every arm asserted, the residue that only falls, a fresh reviewer on another model) is
unchanged. Inputs: the thirteen pass-R packets, the class tree (story 5), the tracker row 06 review
of 2026-09-27.

## 1. Where story 5's time went

Measured from every `$ELYSIUM_WORK_ROOT/logs/*.json` of 2026-09-25 → 27 (the story-5 days). The
tool time was serial, on one checkout, and the porting agent waited on all of it.

| cost | runs | wall | cause |
|---|---|---|---|
| builds that touched a hot header (≥ 2 min) | 88 | 5.8 h | `ElysiumNpc.h`, the generated slot `.inl`s, `ElysiumNpcConditions.h`, `ScheduleHost.h`, `KernelTunables.h`, `KernelShape.h` are each included by 290–383 of the module's 878 TUs; one edit is a 4-minute rebuild |
| `uv run elysium research … --check` | 791 | 6.1 h | the seven checkers (15–30 s each, they rebuild the ledger model from scratch) were run per edit, not per commit |
| full test suites (≥ 90 s) | ~60 | 2.2 h | `Elysium.Substrate` (2 min) run per edit instead of per family |
| narrow test runs | ~280 | 1.5 h | 19 s each (8 s is editor boot). These are the right tool; keep them |
| map smokes (`run play`) | 14 | 2.0 h | 7–39 min each, run per commit |
| builds that touched only `.cpp` (< 30 s) | 71 | 0.2 h | adaptive unity compiles a dirty file alone: 2–17 s. Also the right tool |

≈ 20 h of tool time in three days; ≈ 16 h of it is the first three rows, and all three are
avoidable by construction.

## 2. Four rules

**R1 — Headers once.** Every header-visible change of the story lands in one *shape commit* before
any body is written; after it, no file in the hot set (the six above plus `ElysiumNpcBase.h`,
`ElysiumNpcEnemy.h`, anything `gen_kernel_shape` writes) is touched until pass C. The gate refuses
a family diff that touches the set. Species headers (`ElysiumNpcDog.h` 9 TUs … `ElysiumNpcHuman.h`
84 TUs) are cheap and may be edited per family. Budget: header rebuilds 88 → ≤ 10 (shape commit,
one per lane merge, the pass-C rename).

**R2 — Bodies per file.** A porter writes only `.cpp`: the retail class's own file (56 exist) for
species overrides and helpers, the family's base/Troika file for the two spine bodies, the family
test file. Every file a family will need is created (empty) in the shape commit, so unity chunk
assignment never shifts under a lane. Inner loop: build ≤ 20 s + family prefix 19 s ≈ 40 s.
Optional for the giants: a kept-open editor with Live Coding (`LiveCoding.Compile`, then
`Automation RunTests <prefix>` through the `elysium` MCP console) cuts that to ≈ 20 s; not required.

**R3 — Verify at boundaries.**
- per edit: the family prefix only (`Elysium.Substrate.NpcKernel<Family>19.`);
- per family close: the gate script (seconds) + one run of
  `NpcKernel<Family>19.+Elysium.Substrate.Npc+Elysium.Substrate.Schedule` (~1 min) +
  `kernel_shape --unported` (16 s);
- per lane merge into `main`: full `Elysium.Substrate` (2 min) + the seven `--check`s (~3 min);
  pytest only when Python tooling changed;
- per story: map smoke twice (after wave 2, after the rename), tutorial and hub only.
Budget: full suites ~60 → ~15; `--check` runs 791 → ~40; smokes 14 → 2.

**R4 — Lanes in worktrees, one porter per lane.** The activity lock is per project path, UBT
serialises checkouts on the engine mutex (the pipeline already passes `-WaitMutex`, per-checkout
logs and UBA ports), a `-nullrhi` test run is ~2–3 GB, a worktree's `Intermediate` is 8 GB
(162 GB free). Five lanes fit an 8-core / 32 GB machine; cheap builds queue ≤ 1 min at worst.

## 3. The dependency graph, re-read

The spec's lane-A order (Conditions → RunAi → StartTask → RunTask → Select → Think) is a reading
order, not a build dependency. Every slot is already a virtual on the tree; a body is tested by
calling it on a prepared NPC, which is what the landed State19 / Lifecycle19 / Maintain19 suites do.
Only two families wire the loop, and they are the smallest: RunAi19 (17 rows, 3.9 KB) and Think19
(15 rows, 6.1 KB; carries the 431/437/433/442 dispatch from the story-5 hand-off). Boss19 and
Werewolf19 are helpers with their own tests; only the StartTask/RunTask *arms that call them* wait.

Weight per family, from the packets (retail bytes of `rule` bodies / branch rows):

| family | rows | bytes | arms | note |
|---|---|---|---|---|
| StartTask19 | 27 | 74,381 | 907 | Troika `StartTask` 24.3 KB (176 arms), base 18.3 KB (107 arms), 25 species bodies 32 KB |
| RunTask19 | 23 | 25,883 | 610 | Troika 6.8 KB, base 4.0 KB |
| Select19 | 31 | 21,411 | 676 | Troika `SelectSchedule` 5.5 KB replaces the port's guessed selector (`ElysiumNpc.cpp:1495`) |
| Misc19 | 31 | 12,759 | 345 | `SetEnemy` / `ChooseEnemy` first: everything reads them |
| Werewolf19 | 17 | 11,691 | 307 | helpers, all on `FElysiumNpcWerewolf` |
| Conditions19 | 20 | 10,242 | 261 | base `GatherEnemyConditions` 2.8 KB, Troika `GatherConditions` 2.1 KB |
| Spawn19 | 48 | 8,677 | 105 | 48 small bodies over ~30 class files |
| Script19 | 20 | 6,988 | 170 | `CCineNPC` / `CCineAI` line |
| Think19 | 15 | 6,085 | 157 | wave 2 |
| Damage19 | 26 | 5,218 | 123 | |
| RunAi19 | 17 | 3,862 | 99 | wave 2 |
| Boss19 | 11 | 2,942 | 47 | helpers |
| Damaged19 | 9 | 1,130 | 40 | 3 rows still open (the 6 others landed with Lifecycle19 / story 5) |

191 KB of retail code in all; StartTask19 is 39 % of it and must be split.

## 4. Waves and lanes

**Wave 1 — five lanes in parallel** (weights balanced to ~35–45 KB each):

| lane | families | split |
|---|---|---|
| L1 | StartTask19: Troika `StartTask` 0x102a1910 | one porter; the body may be cut into part files only at the packet's chunk boundaries (chunk 0 = the dispatch prologue), one `switch`, retail's default arm once |
| L2 | StartTask19: base `StartTask` 0x102827f0 + the 25 species `StartTask` bodies | same rule for the base body |
| L3 | RunTask19, then Damage19 | |
| L4 | Select19, then Conditions19 | |
| L5 | Misc19 (SetEnemy/ChooseEnemy first), Werewolf19 + Boss19 helpers, Spawn19, Script19, Damaged19's 3 | small bodies, many files |

**Wave 2 — one lane on `main`**: RunAi19 → Think19, the loop wiring, then the two existing
loop witnesses (`SCHED_TROIKA_CHASE_ENEMY_FAILED`, the Maintain19 suite) run through the retail
`NPCThink → RunAI → GatherConditions → MaintainSchedule → GetNewSchedule (437 before 438)` chain.
First smoke here.

**Pass C**: Misc19's remaining rows, the absorbed stories' sentences (25b, 25c, 26, 16b, 10d), the
ticks, the pure-move rename commit dropping the `19` suffix (one full rebuild), second smoke.

## 5. Steps and budgets

| step | who | wall | what |
|---|---|---|---|
| 0 | orchestrator, `main` | ~1 h | fix the second-judge join in `kernel_packet.py:738` (it keeps a row only when its first cell starts with a *function* address; the judge wrote instruction addresses, so 27 of 60 rows are dropped) and regenerate the thirteen packets, which also puts the 32 `CORRECTED` clauses into the verdict lines (packets are from 08:56, the write-back is 09:27); `SLOT_PORT_MAP` rows for slots 460/461 → `PreSelectIdealStateRetail` / `SelectIdealStateRetail` so the 32 State19 residue rows stop reading `no-override`; the gate script (§6); pytest |
| 1 | one subagent + orchestrator, `main` | ~1.5 h | the shape commit: from the verdict `target` column and `signatures.tsv`, declare every missing method of the 295 rows on its class-tree owner (the 57 `stub` slots flipped `hand:` with forwarding bodies that tally exactly what the generated stub tallied; helper members named as the targets spell them); create every family `.cpp` / test file empty; regenerate; **one** header rebuild; full `Elysium.Substrate`; the seven checks; re-pin `unported.tsv` (778 → 746 after the 460/461 fix, unchanged otherwise) |
| 2 | script, parallel with 0–1 | ~1 h, off the critical path | five worktrees from today's `main`, `.elysium.local.env` copied, clean builds staggered two at a time (a clean build is unmeasured on this tree; the longest logged build is 18 min); after step 1 lands, each rebases onto it: one 4-min rebuild each, in parallel |
| 3 | five porters + reviewers | L1 is the critical path | per family: port → gate → reviewer on another model (reads the diff, the packet, the listing through `vtmb-corpus`; no build) → fixes → gate → merge to `main` (regenerate the census at the merge, one rebuild). The lane starts its next family while its review runs |
| 4 | one porter, `main` | ~2 h | wave 2 as above |
| 5 | orchestrator | ~1.5 h | pass C |

Machine time for the whole story, all lanes: header rebuilds ≤ 10 (≈ 45 min), clean builds 5
(parallel, ≈ 1 h wall, hidden behind steps 0–1), full suites ≈ 15 (30 min), check sets ≈ 8
(25 min), smokes 2 (30 min), inner loops ≈ 295 rows × ~3 iterations × 40 s ≈ 10 h *spread over
five lanes* (≈ 2 h wall each). Against story 5's ≈ 20 h serial, and against the spec's twelve
serial families.

Wall-clock, critical path: steps 0–1 (~2.5 h) → L1 (Troika `StartTask`, 24 KB / 176 arms: the
one body a porter will need most of a day for, with review) → wave 2 (~2 h) → pass C (~1.5 h).
That is one long day if L1 finishes in ~5 h, two days if it takes ten. Every other lane finishes
inside L1's window. The estimate assumes a porter lands ~1 KB of retail per 10–15 minutes with
tests, which is what Lifecycle19 (34 rows) and State19 (21 rows) cost.

## 6. The gate (built in step 0, `kernel_skeleton gate --family <F> [--base <rev>]`)

Mechanical, seconds, run by the porter before hand-off and by the orchestrator before merge:
1. every `rule` address of the family appears in a body's citation comment in the family's files;
2. **arm coverage from the skeleton, not a hand table**: every branch and call address in the
   family's skeleton JSON appears in the port files (body comment or test), and the report lists
   the missing ones — this is the "stubbed arms inside a body whose row reads ported" habit made
   visible;
3. the three seam searches of `lessons-block.md`, executed and printed (retail address in `Source/`,
   retail offset in the shape map, recovered name in the family `.inl`s) for every new accessor or
   member the diff adds;
4. the diff touches no hot header;
5. `kernel_shape --unported`: the family's slot rows are gone and no other row appeared;
6. test count for the family ≥ its rule rows, every test name carries the retail address;
7. no port-only twin of a retail body remains (a body citing an address the family now owns in
   another file is a defect).
A red gate goes back to the porter; nothing red reaches a reviewer.

## 7. What the reviewer reads

The full read stays: every ported body against `vtmb_code` / `vtmb_asm`, the test edits against
their inline citations, the gate output. Porters and reviewers are different models (proposal:
Claude porters — Opus on the spine bodies and giants — and luna / Sol reviewers through `codex
exec` with the corpus MCP, which spends no Claude quota; the reverse routing is the one story 29e
used and it needed the full read precisely because those porters invented seams). The orchestrator
adjudicates a porter/reviewer disagreement against the listing and records it in the packet's
family file as a `CORRECTED` clause when the packet was wrong.

## 8. Risks and unknowns

- Clean-build time per worktree is unmeasured (18 min is the longest logged build; a from-scratch
  build compiles plugins too). It is off the critical path only if the worktrees are created at
  step 0.
- Live Coding cannot take a class-layout change; R1 makes that a non-issue, and it is optional.
- Splitting a giant `switch` into part files is a port structure, not retail's; it is allowed only
  along the packet's chunk boundaries, with one dispatch and retail's default arm once, and the
  reviewer checks that no case shares a body across a cut.
- Five concurrent editors on 32 GB is the ceiling; a sixth lane waits.
- The `hand:` flip is still a live mechanism for the 57 base/Troika stubs (the generator emits no
  definition for a `hand:` row); species overrides need no flip. Both are in the shape commit.
