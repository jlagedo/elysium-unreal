# Brief — V4a's integrator

Runs after A1, A2 and A3 report (their reports are given to you). Read `README.md` here, the three
briefs, `packets.md`, `stories/v1/triage.md` § "The known reds" (3) and § "The fix order",
`Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **Build once** (`uv run elysium build`). Fix only integration breaks (compile, link, a wrong call
   across the lanes). This is V4a's second build (the seam's was the first); a third means stop and
   report.
3. **Run, by name**: `uv run elysium arena sense_enemy_facing_me ranged_open_fire anim_footsteps_walk
   cover control_sequence range_bands patrol_sentry2_pingpong`. Then the family filters once:
   `uv run elysium test Elysium.Arm.NpcKernelAnimEvents. Elysium.Arm.NpcKernelAnim.
   Elysium.Arm.NpcKernelMotor. Elysium.Arm.CombatCharacter. Elysium.Arm.NpcKernelConditions.`
4. **Acceptance** (README §4, V4a):
   - `sense_enemy_facing_me`: green, `known_red` removed.
   - `ranged_open_fire`: no `BEHIND_ENEMY`; `shot_event` met (an `animevent` from the shooter inside
     `task_range_attack1`, now emitted by the kernel dispatcher). The record then fails at N2's
     `never taskdone task_wait_attack_time1 until 1.05` (V5): retarget `known_red` to N2 alone with
     the trace line. Any other first unmet is triaged under the bug protocol.
   - `anim_footsteps_walk`, `cover`, `control_sequence`: green. `cover`'s shot must come through the
     dispatcher (the `animevent` line in its trace); if it went red, read why before anything else —
     the look-ahead finish (README §8 Q5) or a missing event row.
   - `patrol_sentry2_pingpong`: still red on N13 (V4b); note whether the trace's leg times moved.
5. **Story close** (V4a is a story): the arm tier once (`uv run elysium test arm`), the whole arena
   once (`uv run elysium arena`), the default tier once (`uv run elysium test`); then the ledger step
   (method step 6: `kernel --check`, the override census, `unported.tsv`, regenerated once — the
   seam's shape-map rows change the generated bindings). Every record that passed before V4a still
   passes; any verdict that moved gets a line in `stories/v4/report-a.md` (record, before, after,
   why) — a moved timing is triaged against retail, never loosened.
6. **Commit once**, when the build, the default tier and the records above are green or carry a
   placed red: `fix(npc): V4a -- slot 258 in PostRun over the baked event table, the clock's speed
   words, slot 363 on the combat character`. Tick nothing in `spec.md` (V4 ticks at V4c). Do not push.

Rules: a build or a run blocks until done — wait for it or its completion notification, never a
sleep or a polling loop. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Report ≤300 words: the build's wall time, each record's verdict
before and after, the family and tier totals, what is left red and where it is placed.
