# Brief — V4o's integrator

Runs after O1, O2 and O3 report (their reports are given to you). Read `README.md` here, the three
briefs, `stories/v1/triage.md` § "Judge's rulings, V4" J5, `Arena/README.md`.

0. **Before the coders start** (a check, no edit): V4a's names exist — README §4 "From V4a".
   A missing one is reported to the coordinator; the coders do not start around it.
1. **Apply the cross-lane lines** the coders reported, nothing more.
2. **The generated slots.** In `research/tooling/ghidra/driver/kernel_verdicts.tsv` retarget rows
   `10099020`, `10099660`, `10099540`, `10099470` to `seam:FElysiumAnimatingOverlay::SetLayer` /
   `RemoveLayer` / `HasLayer` / `AllocateLayer` (look each up with `uv run elysium research rows
   kernel_verdicts.tsv address=<addr>`; the shape to copy is row `100994c0`), then
   `uv run elysium research gen_kernel_shape` once. If the generator refuses, stop and report.
3. **Build once**: `uv run elysium build --arm`. Fix only integration breaks. A second build means
   stop and report.
4. **The record.** Make `Arena/scenarios/combat/cover_move_shoot.json` exactly README §5's (create
   it if A0's seam did not), `known_red` removed. Add the one `about` sentence to `cover`,
   `cover_armed`, `cover_reclaim`. No other expectation moves.
5. **Run, by name**: `uv run elysium arena cover_move_shoot cover cover_armed cover_reclaim
   range_bands ranged_open_fire anim_footsteps_walk`. Then the touched arm prefixes once:
   `uv run elysium test Elysium.Arm.NpcKernelOverlay. Elysium.Arm.NpcKernelMoveAndShoot.
   Elysium.Arm.Weapon. Elysium.Arm.NpcKernelSenses10. Elysium.Arm.NpcKernelAnimEvents.
   Elysium.Arm.NpcKernelAnim.`
6. **Acceptance.**
   - `cover_move_shoot`: green. Red on `shot_on_the_move` with no `cond+ CAN_RANGE_ATTACK1` during
     the run → placed on V5 with the trace line (`known_red` "V5: 0x4f never holds on the move"),
     never loosened; any other first unmet is triaged under the bug protocol.
   - `cover`: green, its `fired` shot still through the dispatcher. `cover_armed`: red on its own
     cause only. `cover_reclaim`: as before V4o. `range_bands`: verdict and trace free of any
     overlay shot (one would mean a native writer of flags2 `0x400`: read it first).
   - `ranged_open_fire`, `anim_footsteps_walk`: as V4a left them.
7. **Close**: the default tier once (`uv run elysium test`), the whole arena once
   (`uv run elysium arena`). Every record that passed before V4o still passes; a moved verdict is
   triaged against retail (the random stream moved for every `TAKE_COVER_HINT` run), never loosened.
   Then the ledger step (`kernel --check`, the override census, `unported.tsv`), regenerated once.
8. **Docs**: in `stories/v4/README.md` §2 M15 and `stories/v1/triage.md` J5, one line each: "V4o
   ported it". In `docs/specs/0015-weapon-overlays/spec.md` story 3, one line: the NPC per-layer
   dispatch landed in 0002 V4o.
9. **Commit once**, staged by explicit path (never `git add -A`), on `spec-0002/step-2`:
   `fix(npc): V4o -- the NPC overlay layers and the move-and-shoot shot from the layer's 3031`,
   the message body carrying the verdict table (record, before, after, why). Do not push. Write no
   `report*.md` file: the table lives in the commit message and your reply.

Rules: a build or a run blocks until done — wait for it or its completion notification, never a
sleep or a polling loop. The query budget (10 s warns, 60 s stops; never a file over ~200 KB
whole). Text through Grep / Read / Glob. Reply ≤300 words: the build's wall time, each record's
verdict before and after, the family and tier totals, what is left red and where it is placed,
and for V4c's C1: what O3 says it can now delete.
