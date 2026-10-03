# Tracker — the character AI (0002, consolidated 2026-10-03)

**What's next = the first unticked box.** Tick here when the story's box is ticked in
[0002's spec](0002-npc-ai/spec.md), which is the source of truth for the text. 0018 and 0019 are
closed into it. The previous tracker (rows 01–50) is
[`0002-npc-ai/tracker-record-2026-09-30.md`](0002-npc-ai/tracker-record-2026-09-30.md); what still
needs a READ is [RE-BACKLOG.md](RE-BACKLOG.md).

Serial, one checkout, no worktrees: each wave fans out ≤3 coders on disjoint files, then one
integrator builds and tests once (spec rule 8).

## Step 1 — the instrument: tools and tests (no development before gate 1)

Wave 1 (Python; T2's and T3's Python halves):
- [x] **T1** — The `kernel_*` tools under budget (10 s cold, 1 s unchanged). M · Opus/high.
  Landed wave 1: unchanged ≤0.44 s; cold ≤8.3 s except `kernel --check` 10.5 s (reported).
- [ ] **T2** — The corpus MCP and the text tree under budget; the address index. M · Sonnet/high.
  Python half landed wave 1 (probe set ≤0.11 s, ≤19.7 KB; `research where`); C++ half landed
  wave 2 (`brief` 3.1 KB, `fields` 1.4 KB, `log_tail` 9.4 KB). Miss: `entity_get` on one NPC with
  no `fields`/`brief` is 82 KB.
- [x] **T4** — The runner, the lease and the waits (blocking build/test, no polling). S · Sonnet/medium.
  Landed wave 1: three prefixes in one boot 24.2 s; `--no-wait` exit 8; waits name the holder.

Wave 2 (C++; T2's and T3's C++ halves):
- [ ] **T3** — The test scale-down, tiered. M · Sonnet/medium. Its `pytest` half landed in
  wave 1 (0 failures; default run 19.7–25.9 s, not reliably under the 20 s budget). C++ half landed
  wave 2: default 176 / Arm 1,551 / Content 53 / Slow 2, all 0 failed; `MapActorTeardown` 50.6 →
  10.1 s. Misses: default C++ 35.5 s wall (2.1 s of tests; the boot), `pytest` 20.8 s.
- [x] **T5** — The Green Room as the live test suite (`uv run elysium arena`). M · Opus/high.
  Landed wave 2: 4 records in one boot, self-tests and control pass, `cover` red at known red 1
  (`seqfinished` after `smith_lean_left_into rate=0`); 28.6 s warm, 37.1 s cold (miss).

Wave 3 (build work, one agent):
- [ ] **T6** — The incremental build (today median 46 s, p90 280 s). M · Opus/high.
- [ ] **Gate 1** — zero queries over 60 s; every `kernel_*` command and corpus probe under 10 s;
  default C++ ≤25 s wall (today 122 s) and `pytest` ≤20 s (today 384 s), zero failures; three
  prefixes in one boot under 25 s; a one-`.cpp` edit rebuilt in ≤60 s; no polling loop or lease
  refusal; the arena suite runs.

## Step 2 — the landed work, proven live

- [ ] **V1** — The inventory: every landed behaviour → its scenario; the 22 re-classed divergences. S.
- [ ] **V2** — The first full run and the triage. S · Opus/high.
- [ ] **V3** — Fix: the arbiter retired; scenes, dialogue and places as retail runs them. L · Opus/high.
- [ ] **V4** — Fix: the animation chain under the kernel. M · Opus/high.
- [ ] **V5** — Fix: the attack conditions and the combat interrupts. S · Fable/medium.
- [ ] **V6** — Fix: session, clock and lifecycle. M · Opus/high.
- [ ] **V7** — The 19 inputs and the one-line items. S · Sonnet/medium.
- [ ] **V2 again** — the full run, every scenario green.
- [ ] **V8** — `sp_tutorial_1` and `sm_hub_1`, live. S · Opus/high.
- [ ] **V9** — The second cut: the arm tests the scenarios cover. S · Sonnet/medium.
- [ ] **Gate 2** — every scenario green in one run; both maps played, 0 ensure / assert.

## Step 3 — the road to close the character AI (re-planned at gate 2)

- [ ] **R1** — Investigation: the sound list, the alert ladder, the programs. M.
- [ ] **R2** — Places and patrols. S–M.
- [ ] **R3** — Cover, kick and the goal selectors. L.
- [ ] **R4** — Social: squads, followers, the coordinator, relationships, logic entities. M.
- [ ] **R5** — Flee, cower, the player on the head. S.
- [ ] **R6** — Makers and templates. S.
- [ ] **R7** — The hub's species rows. S–M.
- [ ] **R8** — The witnesses, closing; 0002 closes and 0003 onward resume.
