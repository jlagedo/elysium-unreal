# Brief — V5b's integrator (and V5's close)

Runs after the three coders report (their reports are given to you). Read `README.md` here, the
three lane briefs, `spec.md` § "Standing rules", § "The bug protocol" and the V5 box (Grep "V5"),
`stories/v1/triage.md` row "4 `0xef` sink" and § "J12" (Grep; never the file whole),
`../v4/packets-S4.md` item a, `Arena/README.md`, and `stories/v4/report-c.md` § "The reading owed"
if the C integrator wrote one (item 0 below). You own every `Arena/` edit of this wave.

0. **Before anything else.**
   - **The reading owed** (J6 part (c), filed to V5 by V4c's integrator): for each entry — a class
     with a row on `sp_tutorial_1` or `sm_hub_1` is a **stop** (report it; the wave does not
     start); any other goes on `spec.md`'s "on demand, not in the sequence" line as one row. The
     four species of J11 are already there (`spec.md` ~:812-816): add nothing for them.
   - **Red 4's record, on the tree as it stands** (no build needed): write
     `Arena/scenarios/combat/ranged_step_back_holds.json` (item 3) and run it alone
     (`uv run elysium arena ranged_step_back_holds`). Green → red 4 is closed as "not a mask
     defect" (README §1.1) with the trace's `0xef` lines; red → triage the first unmet under the
     bug protocol and place it; either way the wave goes on.
   - Write `Arena/scenarios/combat/ranged_fake_reload.json` (item 3), red, with
     `"known_red": "V5b lanes 1 + 2: the ranged pre-pass 0x102b8620's fake-reload arm reads a seam answering 0, the count is never re-rolled from the template (0x102c54c0) nor counted down per set (0x10268919)"`.
1. **Read the three diffs before the first build**, for: a local or a parameter that shadows a
   member or an outer local (C4458 / C4459 are errors here); a missing include (lane 1 calling
   `FElysiumWeapon::CanReloadMagazine` needs the weapon's header by its layer path; lane 3 calling
   `ElysiumNpcCond::WeaponCapability` needs `Substrate/ElysiumNpcConditions.h`); the shared names
   of README §3 spelled identically in lanes 1 and 2. **Wave disjointness, by listing**: 18 paths,
   none in two lanes (README §4). **Both halves of the count landed**: lane 1's re-roll and gate,
   lane 2's per-set count-down — with only lane 1, every gunman fake-reloads for ever.
   Then **apply the cross-lane lines** the coders reported, nothing more. Expected: a reader of
   `ActiveWeaponCapabilityWord` whose comment now lies (lane 3's list); a declaration lane 2 found
   homeless (`m_bIsJammed`, `m_bInterruptReload`, the second magazine); a test in a file of no
   lane that pinned the seam's 0.
2. **Build once**: `uv run elysium build --arm`. Fix only integration breaks (compile, link, a
   wrong call across two lanes' files). **Cap: two builds**; a third means stop and report.
3. **The two new records.**

   **`ranged_step_back_holds`** — red 4. `about`: retail's `0xef`
   (`sched_troika_step_back_range_attack1.sch`) lists `NEW_ENEMY ENEMY_DEAD LIGHT_DAMAGE
   HEAVY_DAMAGE ENEMY_OCCLUDED NO_PRIMARY_AMMO` and slot 453 (`0x102ad140`, `0x10387520`) adds none
   of `NOT_FACING_ATTACK 0x61`, `WEAPON_THROUGH_WALL 0x3c`, `TOO_CLOSE_FOR_RANGED 0x08`; the mask
   test is `HasInterruptCondition 0x10269d30` over `CacheInterruptConditions 0x1026a0f0`'s mask;
   known red 4's 99 s were `TASK_RANGE_ATTACK1` not finishing.
   - Staging: `range_bands`'s cast, seat and seed (`arena_shooter`, `npc_VHumanCombatant`,
     `TutorialThug`, `item_w_thirtyeight`, `hint_groups 2`), `duration` 30.
   - Script: `t 6.0` the player 96 cm in front (`range_bands`'s first teleport) so `0x08` selects
     `0xef` / `0xf0`; then, `after` the label `steps_back` + `delay 0.3`, the player teleported
     **behind the shooter's back, more than 100 units from it** (first-match in slot 365
     `0x1024f670`: `d < 100` answers `0x08` before the dot is asked; pick the point from the
     arena's places, `Arena/README.md`, and state it in `notes`); then, `after` the label
     `reselects` + `delay 0.5`: `fire arena_shooter TakeDamage 1` (the form
     `world/input_takedamage.json` uses).
   - `expect`, in order: `steps_back` (`schedule`, `^SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1 \(`,
     `by 12`; the 25 % `0xf0` draw of `0x102b7f40` makes the run seed-bound — keep `seed 1`, and
     if that seed draws `0xf0`, move the seed, not the regex); `not_facing` (`cond+`,
     `NOT_FACING_ATTACK (0x61)`, `within 2.0`); `runs_on` (`taskdone`, `^task_step_back$` or the
     program's last `task_wait_attack_time1`, `within 6.0` — the program reached its end);
     `reselects` (`schedule`, any ranged program, `within 3.0`); `hurt_breaks` (`break`,
     `LIGHT_DAMAGE`, `within 1.5`).
   - `never`: `break` matching `NOT_FACING_ATTACK \(0x61\)|WEAPON_THROUGH_WALL \(0x3c\)|TOO_CLOSE_FOR_RANGED \(0x8\)`
     on `arena_shooter`, whole run (no ranged program lists any of them); `death` on the shooter.
   - If `LIGHT_DAMAGE` is not raised by a 1-point `TakeDamage`, raise the amount until the
     trace shows `cond+ LIGHT_DAMAGE`; that is staging, not a loosening.

   **`ranged_fake_reload`** — README §1.2. `about`: `m_iFakeReloadCount +0x65f0` re-rolled by
   `0x102c54c0` from the template's `NpcFakeReloadCountMin` / `Max` (`0x101d3f10`: default 8, Max
   defaults to Min; `TutorialThug` authors 6), counted down per `FireBullets` (`0x10268919`); below
   1 with a `0x6000` weapon the pre-pass `0x102b8620` answers `0xc4` (`0xc6` with a hint);
   `sched_troika_hide_and_fake_reload1.sch` fails over to `…_FAKE_RELOAD2` (`FACE_ENEMY`,
   `PLAY_SEQUENCE ACT_RELOAD_FAST`) when no cover is found; no `TASK_RELOAD`, the clip untouched.
   - Staging: `ranged_sustained_fire`'s cast, seat and seed; `duration` 60.
   - `expect`, in order: `engages` (`START_COMBAT`, `by 3`); `shot_1` … `shot_6` (`animevent`,
     `^3031( |$)`, `within 8` each); `fake_reload` (`schedule`, `\(0xc4\)$`, `within 6`);
     `reload_in_place` (`schedule`, `\(0xc5\)$`, `within 8`; the arena's `hint_groups 2` leaves no
     cover, so `TASK_FIND_COVER_FROM_ENEMY` fails over); `reload_clip` (`task`,
     `task_play_sequence (`, `within 2`); `reload_done` (`taskdone`, `^task_play_sequence$`,
     `within 6`); `resumes` (`animevent`, `^3031( |$)`, `within 10`).
   - `never`: `cond+` `NO_PRIMARY_AMMO (0x40)`; `task` `task_reload`; `schedule` `\(0xc2\)$|\(0xc3\)$`;
     `death`. In `notes`: the count is per bullet set, and the .38's shot is one set; a seventh
     `3031` before `0xc4` is the arm test's (`Elysium.Arm.Weapon.FakeReloadCountPerSet`), the
     record's order cannot forbid it.
   - Probe at `end`: `alive`.
4. **Record corrections, each with its retail source.** `ranged_sustained_fire`: `shot_7`'s
   `within` widened to what the trace of `ranged_fake_reload` measures for `0xc4` + `0xc5` (state
   the two times in `notes`); one sentence in `about` (§1.2); its three `never` unchanged. No
   other record's expectation is edited unless its trace shows a fake reload inside a window the
   record wrote without one — then the same correction, the same sentence.
5. **Run, by name, once**: `uv run elysium arena ranged_fake_reload ranged_step_back_holds
   ranged_sustained_fire ranged_open_fire range_bands cover cover_armed cover_move_shoot
   ranged_friend_in_line_of_fire control_sequence`.
6. **Acceptance.**
   - `ranged_fake_reload`: green; `known_red` removed. Red on `reload_clip` / `reload_done` → read
     the `sequence` line: no sequence for `ACT_RELOAD_FAST` under the held weapon is triaged
     (README §7), not patched.
   - `ranged_step_back_holds`: green before and after the build.
   - `ranged_sustained_fire`, `ranged_open_fire`, `range_bands`, `cover`, `cover_armed`,
     `cover_move_shoot`, `ranged_friend_in_line_of_fire`, `control_sequence`: green. A moved timing
     is read against §1.2 (the count), §1.4 (slot 513 now carries the weapon's bits; the aim
     gates open) and triaged under the bug protocol, never loosened. If a verdict moved by lane 3
     cannot be read as retail inside the build cap: revert lane 3's one body
     (`ActiveWeaponCapabilityWord`), place the red, keep lanes 1 and 2.
7. **Tests, once each**: `uv run elysium test` (the default tier); then the arm tier
   (`uv run elysium test arm`) — V5 closes here, so the whole tier runs once, not prefixes.
8. **The full arena once, after the last build**: `uv run elysium arena`. Every record green
   before the wave stays green; each moved verdict gets a row (record, before, after, why) in the
   commit message's table; a new red is placed under the bug protocol (triage row, `known_red`),
   not parked.
9. **Ledger, triage, spec** (V5 closes): `kernel --check`, the override census and `unported.tsv`
   regenerated once; no verdict row is expected to move (`0x102b8620`, `0x102c54c0` are rule rows;
   `0x10255050`, `0x102552c0`, `0x10253ab0`, `0x101d3f10` are outside the kernel closure — take no
   row unless `--check` asks for one). `stories/v1/triage.md`: row "4 `0xef` sink" closed with the
   record and the sentence "not a mask defect: the text of `0xef` lists neither condition; the
   99 s were `TASK_RANGE_ATTACK1`"; J12's line amended ("an NPC fakes a reload after
   `NpcFakeReloadCountMin..Max` sets; `NO_PRIMARY_AMMO` still cannot rise from firing"). `spec.md`:
   tick V5 with a short landed note (V5a, V5a-3, V5b; records; what is proven by arm tests only:
   the real reload's finish); known red 4 struck. **For the judge, in your report**: the real
   reload proven by arm tests only (README §5); Presence filed with 0006 (§1.5); the unread bytes
   `0x1026a233..0x1026a29f` (§7).
10. **Commit once**, staging by explicit path (never `git add -A` / `.`), on `spec-0002/step-2`,
    **never push**: `fix(npc): V5b -- the NPC's fake reload (0x102b8620, 0x102c54c0, 0x10268919),
    the reload finish (slots 322 / 323), slot 360 behind the Motor seam, red 4 closed by its
    record`. The message body carries the verdict table (record, before, after, `known_red`
    lane), the tier totals, the builds' wall times, the tests corrected and why, the seams left
    with their retail field. **No `report*.md` file**: the table lives in the commit message and
    your report.

Rules: a build or a run blocks until done — wait for it or its completion notification, never a
sleep or polling loop. Query budget: 10 s warns (log to
`$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops, never retried as-is; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. Report ≤300 words: the builds' wall times,
each record's verdict before and after, the tier totals, what is left red and where it is placed,
the three judge items.
