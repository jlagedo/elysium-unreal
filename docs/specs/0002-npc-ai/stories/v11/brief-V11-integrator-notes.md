# V11 — notes for V4b's integrator, when V11-1 shares the wave with B1 and B2

Additions to `../v4/brief-B-integrator.md`; its steps, order and rules stand. Read `README.md`
here and `brief-V11-1.md`, and V11-1's report, beside B1's and B2's.

**Before the build.** Confirm by listing that no file appears in two lanes: V11's are in
`README.md` §4. In particular B1's "embodiment interface header" must not be
`ElysiumEntityWorld.h`, and nobody but B2 touched `ElysiumNpcBaseMotor.cpp`. Apply V11-1's
cross-lane lines (expected at most: the slot-601 dispatch in the death / removal path, README §7;
the human line's slot-602 guard) — one line each, or they are filed, not written.

**The build.** Still one (`uv run elysium build --arm`). `ElysiumEntityWorld.h` is a wide header:
expect a long incremental build, not a second one.

**Records, added to step 3's run**: `chase_melee melee_swing`. First correct the record errors
(bug protocol step 1; retail source in each `about`; no expectation loosened):
- both: the schedule matches take the `_NR` forms — `chase_melee` `advances`
  `^SCHED_TROIKA_MELEE_(CIRCLE_)?ADVANCE(_NR)? \(`, `swings`
  `^SCHED_TROIKA_MELEE_ATTACK1(_NR)? \(` (regex); `melee_swing` `attack_program` the same. Source:
  `CNPC_VHuman::SelectScheduleMeleeCombat 0x10385e40`, slot 308 true → `0xdc` / `0xca`, false →
  `0xdd` / `0xcb`; the `about` sentence "`0xdd` only with a ranged weapon" is reversed. If the
  trace prints another name for `0xcb` / `0xe1`, use the trace's, with the number.
- `melee_swing`: drop `hit_event` (no attack clip of the bat bank authors an event; the contact
  is slot 312 `UpdateCharacter 0x103246d0` → `MeleeSwingUpdate 0x10346cd0`; `../v4/packets-R2.md`
  item 3 (c)); correct the `about`.
- both: the `about`'s "slot 555 `0x1026d9a0`" for the melee band → the weapon's slot 367
  (`0x103eac30` → `0x103ea7e0`), marked unread.

**Expect.**
- `chase_melee`: `START_COMBAT`, then `MELEE_ADVANCE_NR (0xcb)` (never `0xe7`), `DIST:COMBATMOVE`
  resolving to 100 (no `-1e+06` in the trace), `CAN_MELEE_ATTACK1` on arrival, then
  `MELEE_ATTACK1_NR (0xdd)`. Green; `known_red` removed. With B1's walk the approach is faster
  than any earlier trace: a moved time is not a red.
- `melee_swing`: `MELEE_ATTACK1_NR` → `MELEE_ATTACK1_SWING` → `task_melee_attack1` → its
  `taskdone`. Green if the player's health dropped (today's world-tick sweep). If only the
  `health < 100` probe fails: `known_red` "V4c C1: the contact sweep (slot 312
  `MeleeSwingUpdate 0x10346cd0`)", a placed red; V11's part is proven by the schedule and task
  lines.
- Any other first unmet (a `taskfail`, `MELEE_IDLE 0xc7` looping, `WAIT_FOR_MELEE` with one
  brawler) is read against README §1 and triaged under the bug protocol.

**Tests, added to the family run**: `Elysium.Arm.AttackCoordinator.
Elysium.Arm.NpcKernelTroikaHelpers. Elysium.Arm.NpcKernelSchedule.` and the species family V11-1
names for `ElysiumNpcKernelSpeciesTests.cpp`.

**The full arena** (V4b's close runs it once): any record with three or more melee NPCs may move
(cap 2: the third waits, the farthest is evicted) — retail; note it in the verdict table.

**Triage / spec**: N3 closed in `stories/v1/triage.md` (one line, both records); tick **V11** in
`spec.md` only if both records are green or `melee_swing`'s residue is the placed C1 red. The
commit stays V4b's one commit, its subject extended: `…; V11 -- the attack coordinator's list
(N3)`; stage V11's files by explicit path; never push; no `report*.md` for V11 — its verdict rows
go in the commit message's table.

**If V11 runs as its own wave instead**: the same list, with its own integrator, one build, the
two records by name, `uv run elysium test` and the prefixes above, the full arena once, one commit
`fix(npc): V11 -- the attack coordinator's list (N3)`.
