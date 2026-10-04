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
- [x] **T2** — The corpus MCP and the text tree under budget; the address index. M · Sonnet/high.
  Python half landed wave 1 (probe set ≤0.11 s, ≤19.7 KB; `research where`); C++ half landed
  wave 2 (`brief` 3.1 KB, `fields` 1.4 KB, `log_tail` 9.4 KB). Closed wave 3: `entity_get` on one
  NPC answers its brief by default, 3.4 KB on the wire (was 82 KB); every field on `full=true`.
- [x] **T4** — The runner, the lease and the waits (blocking build/test, no polling). S · Sonnet/medium.
  Landed wave 1: three prefixes in one boot 24.2 s; `--no-wait` exit 8; waits name the holder.

Wave 2 (C++; T2's and T3's C++ halves):
- [x] **T3** — The test scale-down, tiered. M · Sonnet/medium. Its `pytest` half landed in
  wave 1 (0 failures; default run 19.7–25.9 s, not reliably under the 20 s budget). C++ half landed
  wave 2: default 176 / Arm 1,551 / Content 53 / Slow 2, all 0 failed; `MapActorTeardown` 50.6 →
  10.1 s. Wave 3 (T6's boot cut): default C++ 15.6–19.6 s wall, ticked on it; `pytest` 19.7 /
  26.0 s, still not reliably under 20 s (gate 1).
- [x] **T5** — The Green Room as the live test suite (`uv run elysium arena`). M · Opus/high.
  Landed wave 2: 4 records in one boot, self-tests and control pass, `cover` red at known red 1
  (`seqfinished` after `smith_lean_left_into rate=0`); 28.6 s warm, 37.1 s cold (miss).

Wave 3 (build work, one agent):
- [ ] **T6** — The incremental build (today median 46 s, p90 280 s). M · Opus/high. Landed wave 3:
  the boot cut (Zen kept running, `Automation Now`, the uncontrolled-changelist tracker off headless,
  the arena's forced exit): default tier 22.1–22.5 s warm (35.5–45.7 with a slow Zen start) → 15.6–19.6,
  arena 28.6–37.1 → 25.6–27.1. Plain unity (a wave's commit 226 s → 1.9 s, nothing compiled) and the
  arm tier compiled on demand (switching it: 104 s on, 81 s off); a no-change `research kernel` no
  longer forces a full rebuild (202 s → 1.6 s). Edit mix, s: null 1.9 → 1.9, `.cpp` 9.4 → 9.3, `ElysiumNpcBase.h` 193.6 → 156.8,
  `ElysiumNpc.h` 187.8 → 154.2, `ElysiumEntityWorld.h` 209.8 → 182.6, arm test 13.6 → 11.9,
  `ElysiumTestServices.h` 93.4 → 72.1, census 11.6 → 8.1. **Miss: p90 164.5 s (target 90)** — the
  kernel headers' fan-out, compiled 3–6 at a time under the machine's commit limit.
- [x] **Gate 1** — zero queries over 60 s; every `kernel_*` command and corpus probe under 10 s;
  default C++ ≤25 s wall (today 122 s) and `pytest` ≤20 s (today 384 s), zero failures; three
  prefixes in one boot under 25 s; a one-`.cpp` edit rebuilt in ≤60 s; no polling loop or lease
  refusal; the arena suite runs. Accepted by the owner 2026-10-03 with four misses as measured
  (`kernel --check` cold 10.5 s, `pytest` 19.7–26 s, edit-mix p90 108 s, seven sleep-waits); T6
  stays open as an on-demand story (the kernel headers' fan-out).

## Step 2 — the landed work, proven live

- [x] **V1** — The inventory: every landed behaviour → its scenario; the 22 re-classed divergences. S.
  Landed 2026-10-03: 96 records, `stories/v1/inventory.md`, `divergences.md`.
- [x] **V2** — The first full run and the triage. S · Opus/high. Landed 2026-10-03: 54 pass, 35
  expected-fail, 4 fail, 1 unexpected-pass, 2 error; ten new reds placed, two planning bugs
  (V11, V12), fifteen harness gaps (`stories/v1/triage.md`).
- [x] **H** — The harness wave: H1–H5 (the model-less row, sight through arena solids, `never`
  with a start and a count, the player's state per record, player probes and actions). S–M.
  Landed 2026-10-04, acceptance open: 62 pass / 36 expected-fail / 3 fail / 1 unexpected-pass of
  102; cameras and `sense_cone_enter` green, `verbs_stealth_kill` red 3; open Q-H1 `cover_reclaim`,
  Q-H2 `memory_occluded_kept` (`stories/v1/triage.md` § "Wave H"). Closed 2026-10-04: both
  record errors (the port matches retail), re-stated and green; ticked.
- [ ] **V3** — Fix: the arbiter retired; scenes, dialogue and places as retail runs them. L · Opus/high.
  Cut 2026-10-04 (`0002-npc-ai/stories/v3/README.md`):
  - [x] **V3r** — the two reading packets (dialogue upkeep `0x102c1400`; the dialogue inputs). S.
    Landed 2026-10-04 (`a156e746`).
  - [x] **V3a** — the seam, then the kernel's sequence plays on every body. M. Landed 2026-10-04:
    default 176 / 0 failed, arm 1550 / 0 failed; suite 66 pass / 37 expected-fail / 1 fail / 1
    unexpected-pass of 105, only `cover` moved (→ pass). Patrols re-triaged: N13 (V4); new red N12
    (R2); the ledger regeneration waits for V3b's build.
  - [x] **V3b** — places and patrols as programs; the ambient executor deleted. M. Landed
    2026-10-04, acceptance open: default 176 / 0 failed, arm 1551 / 0 failed; suite 70 pass / 33
    expected-fail / 1 fail (`rollcall_vzombie`, H11) / 1 unexpected-pass (`hear_world_investigate`,
    N4) of 105 (after the animal records' correction). Green: `input_useinteresting`,
    `map_hub_idle`, `rollcall_vhuman`, `rollcall_vhumancombatpatrol`. Left for a V3b follow-up
    wave: N15 (`places_thug_pt1`, the sneak-past's first half), H16 (`places_pedestrian_visit`),
    Q-V3b1 (`hub_crosswalk_wait`). N10 was red 6, now red 5 (V6); N14 filed (V6). Ledger
    regenerated, `kernel --check` clean. Follow-up wave 2026-10-04, ticked: N15 and H16 closed;
    default 176 / 0, arm 1551 / 0; suite 71 pass / 32 expected-fail / 1 fail (H11) / 1
    unexpected-pass (N4) of 105; `places_thug_pt1` green, the sneak-past's first half green
    (hearing half V12), `places_pedestrian_visit` red on N13 (V4), `hub_crosswalk_wait` on N16
    (V13, proposed); N17 filed (R2).
  - [x] **V3c** — the scene hold through `m_scriptState` and `SCHED_AISCRIPT`. M. Landed
    2026-10-04 with H17 (the map host seeded at boot). `script_walk_to_mark` runs retail's program
    (SCRIPT, `0xf2`, walk, plant, face, enable, wait, `OnBeginSequence`, play, `OnEndSequence`,
    idle), red only on N19 (sequence 0; V4's judge). Default 171 / 0, arm 1550 / 0; suite 71 pass /
    32 expected-fail / 1 fail (H11) / 1 unexpected-pass (N4); divergence 18 closed, K1 kept.
  - [ ] **V3d** — the dialogue hold as a program; the arbiter deleted whole. M–L.
- [ ] **T6b** — The header pass: the kernel headers' include fan-out, after V3d; closes T6
  (`0002-npc-ai/stories/t6b/brief.md`). M · Opus/high.
- [ ] **V4** — Fix: the animation chain under the kernel. M · Opus/high.
- [ ] **V5** — Fix: the attack conditions and the combat interrupts. S · Fable/medium.
- [ ] **V6** — Fix: session, clock and lifecycle. M · Opus/high.
- [ ] **V11** — The attack coordinator's list (pulled from R4; melee never starts without it). S–M · Opus/high.
- [ ] **V7** — The 19 inputs and the one-line items. S · Sonnet/medium.
- [ ] **V10** — A sound's life in `Listen` (new: the expiry race). S.
- [ ] **V12** — The footstep sound producer (pulled from R1; the tutorial's hearing half). S.
- [ ] **V13** — The pedestrian nav area in the hub's bake (new, from the V3b follow-up: N16;
  `hub_crosswalk_wait`). S–M. Accepted 2026-10-04 (the judge: implement now, ahead of V3c); runs
  with the two senses bugs from Q-H3 (the SEE clear in `OnLooked`, the last-known position's field).
  Wave 2026-10-04, not ticked: the senses bugs landed (Q-H3 closed); the floating-floor lead was
  refuted by the bake. N16's measured cause is that the nav-area marks never enter the navigation
  octree at bake time (slabs and door cuts alike); the registration fix and two re-bakes await the
  owner / the judge.
  Second wave 2026-10-04, committed, not ticked: the marks register and both maps are re-baked; the
  road reads pedestrian, linked doors are cut, the tutorial's unlinked doors are walls. Open: N20
  (tutorial Rat links, pipeline, with the judge), N21 (two hub gates still walkable), Q-V13b (the
  ×8 route's crossing), Q-V13c (no `CROSSWALK_WALK` after the green).
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
