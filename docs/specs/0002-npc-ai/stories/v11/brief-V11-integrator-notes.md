# V11 — notes for V4b's integrator, when V11-1 shares the wave with B1 and B2

Additions to `../v4/brief-B-integrator.md`; its steps, order and rules stand. Read `README.md`
here and `brief-V11-1.md`, and V11-1's report, beside B1's and B2's.

**Before the build.** Confirm by listing that no file appears in two lanes: V11's are in
`README.md` §4. In particular B1's "embodiment interface header" must not be
`ElysiumEntityWorld.h` (it is not: B1 names `Public/ElysiumWorldServices.h`), and nobody but B2
touched `ElysiumNpcBaseMotor.cpp`. **After the second sitting (J10)** V11-1 also holds
`ElysiumNpcSchedule.inl` and `ElysiumNpcMingXiao.cpp` (the gate's declaration and one call site's
name) and **calls** `IElysiumEmbodiment::TraceRetail` from B1's `ElysiumWorldServices.h` without
editing it: confirm V11-1's diff has no hunk in that header or in `Tests/ElysiumTestServices.h`.
Apply V11-1's cross-lane lines (expected at most: the slot-601 dispatch in the death / removal
path, README §7; the human line's slot-602 guard; the gate's new name at a call site outside its
files — `CNPC_VChangBros`, `CNPC_VTzimisceRunner`; a test-double line in
`Tests/ElysiumTestServices.h`; B2's slow-turn line if R1b named a V11 file) — one line each, or
they are filed, not written.

**The build.** Still one (`uv run elysium build --arm`). `ElysiumEntityWorld.h` is a wide header:
expect a long incremental build, not a second one.

**Records, added to step 3's run**: `chase_melee melee_swing melee_ally_in_the_way`. The record
errors below are **written by the seam agent A0** (`../v4/brief-A0-seam.md` item 7, after S3);
check each is there and correct what is not (bug protocol step 1; retail source in each `about`;
no expectation loosened):
- both: the schedule matches take the `_NR` forms — `chase_melee` `advances`
  `^SCHED_TROIKA_MELEE_(CIRCLE_)?ADVANCE(_SLOW)?(_NR)? \(`, `swings`
  `^SCHED_TROIKA_MELEE_ATTACK1(_NR)? \(` (regex); `melee_swing` `attack_program` the same. Source:
  `CNPC_VHuman::SelectScheduleMeleeCombat 0x10385e40`, slot 308 true → `0xdc` / `0xca`, false →
  `0xdd` / `0xcb`; the `about` sentence "`0xdd` only with a ranged weapon" is reversed. If the
  trace prints another name for `0xcb` / `0xe1`, use the trace's, with the number.
  **`advances` must also admit `0xd2`** — `SCHED_TROIKA_MELEE_ADVANCE_SLOW_NR` if the trace prints
  it under that name — the legitimate approach program inside 100 units without `0x51` (S3 item
  8, line `0x6bb`).
- both: the `about` names the `MELEE_IDLE` step (S3 item 8): the selection that admits the NPC to
  melee answers `SCHED_TROIKA_MELEE_IDLE (0xc7)` because no band word is gathered until
  `m_bInMelee` is set (`FCanCheckAttacks 0x102953a0`); the next gather breaks it.
- `melee_swing`: drop `hit_event` (no attack clip of the bat bank authors an event; the contact
  is slot 312 `UpdateCharacter 0x103246d0` → `MeleeSwingUpdate 0x10346cd0`; `../v4/packets-R2.md`
  item 3 (c)); correct the `about`.
- both: the `about`'s "slot 555 `0x1026d9a0`" for the melee band → the weapon's slot 367
  (`0x103eac30` → `0x103ea7e0`) — **read whole** (README §1; S2 item 4, S4 item b), no longer
  "marked unread": out of reach the word is `9` beyond `max(1.2 × reach, 256)` units and `0x60`
  between reach and that; both lead to the same selector arm.

**Expect.**
- `chase_melee`: `START_COMBAT`, **one `MELEE_IDLE (0xc7)`** (retail), then `MELEE_ADVANCE_NR
  (0xcb)` — or `0xd2` once inside 100 units without `0x51` — (never `0xe7`), `DIST:COMBATMOVE`
  resolving to 100 (no `-1e+06` in the trace), `CAN_MELEE_ATTACK1` on arrival, then
  `MELEE_ATTACK1_NR (0xdd)`. Green; `known_red` removed. With B1's walk the approach is faster
  than any earlier trace: a moved time is not a red.
- `melee_swing`: one `MELEE_IDLE (0xc7)`, then `MELEE_ATTACK1_NR` → `MELEE_ATTACK1_SWING` →
  `task_melee_attack1` → its `taskdone`. Green if the player's health dropped (today's
  world-tick sweep). If only the `health < 100` probe fails: `known_red` "V4c C1: the contact
  sweep (slot 312 `MeleeSwingUpdate 0x10346cd0`)", a placed red; V11's part is proven by the
  schedule and task lines.
- **`melee_ally_in_the_way`** (new, J10; written red by A0, or by you from README §6 if A0's
  commit lacks it): the rear brawler `0xe1` (the circle, `_NR`), never `0xcb` while the front
  one stands between. Green; `known_red` removed.
- **`MELEE_IDLE 0xc7`: one after `START_COMBAT` is retail and is not a first-unmet.** Only **a
  second consecutive `MELEE_IDLE` with `0x51` or `0x60` (or `9`) standing** in the trace's
  `cond+` is a defect. A second one with none of them standing means the melee band raised
  nothing: that is the band (`0x103ea7e0`, lane V5a-1, J14.4) or slot 331 (`0x10347180`; V11-1's
  report says what its doc gives for `0x51`) — placed there with the trace line, not on V11.
- Any other first unmet (a `taskfail`, `WAIT_FOR_MELEE` with one brawler) is read against README
  §1 and triaged under the bug protocol.

**Tests, added to the family run**: `Elysium.Arm.AttackCoordinator.
Elysium.Arm.NpcKernelTroikaHelpers. Elysium.Arm.NpcKernelSchedule.` and the species family V11-1
names for `ElysiumNpcKernelSpeciesTests.cpp`.

**The full arena** (V4b's close runs it once): any record with three or more melee NPCs may move
(cap 2: the third waits, the farthest is evicted) — retail; note it in the verdict table.

**Order**: the melee band is V5a-1's (J14.4), and V11 must not share a wave with V5a (README §4):
V11 runs after V5a. If V5a-1 reported that no port body stood for the band and it ported one,
`chase_melee`'s out-of-reach word comes from it.

**Triage / spec**: N3 closed in `stories/v1/triage.md` (one line, both records); tick **V11** in
`spec.md` only if `melee_ally_in_the_way` is green and both older records are green or
`melee_swing`'s residue is the placed C1 red. The
commit stays V4b's one commit, its subject extended: `…; V11 -- the attack coordinator's list
(N3)`; stage V11's files by explicit path; never push; no `report*.md` for V11 — its verdict rows
go in the commit message's table.

**If V11 runs as its own wave instead**: the same list, with its own integrator, one build, the
two records by name, `uv run elysium test` and the prefixes above, the full arena once, one commit
`fix(npc): V11 -- the attack coordinator's list (N3)`.
