# Brief — V4c's integrator (and V4's close)

**Final (amended after V4r, 2026-10-04 — J2, J6, J8).** Runs after C1 and C2 report. Read
`README.md` here, both briefs, `packets-R2.md`, the judge's rulings J2, J6, J8
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it), `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more. Expected: C2's K4 line in
   `ElysiumWeaponClasses.cpp` (`FElysiumWeapon::BuildActivityClipRequest`'s `Variant`); a
   mismatch between C1's call and C2's body of `SequenceBounds` (README § "Shared names" is the
   authority); a `RemoveFlag2(4)` writer or a `m_flDesiredMoveYaw`-style word a lane found
   missing outside its files.
2. **Build once** (`uv run elysium build`). Fix only integration breaks; a third build means stop.
3. **The one body-data import** (J2), after the build is green: one character import
   (`uv run elysium import <the character lane>`; body data only changes — about 20 minutes,
   unmeasured; **capped at one**; wait on its completion notification, and time it). It is a
   bake, not a query, but log its wall time in the report. **Stop rule: if the import reports any
   animation package rebuilt rather than reused, stop** — the body data's fingerprint
   (`pipeline/unreal/import_characters.py`, `fingerprint("body-data", …, bodyDataSha256)`) was
   meant to leave `clipDataSha256` and the animation packages untouched. Then: revert nothing in
   code, file the data half for V4d's bake window in `stories/v4/report-c.md`, and the slot
   stands on `SequenceBounds` answering false (it writes nothing, as retail with no seqdesc)
   until that bake. If the import reuses every animation package, read the result live: lab
   (`uv run elysium gr --arena --headless`, `elysium.gr_scenario ranged_open_fire`),
   `elysium_entity_get` on the shooter — `attack_extents` **before and after a sequence change**;
   the two values and the sequences go in the report. No step-2 record observes the extents:
   say so, do not hide it.
4. **Records** (you own `Arena/` edits): `melee_swing` and `ranged_open_fire` were corrected by
   the seam (J7) — check they still say so, change nothing else. Remove a `known_red` only where
   the acceptance below turns its record green.
5. **Run, by name**: `uv run elysium arena damage_lethal_death verbs_stealth_kill ranged_open_fire
   cover range_bands melee_swing chase_melee control_sequence verbs_feed_trance cover_move_shoot`.
   Then the family filters once: `uv run elysium test Elysium.Arm.NpcKernelAnim.
   Elysium.Arm.NpcKernelAnimEvents. Elysium.Substrate.NpcCombat. Elysium.Arm.NpcKernelRunTask19.
   Elysium.Weapon` (use the weapon tests' actual prefix).
6. **Acceptance** (README §4, V4c):
   - `damage_lethal_death`, `verbs_stealth_kill`: the death transaction green in the trace
     (`death`, `OnDeath` on the kill tick, `corpse ragdoll`, no `move` / `task` / `schedule` after
     death — an ordinary kill never reaches `SCHED_DIE`); the records themselves stay
     `expected-fail` on `corpse_on_floor` with `known_red` "V4d" until V4d lands (if V4d landed
     first, they are green).
   - `cover`, `ranged_open_fire`: the shot still comes from the event (`animevent 3031` inside
     `task_range_attack1`), no `damage` before it; `ranged_open_fire` still red only on N2 (V5).
   - `melee_swing`, `chase_melee`: red only on N3 (V11); `melee_swing`'s trace shows no
     `animevent` on the brawler during a swing.
   - `cover_move_shoot`: still red on its `known_red` (0015; R3).
   - `verbs_feed_trance`, `control_sequence`: green (a moved verdict is triaged).
7. **The silent-class list** (J6, part (c)): from the baked event tables, list every NPC class
   whose ranged attack activity has **no clip with an event in 3030..3044** — query the staged
   clip sidecars by event id (a lookup per bank; never read a sidecar over ~200 KB whole; 60 s
   stops). Add C1's list of classes left on the "unread operator body" seam, and the three NPC
   bodies that call a weapon's slot 326 directly (the Troika melee arm, `CNPC_VBach::StartTask
   0x103645a0`, `CNPC_VManBat::RunTask 0x1038d130`). Write it as § "The reading owed" in
   `stories/v4/report-c.md` and file it to V5 (a line under V5 in `stories/v1/triage.md` § "The
   fix order"). Check the run's log for C1's Warning ("an NPC attack clip with no fire event"):
   each (model, sequence) it named goes in the list.
8. **V4's close**: the arm tier once (`uv run elysium test arm`), the whole arena once, the default
   tier once; the ledger step (`kernel --check`, the override census, `unported.tsv`; the verdict
   row for `0x102e19e0` if V4b left it); `divergences.md` row 4 marked closed with the commit;
   `spec.md` V4c ticked (V4 itself only when V4d has also landed) with a short landed note in the
   V3 style (sub-stories, records turned green, what moved where); `stories/v1/triage.md` § "The
   fix order" V4 row struck through as V3's is. Moved verdicts in `stories/v4/report-c.md`, with
   K4's open point for the owner (which port stream stands for the shared engine stream at the
   non-NPC pick sites) and the driver's variant-keyed cache.
9. **Commit once**: `fix(npc): V4c -- the NPC shot from its event, Weapon_FrameUpdate, the melee
   sweep in slot 312, slot 247 and the sequence bbox, the weighted pick, SetDisposition, the
   die-ragdoll seed`. Do not push.

Rules: wait for a build, an import or a run by its completion notification, never a sleep or
polling loop. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text
through Grep / Read / Glob. Report ≤300 words: the build's and the import's wall times, whether
every animation package was reused, verdicts before and after, totals, the silent-class list's
size, what is left red and where it is placed.
