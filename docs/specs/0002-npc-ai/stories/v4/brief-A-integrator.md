# Brief — V4a's integrator

**Final (amended after V4r, 2026-10-04).** Runs after A1, A2, A3 and **A4** report (their reports
are given to you). Read `README.md` here, the four briefs, `packets-R1.md`, `packets-R2.md`, the
rulings J1, J3, J4, J5 (`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it),
`stories/v1/triage.md` § "The known reds" (3) and § "The fix order", `Arena/README.md`.

1. **Apply the cross-lane lines** the coders reported, nothing more. Expected ones: A1/A4's
   mismatch on the dispatcher's signature, if any (README § "Shared names" is the authority);
   A4's line for `ElysiumWeaponClasses.cpp` (`CommitArrivesFromAnimEvent`'s read of the poll's
   cursor); the stale comments A1 lists (`Visual/ElysiumEntityBodies.cpp`) and A2 lists
   (`Public/ElysiumAnimationIntent.h`).
2. **Build once** (`uv run elysium build`). Fix only integration breaks (compile, link, a wrong call
   across the lanes). This is V4a's second build (the seam's was the first); a third means stop and
   report.
3. **Run, by name**: `uv run elysium arena sense_enemy_facing_me ranged_open_fire anim_footsteps_walk
   anim_player_footsteps anim_player_weapon_event anim_prop_event script_walk_to_mark
   cover cover_move_shoot control_sequence range_bands patrol_sentry2_pingpong face_enemy_turn`
   (use the two weapon records' actual names). Then the family filters once:
   `uv run elysium test Elysium.Arm.NpcKernelAnimEvents. Elysium.Arm.NpcKernelAnim.
   Elysium.Arm.NpcKernelMotor. Elysium.Arm.CombatCharacter. Elysium.Arm.NpcKernelConditions.
   Elysium.Arm.Player. Elysium.Arm.CameraAnimated.`
4. **Acceptance** (README §4, V4a):
   - `sense_enemy_facing_me`: green, `known_red` removed.
   - `ranged_open_fire`: no `BEHIND_ENEMY`; `shot_event` met — `animevent 3031` from the shooter
     inside `task_range_attack1`, emitted by the kernel dispatcher — and no `damage` on the player
     before it. The record then fails at N2's `never taskdone task_wait_attack_time1 until 1.05`
     (V5): retarget `known_red` to N2 alone with the trace line. Any other first unmet is triaged
     under the bug protocol.
   - `anim_footsteps_walk`, `cover`, `control_sequence`: green. `cover`'s shot must come through the
     dispatcher (the `animevent` line in its trace); if it went red, read why before anything else —
     the look-ahead finish (README §8 Q5) or a missing event row.
   - **`anim_player_footsteps`**: green — 2050 then 2051 `who: player` while walking, none
     standing. **`anim_player_weapon_event`**: the firearm record green (3031 on the player before
     its `damage`); the melee record green (`never animevent` in 3000..0xfa2, the `damage` still
     landing). A player shot whose timing moved is retail's look-ahead (README §8 Q10): triaged,
     never loosened. `known_red` removed from the three.
   - **`anim_prop_event`**: green — the prop animates and no `animevent` comes from it (the poll
     is gone; retail's `0x10190850` never dispatches). `known_red` removed. If the seam wrote it
     on a prop that authors no event, it says so in `notes` and proves nothing: leave it, say so.
   - **`script_walk_to_mark`** (N19, J1): `plays` `seq 0 rate=1`; **re-measure the timing after
     it** and correct the record's later deadlines from the measured retail-shaped run, the
     arithmetic in `about`; `known_red` removed.
   - **`cover_move_shoot`**: still red on its `known_red` ("0015 (the NPC overlay stack); the wire
     0002 R3"). It must not go green in V4.
   - `face_enemy_turn`, `patrol_sentry2_pingpong`: still red (V4b); note whether the turn's or the
     legs' times moved. A2's `+0x560` does not fix the turn (J9).
   - **The camera** (J4): `Elysium.Arm.CameraAnimated.ThinkOrder` green. No step-2 arena record
     stages an animated camera by name; Grep `Arena/scenarios` for one that runs a cutscene camera
     and, if one exists, run it and name it in the report — its `OnCameraComplete` moves 0.1 s of
     clip time earlier, which is retail's.
5. **Story close** (V4a is a story): the arm tier once (`uv run elysium test arm`), the whole arena
   once (`uv run elysium arena`), the default tier once (`uv run elysium test`); then the ledger step
   (method step 6: `kernel --check`, the override census, `unported.tsv`, regenerated once — the
   seam's shape-map rows change the generated bindings). Every record that passed before V4a still
   passes; any verdict that moved gets a line in `stories/v4/report-a.md` (record, before, after,
   why) — a moved timing is triaged against retail, never loosened. `report-a.md` also lists, for
   the owner: the one named seam (the player's cycle read from the pose layer's phase; its clock
   filed to 0015 layer 0) and the player's melee sweep still at the map actor's tick.
6. **Commit once**, when the build, the default tier and the records above are green or carry a
   placed red: `fix(npc): V4a -- slot 258 in PostRun over the baked event table, the player's and
   the camera's dispatch, the world poll deleted, the clock's speed words, sequence 0, slot 363 on
   the combat character`. Tick nothing in `spec.md` (V4 ticks at V4c). Do not push.

Rules: a build or a run blocks until done — wait for it or its completion notification, never a
sleep or a polling loop. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole).
Text through Grep / Read / Glob. Report ≤300 words: the build's wall time, each record's verdict
before and after, the family and tier totals, what is left red and where it is placed.
