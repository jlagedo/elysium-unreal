# Brief A0 — V4a's seam commit (one agent: writes the seam, builds once, commits once)

**Final (amended after V4r, the settling packets S1–S4 and the judge's second sitting,
2026-10-04).** Where this brief and an older packet or the README disagree, this brief wins:
S1–S4's changes and the second sitting's (J10, J12, J13, J14; `stories/v1/triage.md` § "Judge's
rulings, V4 — second sitting" — Grep, read only that section) are already written in below
(`packets-S1.md` items 1, 3, 5, 7; `packets-S3.md` items 4, 8; `packets-S4.md` items a, d, e, f.1
are the retail sources the records cite). **The second sitting gives you one harness kind (H22,
`removed`) and six new red records** (item 7b).
Read `README.md` here (all of it; §3 is your job),
`packets-R1.md`, `packets-R2.md`, `packets-spike.md` (findings 4, 5, 7, 8) and the judge's rulings
(`stories/v1/triage.md` § "Judge's rulings, V4": J2, J3, J4, J5, J7, J9 are yours; find the section
with Grep, read only it). You are the only V4a agent that writes and builds before the wave
(method step 1). **The seam changes no behaviour**: after your commit every record that passed
still passes and every V4 record is still red (`anim_footsteps_walk` may pass: it is a guard).
Re-locate every site by Grep on its name: V3c / V3d / T6b moved lines.

## Files (only these)

Substrate (`Source/ElysiumUE/Private/Substrate/` unless a path says otherwise):

- `ElysiumNpcBase.h` (the words; the slot-258 override's declaration)
- `ElysiumNpcBaseAnimEvents.cpp` (new: the override's body, forwarding to the base stub)
- `ElysiumNpc.h` (the bridge row accessors' declarations only)
- `ElysiumNpcAnim.cpp` (the accessors' bodies answering nothing)
- `ElysiumNpcPositions2.cpp` (`GroundSpeedCm()` reads the word)
- `Source/ElysiumUE/Public/ElysiumPlayer.h` (`FElysiumCombatCharacter::FieldOfView`; the
  declaration of `FElysiumPlayer::PostThinkAnimation()`), and the NPC FOV word's current home
  (`ElysiumNpcLifecycle2.inl:~118`, `ElysiumNpcSpawn.inl:~92`) if the word moves
- `ElysiumPlayerEntity.cpp` (`PostThinkAnimation()`'s empty body only)
- `ElysiumAnimEvents.{h,cpp}` (`FElysiumSequenceWords`, `DispatchBase`, `DispatchLayer`: the
  declarations of README § "Shared names" and bodies that dispatch nothing)
- `ElysiumAnimatingImpl.cpp` (H21: the `animevent` tap on the poll, ~:373-377, only)
- `ElysiumNpcKernelShapeMap.cpp`

Harness:

- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.{h,cpp}`,
  `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}`, the MCP tool file holding
  `elysium_entity_get` (`ElysiumMcpTools.cpp`), `Arena/README.md`
- for H20's bone read, the one place the runner's existing `on_ground` read goes through
  (`Private/Visual/ElysiumNpcBodyGeometry.cpp`, ~:120-125) — add the pelvis read beside it; if the
  runner cannot reach the drawn mesh from there, the map actor's embodiment file
  (`Private/Map/ElysiumMapActorEmbodiment.cpp`) and say so in your report

Records (`Arena/scenarios/`):

- new: `world/anim_footsteps_walk.json`, `combat/face_enemy_turn.json`,
  `combat/cover_move_shoot.json`, `anim_player_footsteps`, `anim_player_weapon_event` (two
  records, firearm and melee), `anim_prop_event` (place the last four in the family directory
  their neighbours use)
- new, red, from the judge's second sitting (item 7b): `corpse_removed_unseen`,
  `corpse_kindred_burns`, `corpse_pedestrian_stays`, `corpse_fades` (beside
  `damage_lethal_death`), `combat/ranged_sustained_fire.json`, `combat/melee_ally_in_the_way.json`
- corrected: `damage_lethal_death.json`, `verbs_stealth_kill.json`, `melee_swing.json`,
  `chase_melee.json`, `ranged_open_fire.json`
- `known_red` (and, under item 9's limit only, a deadline): `patrol_sentry2_pingpong`,
  `patrol_monk_loop`, `input_clearpatrolpath`, `places_pedestrian_visit`, `sense_enemy_facing_me`

## The job

1. **The words** on `FElysiumNpcBase`, beside the sequence words: `float LastEventCheck`
   (`m_flLastEventCheck +0x658`), `float YawSpeed` (`m_flYawSpeed +0x560`), `float GroundSpeed`
   (`m_flGroundSpeed +0x654`), `bool SequencePastHalf` (`m_fSequencePastHalf +0x568`, unless a word
   already stands for it — Grep `PastHalf`). Each with a comment: retail writers (`0x1008f120`,
   `0x10090950`, `0x10091880`, `0x10264841`), readers (`0x100916a0`, `0x10091740`), "not written
   yet: V4a lane A1/A2". `GroundSpeedCm()` returns `GroundSpeed` (0, as today).
2. **`m_flFieldOfView +0x1574` on `FElysiumCombatCharacter`** (`FieldOfView`). If the NPC holds it
   today, move it; every NPC writer keeps writing its value (0.2 Troika, species' own). The player's
   value (0.5, `CBasePlayer::Spawn 0x1016d260`) is written by lane A4, not you. Shape-map rows for
   all five words in the macro form their neighbours use; generated bindings are not touched (they
   regenerate at V4a's close).
3. **The slot-258 override** `FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity*
   Handler)` declared in `ElysiumNpcBase.h`, defined in the new `ElysiumNpcBaseAnimEvents.cpp` as a
   call to today's base (the overlay stub). Comment: "`CBaseAnimatingOverlay::DispatchAnimEvents
   0x10098c80` on the NPC chain; filled by V4a lane A1".
4. **The shared dispatcher's declarations** (J3; README § "Shared names", copy them exactly):
   `FElysiumSequenceWords`, `ElysiumAnimEvents::DispatchBase`, `ElysiumAnimEvents::DispatchLayer`
   in `ElysiumAnimEvents.h`, with bodies in the `.cpp` that fire nothing, write nothing and return
   false. Comment: "`0x10091880` / `0x10098cd0`; filled by V4a lane A1; called by A1 (the NPC) and
   A4 (the player, the camera)". And `void FElysiumPlayer::PostThinkAnimation()` declared in
   `ElysiumPlayer.h`, an empty body in `ElysiumPlayerEntity.cpp`, **called by nobody yet**.
   Comment: "`CBasePlayer::PostThink 0x1016be10`'s animation step; the call site is A1's, the body
   A4's".
5. **The bridge row accessors** (README § "Shared names"): `SequenceEvents(int32)`,
   `SequenceTurnYaw(int32)`, `SequenceGroundSpeedAt(int32, poseParams)` answering empty / 0 / 0;
   `SequenceLoops(int32)` returning today's row `bLoops`. Comment each with the descriptor field it
   stands for; "filled by lane A2 from the baked clip data".
6. **Harness H18–H21** (README §3, §9), in the scenario schema, the runner and `Arena/README.md`'s
   tables (a probe that cannot be read fails, as the others):
   - **H18** probes `speed2d` (cm/s, the body's horizontal speed), `move_yaw` (the body's,
     degrees), `ground_speed` (the kernel's `+0x654`, cm/s).
   - **H19** `elysium_entity_get`, per NPC: the facing-queue count and target, orient-to-movement
     on/off, `MaxWalkSpeed`, `m_flDesiredMoveYaw` and the `move_yaw` pose value, `+0x654`;
     **`attack_extents`** (`m_vecAttackExtents +0x50`, J2); **the three yaw words** (J9):
     `max_yaw_speed` (what the port's `MaxYawSpeed 0x10297ce0` answers now), `m_afMemory` (hex),
     `m_Activity`; and the driver's live **`Selection.MoveYaw`** as `selection_move_yaw`, with a
     line in the tool's description that `animation` / `next_animation` / `axis_fraction` are
     written only by a resolve and go stale during a continuous walk (R1 item 4, verdict 4). A
     field may read 0 where its kernel word is unwritten yet.
   - **H20** probe `corpse_on_floor`, as the spike measured it (`packets-spike.md` findings 4, 5,
     7): it reads **the `Bip01 Pelvis` bone of the drawn mesh** (`GetBoneLocation`; the drawn mesh
     is the map actor's component, attached to the motor) — never the component's location (below
     the floor at rest) and never the capsule. It passes when the pelvis is within the record's
     height bound of the floor under it (a downward trace) **and at rest: the pelvis's speed
     under ~5 cm/s** between two reads — not the sleep state, which a ragdoll here never reaches.
     The bound is a parameter of the probe in the record, **per the record's body, measured**:
     with Unreal's default capsule asset `regular_cop` rests at 16.2 cm and `bum_male` at 32.9 cm;
     so no one number serves every body. In each of the two death records name the record's body
     and write a provisional bound for it (24 cm where the body is `regular_cop`; for any other
     body say in `notes` that its rest height is unmeasured), with a `notes` line that V4d's
     integrator measures the rest height on the retail `.phy` asset and sets the bound from it. `on_ground`
     is re-described in `Arena/README.md` as the motor capsule's floor answer, false on every
     corpse.
   - **H21** (the owner, K3): the `animevent` trace kind emitted for every animating entity, not
     only NPCs — until lane A1 moves the tap into the dispatcher it taps the world-tick poll for
     the non-NPC entities, so the records have a "before" — and `who: "player"` accepted in
     `expect` / `never` for that kind. Check first what the tap covers today
     (`stories/wave2/seam.md`) and say so in your report.
   - **H22** (J13): the trace event kind **`removed`** — one line when an entity is removed from
     the entity world (`UTIL_Remove`'s port: the entity answers no name lookup and fires no
     output from then on), `who` the entity's name, usable in `expect` / `never` with `by` /
     `within` / `after` as the other kinds, documented in `Arena/README.md`'s table. **Check
     first whether a kind for removal already exists** (Grep the runner's kind table and
     `Arena/README.md`); if one does, use it, add nothing, and say its name in your report. The
     emit is in the runner / harness files of your list, tapping the world's removal — **no
     behaviour changes**: if the tap needs a line in a substrate file not in your list, the
     fallback is a probe that passes when `elysium_entity_get` (H19) answers none for the name
     (`probe: "exists", equals: false`, `at:` a time), and you say which you built.
7. **The NPC records.**
   - `world/anim_footsteps_walk.json` (new, a guard): sentry2 walking a two-point patrol (the
     `patrol_sentry2_pingpong` staging); `animevent` `2050` then `2051` on sentry2 during the leg,
     `never animevent` with an id ≥ 5000 on the NPC.
   - `combat/face_enemy_turn.json` (new, red): a hostile Troika human, the player placed ~135°
     behind its facing, in combat; `task_face_enemy`, then its `taskdone` **within 0.5 s**. `about`
     cites `packets-S1.md` item 5 (it corrects `packets-R1.md` item 3's address): `MaxYawSpeed
     0x10297ce0` answers 90 untagged in combat; `StartTask`'s turn tail `0x102a44d1` resets the
     yaw clock (`m_flLastYawTime = −1`); **`RunTask` 0x2e on a Troika NPC is `0x102aae61`** (not
     the base `0x102889b5`), which re-reads `MaxYawSpeed` every call and does not reset the clock,
     so `UpdateYaw 0x102e1e20` integrates 0.1 s on the first call and the real time between
     thinks after it, at `rate × 10` = 900°/s (`AI_ClampYaw 0x102e1d10`): 135° completes on the
     second call. The 0.5 s bound stands.
     `known_red` "V4b (the port's turn takes 13.4 s; cause unread, reader R1b)".
   - `combat/cover_move_shoot.json` (new, red; J5, bug protocol step 2): the `cover_armed` staging
     with the player in view during the run; `expect` `animevent 3031` on the gunman between the
     move task's start and its `taskdone`. `about` cites `RunTaskOverlay 0x10289c90` →
     `0x102e8560` and slot 575's gate. `known_red` names **the slot-575 seam first**
     (`packets-S3.md` item 4.1): "V4o O2: slot 575 never passes — `FElysiumNpc::ShouldMoveAndShoot`
     reads `ActiveWeaponCapabilityWord()`, a seam answering 0, so the overlay never arms; then
     V4o O1 (the NPC has no overlay layers)". Not "0015 … the wire 0002 R3" alone, and not V5.
     Staging note for the `about` (`packets-S4.md` item d): on the witness maps the run-and-gun
     is reachable only on `sp_tutorial_1` — nine placed rows (`thug_3` with
     `item_w_thirtyeight`; `Hunter1`, `sentry3`, `sabbat_redshirt_1`, `_2`, `_5`,
     `sabbat_redshirt_2_proxy` with `item_w_mac_10`; `mercenary_upstairs` with
     `item_w_ithaca_m_37`; `condotierre_upstairs` with `item_w_steyr_aug`), none on the hub with
     a ranged primary; the arena gunman stands for `thug_3` (the same class and weapon).
   - `damage_lethal_death`, `verbs_stealth_kill` (corrected, record error): the end probe
     `on_ground` → `corpse_on_floor` (H20); add `corpse` `match: "ragdoll"`; `known_red` "V4d (no
     character physics asset)". The retail source in `about`: `brief-D-ragdoll.md` § "What retail
     does".
     **`damage_lethal_death` expects no schedule after the death** (`packets-S1.md` item 3 and
     its "Changes to the plan" A0.1): on this record's kill `Event_Killed` → `CreateCorpse`
     replaces the think, so **no `schedule`, `task` or state line follows `dies`**. The `about`'s
     last sentence ("the only programs base `SelectSchedule` can answer in state 7 are `0x2c` /
     `0x2b`") is wrong for this record: rewrite it to say so. The `never` regex today admits
     `DIE (` and `SCHED_DIE_RAGDOLL (`: drop those two alternatives and add a `never` of any
     `schedule` on the victim after `dies`. This is a record error corrected with its retail
     source, not a loosened expectation.
   - `melee_swing` (corrected, record error; J7, R2 item 3c): replace `hit_event` (`animevent`,
     `match ""`) with a **`damage` on the player after `task_melee_attack1`** (the kind exists:
     `input_takedamage`); add **`never animevent` on the brawler** between the swing and its
     `taskdone`; rewrite the `about` (no attack clip in the male `baseball` bank authors an event;
     contact is `MeleeSwingUpdate 0x10346cd0` from slot 312, `0x1029365b`) and the `known_red`'s
     tail ("the hit event rides the world-tick poll" goes). `known_red` stays N3 (V11).
   - `melee_swing`, `chase_melee` (corrected, record error; `packets-S3.md` item 8,
     `CNPC_VHumanCombatant::SelectSchedule 0x103872d0` → slot 604 `0x10385e40`; slot 308
     `HasUsableRangedWeapon 0x10336d70` is false for a bat-only NPC): the schedule matches take
     the `_NR` forms. **In reach** (`CAN_MELEE_ATTACK1 0x51`) → **`0xdd
     SCHED_TROIKA_MELEE_ATTACK1_NR`** (line `0x65d`) → `MELEE_ATTACK1_SWING 0xde` →
     `task_melee_attack1`: `melee_swing` `attack_program` and `chase_melee` `swings` match
     `^SCHED_TROIKA_MELEE_ATTACK1(_NR)? \(`. **Out of reach** (`9` / `0x60`) → **`0xcb
     SCHED_TROIKA_MELEE_ADVANCE_NR`** (line `0x6b0`): `chase_melee` `advances` matches
     `^SCHED_TROIKA_MELEE_(CIRCLE_)?ADVANCE(_SLOW)?(_NR)? \(` (`0xd2`, line `0x6bb`, is the
     legitimate approach inside 100 units without `0x51`; `0xe1` the circle). **One
     `SCHED_TROIKA_MELEE_IDLE (0xc7)` first is retail** (the selection that admits the NPC to
     melee has gathered no band word yet): no `never` may forbid a single `MELEE_IDLE` after
     `START_COMBAT`; say so in each `about`. The `about` sentence "`0xdd` only with a ranged
     weapon" is reversed: correct it. The `about`'s "slot 555 `0x1026d9a0`" for the melee band →
     the weapon's slot 367 (`0x103eac30` → `0x103ea7e0`, walked in `packets-S2.md` item 4). If
     the trace prints another name for `0xcb` / `0xd2` / `0xe1`, the integrator of V11 swaps the
     name, keeping the number. `known_red` stays N3 (V11) on both.
   - `ranged_open_fire` (corrected): `shot_event` matches **`3031`**, not `""`; add `never` a
     `damage` on the player before it (J6). `known_red` "V4a (slot 363), then V5 (N2)".
   - `sense_enemy_facing_me`: `known_red` → "V4a".
7b. **The second sitting's records — new, written red, each `known_red` naming the lane that
   turns it** (J10, J12, J13, J14.1). Each `about` cites its retail source. Each must parse and
   be `expected-fail` on its real cause after your build (a record that passes today is reported,
   not forced red). Where a staging detail below cannot be met by the arena as it is (a
   `from_map` row, a second cast member, turning the player away), write the nearest staging the
   schema allows, say in `notes` what differs, and report it — do not invent a schema field.
   - **`corpse_removed_unseen`** — the ordinary mortal's clock. The `damage_lethal_death`
     staging with **the player turned away** from the victim: `expect` `removed` on the corpse
     about 10 s after `dies` (`by` death + 10.5 s). A second record or a second phase **facing**
     the corpse: `never removed` through death + 12 s. `about`: `CreateCorpse 0x1032c0e0`'s tail
     arms `SUB_PVSRemove 0x102696f0` at `curtime + 10.0` (`0x1032c404`); it re-arms every 10 s
     while any player passes all three of the view cone (slot 363), the PVS test (`0x101d1a90`)
     and `FVisible(corpse, mask 0x2804091)` (slot 201), else `UTIL_Remove`. `known_red`: "H22 has
     no consumer until the C integrator checks it (J13); the think is ported
     (`ElysiumNpc.cpp:~597-611`)" — if it is green on your build, leave it green with no
     `known_red` and say so.
   - **`corpse_kindred_burns`** — a tutorial `npc_VVampire` row by `from_map` (Kindred by its
     template), killed while **watched**: `expect` `removed` at death + 10 s (`by` + 10.5 s),
     seen or not. `about`: `burn = 0x10207df0` (`Has_Burning_Death`, or Kindred without
     `Disallow_Kindred_Death`) → `BurnModel 0x10090580` and `SUB_Remove 0x101c0b10` at `curtime +
     10.0` (`0x1032c32f`), unconditionally; pick a row that is **not** a maker child with
     `Flag_InfChild` / `Flag_Fade` (such a child also fades and goes at about +13.8 s — the later
     think wins). `known_red`: "V4c C integrator (J13): unverified until run; V4d lane D: a
     simulating body removed at +10 s must not ensure".
   - **`corpse_pedestrian_stays`** — a hub `npc_VPedestrian` by `from_map`, killed, **the player
     turned away**: `never removed` through death + 25 s; an end probe that the entity still
     exists. `about`: `CNPC_VPedestrian::CreateCorpse 0x103a38c0` — the pre-death bounds
     snapshot, the base, then `ThinkSet(this, NULL)` and `SetSolid(SOLID_NONE)`: no think at
     all, this chain never removes it. `known_red`: "V4c lane C2 (item 8): the override is not
     wired to slot 301, and the port's `Think` runs `SUB_PVSRemove` on any committed death
     (`ElysiumNpc.cpp:~645`)".
   - **`corpse_fades`** — a child of the tutorial's `stealth_victim_maker` by `from_map`
     (`Flag_InfChild 1` → `m_bFade` → `MakeNPC 0x1034b7b0` gives the child spawnflags `0x204`),
     killed and **watched**: `never removed` through death + 13 s; `expect` `removed` by death +
     14.5 s. `about`: `Event_Killed` step 14 (after slot 301): slot 552 `ShouldFadeOnDeath`
     (spawnflag bit 9) → `SUB_StartFadeOut 0x102695d0` (first think at `curtime + 10.0`) →
     `SUB_FadeOut 0x10269960`: alpha −7 per 0.1 s from 255 (36 steps), then alpha 0 and
     `SUB_Remove` after 0.2 s — gone about 13.8 s after the death, seen or not. `known_red`: "V4c
     lane C2 (J14.1): `StartFadeOut` is a counted seam reached only from `TASK_DIE`".
   - **`combat/ranged_sustained_fire.json`** — the `ranged_open_fire` staging (the shooter with
     the .38, `Size 6`), a longer `duration`: `expect` **eight** `animevent 3031` on the shooter
     (two more than `Size`); `never` `cond+` `NO_PRIMARY_AMMO (0x40)` on it; `never` `task`
     `task_reload` on it. `about`: retail fills an NPC's clip once, at equip
     (`Inventory_Insert 0x10334e70`: `max(Default_Size, 1)`), and `Shot 0x102387b0` subtracts
     only inside the player block (`0x10238a15`): an NPC's clip never drops and `0x40` cannot
     rise from firing (`packets-S4.md` item a). `known_red`: "V4a (slot 363), then V5a-2 (N2,
     the wait), then V4o lane O3 (J12): the port spends `Ammo_Cost` for every wielder and
     refuses the seventh shot (`ElysiumWeaponClasses.cpp:~2313-2323`)".
   - **`combat/melee_ally_in_the_way.json`** — two bat-only `npc_VHumanCombatant` hostile to the
     player and **neutral to each other**, in one line with the player, the rear one behind the
     front one: on the rear one `expect` `schedule` `0xe1` (the circle, `_NR` form; match the
     number, `SCHED_TROIKA_MELEE_CIRCLE_ADVANCE_NR (` if that is the trace's name) and `never`
     `schedule` `SCHED_TROIKA_MELEE_ADVANCE_NR (` (`0xcb`) while the front one stands between.
     `about`: `0x102a11d0` — a swept slab (mins/maxs doubled in X/Y, Z ±6, mask `0x2000000`)
     from `WorldSpaceCenter` to the enemy, true when the nearest body met is an NPC that is not
     hated (`IRelationType != D_HT`); `CNPC_VHuman::SelectScheduleMeleeCombat 0x10385e40` then
     answers the circle pair. `known_red`: "V11 (N3: no NPC enters melee), then V11-1 (J10):
     `ScheduleMeleeReachGate` is a seam answering false". (J7; R1 item 4). The four `known_red` texts
   (`patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`,
   `places_pedestrian_visit`) name 136.7 cm/s and "0.44×": rewrite each as "V4b (N13): the cruise
   speed is this body's own `walk_0` (<value> cm/s, <bank> bank) and is right; the time is lost
   at arrival — the body creeps the last ~30 cm for ~4 s before the kernel sees `arrived`
   (inferred from code until the B integrator toggles it; `packets-R1.md` item 4)". The value is
   **each body's own `walk_0`**, read from that body's bank
   (`$ELYSIUM_WORK_ROOT/import/characters/character/shared/{female,male}/
   move_and_ranged.clips.json`, look the cell up, do not read the file whole): sentry2
   (`vampire_hunter_chick`, female) **101.278** — the `about` may state the cause: the female
   `walk_0` is 37 frames at 33.82 fps (1.0645 s over 107.807 cm; `packets-S1.md` item 7), the
   male's 37 frames at 30.0; the male cell is 136.683. **Check
   `patrol_monk_loop`'s and the pedestrian's banks first** (R1 did not) and state each in the
   record. If a body is on neither bank, write "unrecovered; this body's `walk_0` is not read"
   and leave its text naming no number.
9. **The limit on every record edit** (J7): a deadline is changed only where it was computed from
   136.7 for a body on another bank, with the arithmetic shown in `about`. No other expectation
   moves.
10. **The player's and the prop's records** (J3, J4; each `about` states retail's order — the event
    from the entity's own think, after its frame advance — and cites `packets-R2.md` extension 1).
    - `anim_player_footsteps`: the player walks (`player_walk`) across the room; `expect`
      `animevent` 2050 then 2051, `who: player`, while walking — dispatched from `PostThink
      0x1016be10`; the handler `0x10178a10` swallows them and the sound stays the step clock's —
      and `never animevent` on the player while standing still. The cycles come from **the PLAYER
      model's baked event table**: R2 cited the cast's banks, so read the player's and say which
      table you read.
    - `anim_player_weapon_event`, two records: **firearm** — `animevent 3031` on the player
      before the `damage` it causes (the `*_attack_layer` clip in an overlay layer, `0x10098cd0` →
      `0x10178a10` → `0x1032e330` → `0x10238160` → `Shot`); **melee** — **`never animevent` in
      3000..0xfa2** on the player with the `damage` still landing (the sweep is slot 312's, right
      after the dispatch). 4050/4051 are the stealth-kill clips' camera pair and belong to
      neither record.
    - `anim_prop_event` — **a `never`, and not a vacuous one** (J4). In the Green Room, a
      hand-written `prop_dynamic` on `models/items/walkie_talkie/walkie_talkie.mdl`, sequence
      `Crooked_Cop_Walkie_Talkie_Into` (which authors 4100 @ 0.225), started by `SetAnimation`:
      `expect` the sequence plays (the prop animates), **`never animevent`** on it. With H21's tap
      on the poll it is red today and green once the poll is gone. `about` cites `CDynamicProp`'s
      think `0x10190850` (advances, never dispatches) and says no retail map row exists to copy.
      If that model has no baked prop body, say so in your report and write the record on any
      animating scenery prop, with `notes` stating it is vacuous by data (scenery 284, gibs 19,
      editor 10, worldcraft 2 and the cinematic lane author zero events).
11. **Build once** (`uv run elysium build`; wait on it, generous timeout or its completion
    notification). A second build only for your own compile break. Then run `uv run elysium arena`
    with the records you touched (item 7b's six among them) plus `control_sequence` and `cover` (by name), and `uv run elysium
    test` (the default tier) once. Acceptance: every record that passed before passes; the new and
    corrected records parse and are `expected-fail` on their real causes (or pass, for the guard);
    the default tier is unchanged.
12. **The measurement for reader R1b** (J9), from the arena run of item 11 plus one lab session:
    `uv run elysium gr --arena --headless`, `elysium.gr_scenario face_enemy_turn`, and
    `elysium_entity_get` on the turning NPC **at least five times during the turn**. Report, and
    write into `stories/v4/packets-R1b-measurement.md` (a short table, nothing else): the trace
    lines from `task_face_enemy` to its `taskdone` with their times; per sample the yaw, the ideal
    yaw, `max_yaw_speed`, `m_afMemory`, `m_Activity`; the degrees turned per second. Close the
    session with `quit`. You name no cause: R1b does.
13. **Commit once**: `feat(npc,arena): V4a seam -- the sequence speed and event words, the shared
    dispatcher's declarations, slot 258's NPC body, the FOV on the combat character, H18-H22, the
    V4 records`. Do not push.

## Not yours

No behaviour: no writer of the new words, no dispatch, no cone, no pick, no call of
`PostThinkAnimation()`. No `ElysiumNpc.cpp`. `script_walk_to_mark` (N19) is the A integrator's.

## Rules

README § "Rules for every agent of V4" (you are the one agent allowed a build here). The query
budget: 10 s warns, 60 s stops; never read a file over ~200 KB whole. Text through Grep / Read /
Glob. Wait on a build or a run by its completion notification, never a sleep or polling loop.

## Report (≤300 words)

The commit hash, the build's wall time, the verdicts of the records you ran, the default tier's
totals, where the FOV word lived and where it went, what the `animevent` tap covered before H21,
each N13 body's bank and `walk_0`, whether the walkie-talkie has a prop body, the measured turn
(degrees per second and the three words), H22's kind (new, existing, or the probe fallback), each
of the six second-sitting records' verdict and where its staging differs from item 7b, and
anything you could not do.
