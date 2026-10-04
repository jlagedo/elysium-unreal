# Brief — V4c's integrator (and V4's close)

**Final (amended after V4r, settling packets S1–S4 and the judge's second sitting, 2026-10-04 —
J6, J8; J2b, J11, J13, J14).** **The body-data import (step 3) is struck: J2b withdrew J2's import
and filed it with the acquire cone's story. The slot-247 body stays in C1**, on the named
`SequenceBounds` seam, and lands writing nothing — proven by its arm test only. **You gain four
corpse records** (J13). Runs after C1 and C2 report. Read
`README.md` here, both briefs, `packets-R2.md`, `packets-S1.md` items 1–4, `packets-S2.md` items
1, 7, 9, `packets-S4.md` items a, e, f.1, the second sitting (`stories/v1/triage.md` § "Judge's
rulings, V4 — second sitting"; Grep, read only it), the judge's rulings J6, J8
(`stories/v1/triage.md` § "Judge's rulings, V4"; Grep, read only it), `Arena/README.md`.

**Final for the code as landed (2026-10-04; `packets-S12.md`).** V4c runs **after V11 and after
V4o**. Where a step below and this block disagree, this block wins.

- **Cross-lane lines, added to step 1**: (a) the world tick's sweep goes — delete the call
  `EntityWorld->AdvanceMeleeSwings(DeltaSeconds)` (`Map/ElysiumMapActor.cpp` ~:2630) and the
  declaration (`Public/ElysiumEntityWorld.h` ~:432), and correct the comment in
  `Visual/ElysiumMeleeTrail.h` ~:19; C1 deleted the definition. (b) Check that **both halves of
  the player's sweep landed**: C1's slot 315 body and C2's one call at the tail of
  `FElysiumPlayer::PostThinkAnimation`; with only one, the player's swing hits nothing. (c) If
  C2 reported that `SelectHeaviestSequence` (`ElysiumNpcBaseHelpers2.cpp`) or
  `SelectWeightedSequenceForActivity` (`ElysiumAnimatingOverlaySlotBodies.cpp`) does not forward
  to the new pick, its line. (d) The maker's spawnflag bit 9 (`MakeNPC 0x1034b7b0`,
  `AiInfra/ElysiumNpcMakerActor.h` / `Map/ElysiumMapActor.cpp`) if C2 found it missing.
- **Before the build — the generated slots** (as V4a did for 242 / 248 and V4b for 389): C1
  wrote slot 315 in `ElysiumCombatCharacterSlotBodies.cpp` and slot 247 in
  `ElysiumAnimatingSlotBodies.cpp`. In `research/tooling/ghidra/driver/kernel_verdicts.tsv`
  retarget rows `10346cd0` and `10090c80` to `hand:FElysiumCombatCharacter::MeleeSwingUpdate` /
  `hand:FElysiumAnimating::SetAttackExtentsForSequence` (look each up with `uv run elysium
  research rows kernel_verdicts.tsv address=<addr>`; the shape is row `103338c0`), then
  `uv run elysium research gen_kernel_shape` once. Neither generated `*Slots.cpp` is hand-edited;
  where step 1 below names `ElysiumCombatCharacterSlots.cpp` as C1's, read the `…SlotBodies.cpp`.
- **Disjointness, by listing, as amended**: C2 gained `ElysiumPlayerEntity.cpp`
  (`PostThinkAnimation` only), `Tests/ElysiumPlayerPostThinkTests.cpp` and `ElysiumFeed.cpp`
  (`SelectGrappleSequence` only); C1 touches none of them. C1's diff in
  `ElysiumWeaponClasses.cpp` shows hunks only in the estimate (`CommitArrivesFromAnimEvent`, the
  commit time, `BeginRangedShot`'s NPC arm), `IsMeleeSwingTrigger` / `OperatorHandleAnimEvent`,
  `AdvanceSwingContact`'s entry and sub-step loop, and `ElysiumSwingEndpointsAt`'s body — none in
  `ShotFromAnimEvent`, `CommitQueuedAttack`'s magazine block (O3's), `MeleeContact`,
  `SwingWallContact`, `KnockbackContact` or the walk's filters (V11-2's).
- **Records, corrected against the tree**: the corpse records on disk are **five** —
  `corpse_removed_unseen`, `corpse_kept_seen`, `corpse_kindred_burns`, `corpse_pedestrian_stays`,
  `corpse_fades`; run all five. **`cover_move_shoot` is green since V4o** and must stay green
  (step 6's "still red on its `known_red`" is void). **`ranged_open_fire` is green since O3's
  stamp** with its bound at 1.6 s and the two relative `never` windows; "red only on N2 (V5)" is
  void. `ranged_sustained_fire` stays green. Add to step 5's run:
  `corpse_kept_seen anim_player_weapon_event_melee anim_player_weapon_event_firearm
  verbs_feed_victim_dispatch melee_ally_in_the_way ranged_sustained_fire`; and to the family
  filters the player's post-think tests (C2's `AliveGate`, `Slot312Order`) and
  `Elysium.Arm.MeleeSequenceChoice.`.
- **Acceptance, added**: `anim_player_weapon_event_melee` green — the player's swing is swept
  from `PostThinkAnimation`'s slot 312 stand-in, after slot 258 (`0x1016c316`), not from the
  world tick; `verbs_feed_trance` / `verbs_feed_victim_dispatch` green with the grapple cells
  picked by activity number (S12 d.1 item 6: one sequence per cell; a miss is `EndGrapple`); a
  dead player dispatches no `animevent` (S8 gate 3).
- **The silent-class list (step 7), corrected**: "each species class's `HandleAnimEvent` arm by
  arm" is **not owed** — the bodies are walked and ported (S12 item c). `0x10239f30` is settled
  (S5 item 5). What stays on the list: the Tzimisce melee's 3045 / 3046 if C1 left the seam, and
  every (model, sequence) C1's Warning named — expect at least a flamethrower wielder's
  `flamet_attack_layer`-family clips if a record reaches one (S12 a.1: the layer authors no 3031).
- **K4's open point** goes to the owner as a ruling, not a recovery (S12 item c).

1. **Apply the cross-lane lines** the coders reported, nothing more. Expected: C2's K4 line in
   `ElysiumWeaponClasses.cpp` (`FElysiumWeapon::BuildActivityClipRequest`'s `Variant`); a
   mismatch between C1's call and C2's body of `SequenceBounds` (README § "Shared names" is the
   authority); a `RemoveFlag2(4)` writer or a `m_flDesiredMoveYaw`-style word a lane found
   missing outside its files; **C2's wiring of the pedestrian's `CreateCorpse` (`0x103a38c0`) to
   slot 301** where it needs a line outside C2's files (a generated slot binding, a
   `kernel_verdicts.tsv` row for `0x1032c0e0`, a virtual's declaration); the state-7 fork's
   bone −1 (`ElysiumNpcBaseSelect.cpp:~273`) if C2 reported it; a word for the sweep's stamp
   (`m_flLastMeleeSwingUpdate +0xaa4`, `m_bMeleeSwingIsLive +0xaa1`) if C1 reported its home is
   not its file.
   **Wave disjointness, checked by listing before the build**: C1 and C2 share no file. By
   function: `ElysiumCombatCharacter.cpp` is C2's (`PlayReactionActivity`, `CreateCorpse`);
   `ElysiumCombatCharacterSlots.cpp` is C1's (slot 315); `ElysiumNpcAnim.cpp` is C2's
   (`SequenceBounds`, called by C1); `ElysiumWeaponClasses.{h,cpp}` is C1's (C2's K4 line is
   yours to apply); `ElysiumNpcPedestrian.{h,cpp}` and `ElysiumNpcKernelSpeciesMisc10Tests.cpp`
   are C2's, as is `ElysiumNpc.cpp` (C2's functions, now with `Think`'s committed-death gate)
   and `ElysiumNpcBaseRunTask.cpp` (`StartFadeOut` only); the bbox files (`ElysiumBodyData.*`,
   `ElysiumNpcClips.h`, `body_data.py`) are in **no** lane (J2b) and must show no hunk.
2. **Build once** (`uv run elysium build`). Fix only integration breaks; a third build means stop.
3. **STRUCK (J2b).** No import, no pipeline change, no live `attack_extents` read. The acquire
   cone (`0x1040f550`, `0x1040f080`) is not ported, so the port has no observer of the extents;
   the data half is filed with the story that ports it. In `report-c.md` write one line — "slot
   247's body landed on the `SequenceBounds` seam and writes nothing; proof: the arm test
   `Elysium.Arm.NpcKernelAnim.AttackExtents`; no arena record, stated (J2b)". The text below is
   the filed item's description only; do none of it.
   ~~**The one body-data import** (J2)~~, after the build is green: one character import
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
4. **Records** (you own `Arena/` edits): `melee_swing`, `chase_melee` and `ranged_open_fire` were
   corrected by the seam (J7; S3 item 8: the `_NR` schedule forms, one `MELEE_IDLE 0xc7` first is
   retail, `hit_event` → `damage`), and `damage_lethal_death` expects no schedule after the
   death (S1) — check they still say so, change nothing else. Remove a `known_red` only where
   the acceptance below turns its record green.
   **The four corpse records (J13)** were written red by the seam agent A0
   (`brief-A0-seam.md` item 7b, with the `removed` event kind, H22); if A0's commit lacks one,
   write it from that item. You run them and turn each green — `known_red` removed — where C2's
   work makes it so; a red one is read against its clock and placed, never loosened.
5. **Run, by name**: `uv run elysium arena damage_lethal_death verbs_stealth_kill ranged_open_fire
   cover range_bands melee_swing chase_melee control_sequence verbs_feed_trance cover_move_shoot
   corpse_removed_unseen corpse_kindred_burns corpse_pedestrian_stays corpse_fades`.
   Then the family filters once: `uv run elysium test Elysium.Arm.NpcKernelAnim.
   Elysium.Arm.NpcKernelAnimEvents. Elysium.Substrate.NpcCombat. Elysium.Arm.NpcKernelRunTask19.
   Elysium.Weapon` (use the weapon tests' actual prefix).
6. **Acceptance** (README §4, V4c):
   - `damage_lethal_death`, `verbs_stealth_kill`: the death transaction green in the trace
     (`death`, `OnDeath` on the kill tick, `corpse ragdoll`, no `move` / `task` / `schedule` after
     death — an ordinary kill never reaches `SCHED_DIE`: `CreateCorpse 0x1032c0e0` replaces the
     think on every NPC arm, and no step-2 record reaches the state-7 fork or the
     `ACT_DIERAGDOLL` seed, which is arm-tested only); the records themselves stay
     `expected-fail` on `corpse_on_floor` with `known_red` "V4d" until V4d lands (if V4d landed
     first, they are green). The death sound: **once** per kill in the trace.
   - **The four removal clocks (J13)** — an entity that is gone answers no name lookup and fires
     no output, so each is state the bytecode observes:
     - `corpse_removed_unseen` (ordinary mortal, `SUB_PVSRemove 0x102696f0`): with the player
       turned away, `removed` at death + 10 s; facing the corpse, still there at + 12 s. Green
       (the think is ported, `ElysiumNpc.cpp:~597-611`).
     - `corpse_kindred_burns` (Kindred or `Has_Burning_Death`, `SUB_Remove` at + 10 s
       unconditionally, `0x1032c32f`): a tutorial `npc_VVampire` by `from_map`, watched, gone at
       + 10 s. Green (the think is ported, `ElysiumCombatCharacter.cpp:~1518-1527`); the burn's
       look is a named seam filed to 0014, the burning-death sound is emitted. A maker child
       with the fade bit goes at about + 13.8 s instead (the later think wins): the record's
       row must not be one.
     - `corpse_pedestrian_stays` (`CNPC_VPedestrian::CreateCorpse 0x103a38c0`; C2 item 8): a
       hub pedestrian by `from_map`, the player turned away, still present at + 25 s. Green
       only with both halves of C2's item — the slot-301 wiring **and** `Think`'s gate
       (`ElysiumNpc.cpp:~645`: the cleared think must win over the committed-death
       `SUB_PVSRemove`).
     - `corpse_fades` (C2 item 10; `SUB_StartFadeOut 0x102695d0` → `SUB_FadeOut 0x10269960`): a
       child of the tutorial's `stealth_victim_maker`, watched, present at + 13 s and gone by
       + 14.5 s. Red with the corpse never removed → the maker does not pass spawnflags `0x204`
       (`MakeNPC 0x1034b7b0`): C2 reported that line; it is pulled from R6 and applied here if
       it is one line, else placed.
     `verbs_stealth_kill` kills exactly such a maker child: its corpse now fades — a moved
     verdict there is read against J14.1, not loosened.
   - `cover`, `ranged_open_fire`: the shot still comes from the event (`animevent 3031` inside
     `task_range_attack1`), no `damage` before it; `ranged_open_fire` still red only on N2 (V5).
   - *(planner, after S5.)* **V11 lands before V4c, as its own wave [V11-1, V11-2, V11-3]**:
     the contact's D1–D8, D10, D11 are V11-2's and already retail; C1 owns only D9, the
     sweep's place, the stamp and `Weapon_FrameUpdate`. C1's diff in `ElysiumWeaponClasses.cpp`
     must show no hunk in the walk's filters, `MeleeContact` or `KnockbackContact`; run
     `Elysium.Arm.MeleeSwingStep. Elysium.Arm.MeleeContact.` with the family tests. If C1
     reported D9 as a seam (no bone-at-cycle accessor), file the accessor with the judge.
   - `melee_swing`, `chase_melee`: green before you start (V11) and still green with
     the hit now landed by slot 315's sweep (C1): `…_MELEE_ATTACK1_NR (0xdd)` in reach,
     `…_MELEE_ADVANCE_NR (0xcb)` out of reach, one `MELEE_IDLE (0xc7)` first being retail;
     `melee_swing`'s trace shows no `animevent` on the brawler during a swing.
   - `cover_move_shoot`: still red on its `known_red` — the slot-575 seam
     (`ActiveWeaponCapabilityWord` answers 0; V4o lane O2), then the NPC's missing overlay layers
     (V4o O1). Not V5.
   - `verbs_feed_trance`, `control_sequence`: green (a moved verdict is triaged).
7. **The silent-class list** (J6, part (c)): from the baked event tables, list every NPC class
   whose ranged attack activity has **no clip with an event in 3030..3044** — query the staged
   clip sidecars by event id (a lookup per bank; never read a sidecar over ~200 KB whole; 60 s
   stops). **There is no "unread operator body" list to add** (`packets-S2.md` item 1: all eight
   slot-370 bodies are read; the estimate is removed for every NPC wielder; the list's first
   half is empty — if C1 reports a class it left on the estimate, that is a stop, not a list
   entry). Add the NPC bodies that fire outside a weapon's event, each already a task body in
   its class file: the Troika melee arm `0x102a1910`, `CNPC_VBach::StartTask 0x103645a0`,
   `CNPC_VManBat::RunTask 0x1038d130` (slot 326); `CNPC_VChangBros::RunTask 0x1036bfc0` →
   `SpawnEnergyBall 0x1036dd20`, `CNPC_VFrenzyShadow::StartTask 0x10375f50`, the Chang ghost
   `0x103f03e0` (slot 372 on a spawned projectile weapon). **J11**: ChangBros, FrenzyShadow,
   Bach and ManBat are verified absent from both witness maps and are filed on `spec.md`'s "on
   demand, not in the sequence" line, one row each — check the four rows are there (the
   coordinator adds them), add nothing to V5 for them; C1's Warning is their tripwire. Still
   owed and named in the list:
   each species class's `HandleAnimEvent` arm by arm, `0x10239f30` (the type-6 throw's launch),
   the Tzimisce melee's 3045 / 3046 if C1 left it a seam. Write it as § "The reading owed" in
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
   sweep in slot 312, slot 247's body, the weighted pick, SetDisposition, the die-ragdoll seed,
   the pedestrian's CreateCorpse, the death fade`. Do not push.

Rules: wait for a build or a run by its completion notification, never a sleep or
polling loop. The query budget (10 s warns, 60 s stops; never a file over ~200 KB whole). Text
through Grep / Read / Glob. Report ≤300 words: the build's wall time, slot 247 on its seam with
no import (J2b), verdicts before and after, totals, the silent-class list's
size, the four corpse records' verdicts and measured removal times, what is left red and where
it is placed.
