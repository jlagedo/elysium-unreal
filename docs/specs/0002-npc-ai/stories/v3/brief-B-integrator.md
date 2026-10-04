# Brief — V3b's integrator

Runs after B1 and B2 report. Read `README.md` here, both briefs, `stories/v1/triage.md` § "The known
reds" (6, "wider than written"), `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **Build once** (`uv run elysium build`). Fix only integration breaks. A second build is allowed; a
   third means stop and report.
3. **Run, by name**: `uv run elysium arena places_pedestrian_visit places_thug_pt1
   input_useinteresting map_hub_idle hub_crosswalk_wait rollcall_vhuman rollcall_vhumancombatpatrol
   map_tutorial_sneak_past patrol_sentry2_pingpong patrol_monk_loop control_sequence`. Family filters
   once: `uv run elysium test Elysium.Arm.NpcKernelMaintain. Elysium.Arm.NpcKernelHints.
   Elysium.Arm.NpcKernelSelect19. Elysium.Arm.NpcKernelStartTask19. Elysium.Arm.NpcKernelRunTask19.
   Elysium.Content.Places. Elysium.Arm.PlaceSeams. Elysium.Arm.PlaceSet.`
4. **Acceptance** (README §4, V3b): the eight place records green, `known_red` removed;
   `map_tutorial_sneak_past`'s first half (`setup`, `at_place`) met — its hearing half stays red:
   retarget its `known_red` to V12/R1's footstep producer with the trace line. Re-check: the pedestrian
   runs `0xff → 0x100 → DO_INTEREST_ACTIVITY` and its place is released only at a schedule change
   without `PRESERVE_PATH` (`hub_crosswalk_wait`: the red-curb wait keeps the place). V3a's records
   stay green.
5. **Story close**: the arm tier once, the whole arena once, the default tier once; the ledger step
   (`kernel --check`, the override census, `unported.tsv`). Verdicts that moved go in
   `stories/v3/report-b.md` (record, before, after, why). A new game red is filed in
   `stories/v1/triage.md` under the bug protocol; you do not fix game code beyond integration.
6. **Commit once**: `fix(npc): V3b -- places and patrols as programs; the ambient executor retired`.
   Do not push.

Rules: wait on a build or a run by its completion notification, never a sleep or a polling loop.
The query budget (10 s warns, 60 s stops; never read a file over ~200 KB whole). Text through Grep /
Read / Glob. Report ≤300 words: the build's wall time, each record's verdict before and after, the
family and tier totals, what is left red and where it is placed.
