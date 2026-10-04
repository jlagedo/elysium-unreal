# Brief — V4o's integrator

Runs after O1, O2 and O3 report (their reports are given to you). **V4o runs after V4b** (amended
after settling packets S2 and S3, 2026-10-04: the shot on the move needs the running body to turn
toward the enemy, V4b lane B2's). Read `README.md` here, the three
briefs, `stories/v4/packets-S3.md` item 4, `stories/v1/triage.md` § "Judge's rulings, V4" J5,
`Arena/README.md`.

**Final for the code as landed (2026-10-04, after V5a, V4a, V4b and V11;
`../v4/packets-S11.md` item 2, `../v4/packets-S12.md`).** Where a step below and this block
disagree, this block wins. Read both packets.

- **Order**: V4o starts only after **V11's commit** (it rewrote the melee contact in O3's
  `ElysiumWeaponClasses.cpp`). V4a's names exist as landed: `ElysiumAnimEvents::DispatchBase` /
  `DispatchLayer`, `FElysiumSequenceWords`, `FElysiumNpcBase::DispatchAnimEvents` with the
  `OverlayLayerWords` seam; there is no `DispatchOverlayLayerEvents` to look for (O1 fills the
  seam).
- **Your own lane, before the one build — the relative `never` window** (S11 item 2.4, judge
  item 2b: implement it; the same build). The harness has `never` with `from` / `after` (+
  `delay`) / `until` / `at_most` and **no window that closes relative to its label**
  (`Arena/README.md` § `never`). Add **`within`** to a `never` entry: only with `after`; the
  window closes `within` seconds after it opens (the label's match + `delay`); `within` and
  `until` exclude each other; the failure's `reason` prints the resolved bounds. Files (yours,
  in no coder's list): `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.{h,cpp}` (the reader:
  the `never` field list ~:472-474 and its checks ~:432-460),
  `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}` (the window's close),
  `Arena/README.md` (the table row), and two self-tests `Arena/scenarios/_selftest/
  never_within_holds.json` / `never_within_trips.json` in the shape of `never_after_ignores`.
  A `never` whose `after` label matches more than once keeps the first match, as today — so one
  entry pins one wait; say so in the README row.
- **Tunables row** (step 1, unchanged): `DebugAllowMfTurn` — `debug_allow_mf_turn` "0", object
  `0x10923cf0`, the shape of row `10924f70 DebugAllowMoveFacing convar_i32 1` in
  `research/tooling/ghidra/driver/kernel_tunables.tsv` (value `0`; ctor `0x1028c7b0`; reader
  `0x102e8560` through `0x10923cf4`, `+0x2c` int) — then regenerate, before the build.
- **`cover_move_shoot`** (step 4): **the record on disk is the record.** A0 landed it with
  measured bounds and the nine tutorial rows in its `about` (labels `covers`, `moves`,
  `shoots_moving` within 2.6 s, `arrives`); do **not** overwrite it with README §5's JSON. Remove
  `known_red` when green; add README §5's end probe `player health less 100` only if a first run
  shows the player at 100 before the shot (O3's proof beyond the event). Staging stands for
  `thug_3` (S4 item d: nine `sp_tutorial_1` rows — `thug_3` .38; `Hunter1`, `sentry3`,
  `sabbat_redshirt_1`, `_2`, `_5`, `sabbat_redshirt_2_proxy` MAC-10; `mercenary_upstairs`
  Ithaca; `condotierre_upstairs` Steyr; none on the hub).
- **`ranged_open_fire`** (O3's stamp): raise `never taskdone ^task_wait_attack_time1$` from
  `until: 1.05` to **`until: 1.6`** — retail's earliest completion is 0.5 + 1.14 = **1.64 s**
  (`Shot 0x102387b0` `0x1023891b..0x1023895d` writes `+0x730 = max(old, now − frametime) + n ×
  rate`; `0x102c5730`'s draw at ~387 units is at least 0.36 s; S11 items 2.3, 2.4), the citation
  in `about`. Then pin the later waits with the new window: label the second wait (`waits_again`,
  a `task` expectation after `fired_again`) and add `{ kind: taskdone, match:
  ^task_wait_attack_time1$, after: waits, within: 0.5 }` and the same after `waits_again`.
  Remove `known_red` only when the record is green **with those entries** — today's
  unexpected-pass is the first-wait accident (S11 item 2.2).
- **`ranged_sustained_fire`**: green since V5a and **must stay green** — eight 3031, no `0x40`,
  no `task_reload`. A red here after O3 is O3's (the spend put back, or the stamp starving the
  shooter): fixed, not placed.
- **Acceptance, corrected**: "the burst pause is 0 (a seam)" is void — V5a landed
  `ActiveWeaponBurstPauseWords` with real values, so bursts on the move pause for
  `RandomFloat(PauseMin, PauseMax)`; back-to-back bursts would now be a defect. The first unmet
  order for `shot_on_the_move` / `shoots_moving` stands (slot 575, then `0x61`, then staging);
  B2's turn is landed (`MotorMoveFacing`), so `0x61` on the move is read against O2's step 9
  (the facing target not added) before it is placed anywhere.
- **Records to run** (step 5), by name: `cover_move_shoot cover cover_armed cover_reclaim
  range_bands ranged_open_fire ranged_sustained_fire ranged_friend_in_line_of_fire
  anim_footsteps_walk melee_swing chase_melee anim_player_weapon_event_firearm` and the
  `_selftest` family (the two new ones trip and hold). `melee_swing` / `chase_melee` are there
  because O3 edits V11-2's file: unchanged verdicts, or a break in O3's diff.
- **Arm prefixes**, added to step 5: `Elysium.Arm.MeleeSwingStep. Elysium.Arm.MeleeContact.`
  (V11-2's, must hold) beside `Elysium.Arm.Weapon.`; the weapon family's default-tier
  `Weapons.AnimEvent` is where O3 deleted the inverted estimate assertion.

0. **Before the coders start** (a check, no edit): V4a's names exist — README §4 "From V4a" —
   and V4b's commit has landed. A missing one is reported to the coordinator; the coders do not
   start around it. **File lists, by listing**: O1 — the two overlay slot-body files,
   `ElysiumNpcBaseAnim.cpp`, `ElysiumNpcBaseAnimEvents.cpp`, `ElysiumNpcAnim.{cpp,inl}`, its
   test; O2 — `ElysiumNpcBaseMaintain.cpp`, `ElysiumNpcBaseSenses10.{cpp,inl}`,
   `ElysiumNpcBaseMoveAndShoot.cpp`, **`ElysiumNpcMotor.cpp` (`ShouldMoveAndShoot` only)**, its
   two tests; O3 — `ElysiumWeaponClasses.{h,cpp}`, `ElysiumWeaponTests.cpp`. No file in two
   lanes.
1. **Apply the cross-lane lines** the coders reported, nothing more. Expected: **O2's tunables
   row** `DebugAllowMfTurn` (`debug_allow_mf_turn` "0", object `0x10923cf0`) — add it to
   `research/tooling/ghidra/driver/kernel_tunables.tsv` in the shape of `DebugAllowMoveFacing`'s
   row and regenerate (`uv run elysium research gen_kernel_tunables`, then its `--check`) before
   the build; `ElysiumNpcKernelTunables.h` is never hand-edited. **From O3 (J12, the judge's
   second sitting)**: the replacement comment for `FElysiumNpcBase::WeaponFinishReload`
   (`ElysiumNpcBaseRunTask.cpp` ~:259-264: the reload body as read, owner V5b — a comment, no
   behaviour); and, only if O3 found it missing, the NPC's equip value `max(Default_Size, 1)`
   (`ElysiumItemClasses.cpp` ~:111-113; `Inventory_Insert 0x10334e70`) — one line, or it is
   filed, not written.
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
   **`ranged_sustained_fire`** (J12; written red by A0, README §5 — write it from there if A0's
   commit lacks it): `known_red` removed if it turns green.
5. **Run, by name**: `uv run elysium arena cover_move_shoot cover cover_armed cover_reclaim
   range_bands ranged_open_fire ranged_sustained_fire anim_footsteps_walk`. Then the touched arm prefixes once:
   `uv run elysium test Elysium.Arm.NpcKernelOverlay. Elysium.Arm.NpcKernelMoveAndShoot.
   Elysium.Arm.Weapon. Elysium.Arm.NpcKernelSenses10. Elysium.Arm.NpcKernelAnimEvents.
   Elysium.Arm.NpcKernelAnim.`
6. **Acceptance.**
   - `cover_move_shoot`: green. Red on `shot_on_the_move` → read the trace **in this order**
     (S3 item 4; **there is no "V5: 0x4f never holds" placement — the port raises `0x4f`**):
     (1) **slot 575**: no overlay arm at all (no layer pushed, no aim-twin activity swap) means
     `ShouldMoveAndShoot` still reads the `ActiveWeaponCapabilityWord` seam — O2's item 0 did
     not land; an integration break, fixed here. (2) **`cond+ 0x61` (`NOT_FACING_ATTACK`)
     instead of `0x4f`** during the run: the body does not turn to the facing target the
     overlay's tail adds — placed on **V4b lane B2** (`0x102e1a83`) with the trace line,
     `known_red` "V4b B2: the running body does not turn to its facing target (0x61 on the
     move)". (3) `0x2f` / `0x66` or the range words: the staging, per README §5. Never loosened;
     any other first unmet is triaged under the bug protocol. The burst pause is 0 (a seam,
     `ActiveWeaponBurstPauseWords`): back-to-back bursts are expected, not a red.
   - `cover`: green, its `fired` shot still through the dispatcher. `cover_armed`: red on its own
     cause only. `cover_reclaim`: as before V4o. `range_bands`: verdict and trace free of any
     overlay shot — the note stands; the only native writer of flags2 `0x400` is Ming Xiao's
     (`0x10394b0b`), so an overlay shot there means the record selected `RUN_AWAY_FROM_ENEMY
     0xb9` or `TAKE_COVER_NO_AMMO`: read that first.
   - A second 3031 inside the weapon's cooldown commits nothing (O3; `Shot 0x102387b0`'s count):
     a trace with more `animevent 3031` lines than `damage` lines is retail, not a defect.
   - **`ranged_sustained_fire`** (J12): green — eight 3031 on the shooter, no `cond+
     NO_PRIMARY_AMMO (0x40)`, no `task_reload`. Red before the first shot → slot 363 (V4a) or
     the wait (V5a-2, N2): placed there. Red at the seventh shot (`0x40`, a reload) → O3's item
     3b did not land in the shared commit. **Every gunman now fires without running dry**:
     `cover`, `cover_armed`, `ranged_open_fire` and any record that timed a reload move — a
     record that expects a reload after emptying a gun is a record error (corrected with
     `Inventory_Insert 0x10334e70` / `Shot 0x102387b0` as its source), never a loosened one.
     `SCHED_TROIKA_TAKE_COVER_NO_AMMO` is no longer reachable by firing: a trace that shows it
     is read first.
   - Staging (S4 item d): the arena gunman stands for the tutorial's `thug_3`; the hub has no
     row that can run-and-gun, so no hub record proves the overlay.
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
