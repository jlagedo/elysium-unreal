# Brief — the integrator of the wave V5a-1 + V5a-2 + A3

Runs after the three coders report (their reports are given to you). Read `README.md` here,
`brief-V5a-1.md`, `brief-V5a-2.md`, `../v4/brief-A3-view-cone.md`, `../v4/README.md` §1 "Slot 363
on the enemy" and § "Rules for every agent of V4", `../v4/packets-R2.md` items 3 and 6,
`stories/v1/triage.md` rows N1, N2 and § "The known reds" 2 and 3 (Grep; never the file whole),
`Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more. *(amended after settling
   packets S2 / S4 and the judge's second sitting, 2026-10-04.)* Expected: from **V5a-1** (it
   now ports the melee weapon's band `0x103ea7e0` whole, J14.4) — an accessor over the wielder's
   sequences for an activity if it lives outside its files (`ElysiumNpcAnim.cpp`), and five
   rows for `research/tooling/ghidra/driver/kernel_tunables.tsv` (`0x104492d0` f64 0.7,
   `0x104454d0` f32 0.5, `0x1049ae90` f32 1.2, `0x1044ddb0` f32 256.0, `0x10449260` f64 0.25 —
   skip a cell the table already holds), then `uv run elysium research gen_kernel_tunables`
   once before the build; from **V5a-2** (it now fixes `Attack_Rate`'s default to retail's 1.0,
   an old port bug) — each reader of the mode record's `AttackRate` that tests 0 as "absent"
   (`ElysiumWeaponClasses.cpp`): decide per line whether the test is now dead or still guards an
   unparsed record, and say so in the commit message; and `ScaleWeaponBurstPause`'s arms if they
   differ from `0x102c5570` (no target → `sqrt(1 / BaseRange)`; `dist <= 0` → `× dist`).
   **Wave disjointness, by listing, before the build** ([V5a-1, V5a-2, A3]): no file in two
   lanes; neither S2 nor the second sitting added a file to any of the three.
2. **Build once**: `uv run elysium build --arm`. Fix only integration breaks (compile, link, a
   wrong call across two lanes' files). A second build only for a break of your own fix; a third
   means stop and report.
3. **Run the records, by name, once**: `uv run elysium arena range_bands cover_armed
   ranged_open_fire sense_enemy_facing_me cover control_sequence`.
4. **Acceptance.**
   - `range_bands`: green; `known_red` removed. If its first unmet is now `0xef` not breaking
     (red 4), retarget `known_red` to "4 (V5)" with the trace line.
   - `cover_armed`: green (`0xa3`, never `0xa4`); `known_red` removed.
   - `ranged_open_fire`: no `BEHIND_ENEMY`; no `taskdone task_wait_attack_time1` before 1.05 s;
     green; `known_red` removed. Record correction (R2 item (c), a record error with its retail
     source `CWeaponRanged 0x10238160`): `shot_event` matches `3031`.
   - `sense_enemy_facing_me`: green; `known_red` removed.
   - `cover`, `control_sequence`: still green. A moved timing is read against retail (the top
     clear `0x1026de02`, the wait `0x102a337d`) and triaged under the bug protocol, never loosened.
   - `melee_swing` / `chase_melee` are not this wave's: do not edit them (their `about` text on
     slot 555 is corrected by V11, README §7).
5. **Tests, once each**: `uv run elysium test` (the default tier), then the touched arm prefixes
   in one boot: `uv run elysium test Elysium.Arm.NpcKernelConditions.
   Elysium.Arm.NpcKernelStartTask19. Elysium.Arm.NpcKernelSchedule. Elysium.Arm.CombatCharacter.`
   plus `Elysium.Substrate.NpcCombat.` if lane 1 touched `ElysiumNpcCombatTests.cpp`.
6. **The full arena once**: `uv run elysium arena`. Every record green before the wave stays
   green; each moved verdict gets a row (record, before, after, why) in the commit message's
   table, and a new red is placed under the bug protocol (triage row, `known_red`), not parked.
7. **Triage and spec**: mark N1, N2 and known red 2 closed in `stories/v1/triage.md` (one line
   each with the record); slot 363's part of red 3 closed. Tick nothing in `spec.md` (V5 ticks
   with its other half; V4a with its own wave); add one dated line under the V5 box: "V5a landed".
   ~~File README §7's melee-band item (`0x103ea7e0`) on the judge's list.~~ **Withdrawn**: the
   band is read whole and reads only sequence data the bake carries; V5a-1 ported it (J14.4) —
   record in the commit message which port body stood for it before. A gunman now waits
   retail's time between shots **and**, once V4o's O3 lands, never runs dry (J12): no record
   of this wave may expect a reload after emptying a gun.
8. **Commit once**, staging by explicit path (never `git add -A` / `.`), on
   `spec-0002/step-2`, never push: `fix(npc): V5a + A3 -- GatherAttackConditions' clears, timers
   and tail; the wait before the shot (N2); the cover tail reads the enemy's weapon (N1); slot
   363 on the combat character`. The message body carries the verdict table (record, before,
   after) and the test totals. **No `report*.md` file**: the table lives in the commit message
   and your report.

Rules: a build or a run blocks until done — wait for it or its completion notification, never a
sleep or polling loop. Query budget: 10 s warns (log to
`$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops; never read a file over ~200 KB whole.
Text through Grep / Read / Glob. Report ≤300 words: the build's wall time, each record's verdict
before and after, the tier totals, what is left red and where it is placed.
