# Brief — V3a's integrator

Runs after A1 and A2 report (their reports are given to you). Read `README.md` here, both briefs,
`stories/v1/triage.md` § "The known reds" (1) and § "The fix order", `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **Build once** (`uv run elysium build`). Fix only integration breaks (compile, link, a wrong call
   across the two lanes). This is V3a's second build; a third means stop and report.
3. **Run, by name**: `uv run elysium arena cover patrol_sentry2_pingpong patrol_monk_loop
   input_clearpatrolpath control_sequence`. Then the family filters once: `uv run elysium test
   Elysium.Arm.NpcKernelStartTask19. Elysium.Arm.NpcKernelScript19. Elysium.Arm.NpcKernelSelect19.
   Elysium.Substrate.NpcCombat. Elysium.Arm.NpcKernelAnim`.
4. **Acceptance** (README §4, V3a):
   - `cover`: the trace shows `sequence <the cover-out clip> rate=1` after `task_play_cover_outof`,
     then `seqfinished` and `taskdone task_play_cover_outof` (`outof_done`). If the record then
     fails at `atk` / `fires` / `fired`, that is not V3's: retarget its `known_red` to the red the
     trace shows (red 3, `DispatchAnimEvents`, V4; or N2, V5) with the trace line in
     `stories/v3/report-a.md`. If it is green, remove `known_red`.
   - The three patrols: green, `known_red` removed. A residual red gets its retail chain read and
     placed under the bug protocol (review doubt 1), in `stories/v1/triage.md`; you do not fix game
     code beyond integration.
   - `control_sequence` stays green.
5. **Story close** (V3a is a story): the arm tier once (`uv run elysium test arm`), the whole arena
   once (`uv run elysium arena`), the default tier once (`uv run elysium test`); then the ledger step
   (method step 6: `kernel --check`, the override census, `unported.tsv`, regenerated once, as the
   H wave's integrator ran them). Every record that passed before V3a still passes; any verdict that
   moved gets a line in `stories/v3/report-a.md` (record, before, after, why).
6. **Commit once**, when the build, the default tier and the records above are green or carry a
   placed red: `fix(npc): V3a -- the kernel's sequence plays on every body; the program claim
   retired`. Tick nothing in `spec.md` (V3 ticks at V3d). Do not push.

Rules: a build or a run blocks until done — wait for it or for its completion notification, never
a sleep or a polling loop. The query budget (10 s warns, 60 s stops; never read a file over
~200 KB whole). Text through Grep / Read / Glob. Report ≤300 words: the build's wall time, each
record's verdict before and after, the family and tier totals, what is left red and where it is
placed.
