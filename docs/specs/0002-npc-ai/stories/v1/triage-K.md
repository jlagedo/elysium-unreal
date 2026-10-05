# V1 triage, lane K

One entry per red: the record, its first unmet expectation, what the trace shows instead (scenario
seconds), the retail source, the class. Runs: `$ELYSIUM_WORK_ROOT/reports/arena/20261004T020206*`,
`…T020809*`, `…T020943*`, `…T021055*` and the last (see the end).

## Reds

### `cover` — known red 1 (record corrected first)
- **Record error, fixed.** The record expected `SCHED_TROIKA_TAKE_COVER_HINT_ATTACK` (0xa3) against an
  UNARMED player. Retail `0x102b7690`'s cover tail sets `bVar1` (melee threat) unless the enemy's
  active weapon's slot-360 word carries `0x6000` (ranged `0x2000`; melee `0x40018000`; none = no
  weapon), and answers `0xa4 _VS_MELEE` (line `0x5cc3`) when `bVar1`, `0xa3` (line `0x5cd9`) when
  not. The port's `0xa4` was right. Rewritten to the `_VS_MELEE` → `_VS_MELEE_ATK` chain, with a
  `never` on `0xa3`.
- **First unmet:** `expect[5]` `seqfinished` (the `PLAY_COVER_OUTOF` clip). `task_play_cover_outof`
  starts at 7.40 and its sequence is `smith_lean_left_into rate=0`; nothing finishes by 17.4.
- **Class:** known red 1 (the arbiter refuses the kernel's sequence after a path task).

### `cover_armed` — new red
- **First unmet:** `never[1]`: `SCHED_TROIKA_TAKE_COVER_HINT_VS_MELEE (0xa4)` at 7.25, with the
  player holding the .38.
- **Retail:** `0x102b7690`, `GetEnemy()->+0x9c` → `GetActiveWeapon` → `vtable+0x5a0 & 0x6000` → `0xa3`.
- **Port:** `ElysiumNpcSchedule.cpp:484-495` — the read is a stub ("no cross-entity weapon
  capability reader"), so `bNoRangedThreat` is always true.
- **Caveat:** the trace cannot show the player's active weapon (G2). `elysium.cmd slot3` does wield
  the .38 (in a `damage_idle_reaction` run the player fired it: `WeaponAttackCommit` at 3.03).
- **Class:** new red.

### `ranged_open_fire` — known red 3, then a new red
- **First unmet:** `never[1]` `cond+ BEHIND_ENEMY (0x57)` at 0.00: the player faces the gunman.
  Known red 3 (slot 363 `FInViewCone` stub).
- **Second divergence (same trace):** `task_wait_attack_time1` completes at 1.000, the instant it
  starts; the next `task_range_attack1` is at 1.10, 0.6 s after the first shot (0.50). Retail
  `0x102a337d`: `m_flWaitFinished = 0x10252450(weapon) + 0x102c5730(this, weapon)`, complete only
  once `<= curtime` (the weapon's next-attack stamp, `Attack_Rate .8` floor, NPC rate 1.0–2.5 s
  scaled by range). Port: `FElysiumNpc::StartTask19WeaponNextAttackTime`
  (`ElysiumNpcStartTask.cpp:575-579`) is a seam answering `curtime`. The record's
  `never taskdone task_wait_attack_time1 until 1.05` catches it once BEHIND_ENEMY is fixed.
- **Class:** known red 3 + new red.

### `chase_melee` and `melee_swing` — new red (one cause)
- **First unmet:** `chase_melee` `expect[1]` (an advance program) by 3.35: the trace shows
  `SCHED_TROIKA_WAIT_FOR_MELEE_ADVANCE (0xe7)` at 0.75; `melee_swing` `expect[1]` (`MELEE_ATTACK1`):
  `SCHED_TROIKA_WAIT_FOR_MELEE (0xe4)` at 0.50 with the player 47 units away.
- **Retail:** both come from `0x10385e40`'s head "not in melee and slot 599 refusing". Slot 599
  `0x102b5650` admits a lone NPC: its last term `0x1025db70(m_pAttackCoordinator, this)` answers true
  when the NPC is already listed or the list has room (it appends it). So `m_bInMelee` is set and the
  tail answers `0xe0`/`0xca` far, `0xdc` inside melee range.
- **Port:** `FElysiumNpc::MeleeCoordinatorAdmits599` (`ElysiumNpcTroikaHelpers.cpp:142-146`) is a
  seam answering false, so no NPC ever enters melee.
- **Also seen (not asserted):** `0xe7`'s `TASK_SET_SPECIAL_DISTANCE_ACCUM DIST:COMBATMOVE` runs with
  `-1e+06` (the `ResolveTaskDistance 0x102702d0` sentinel stub fires), and
  `task_choose_best_melee_weapon` fails `No weapon to choose (0x1f)` for a bat-armed NPC.
- **Class:** new red.

### `damage_lethal_death` — known red 3
- Met: `input TakeDamage` 3.017, `death none`, `output OnDeath`, `corpse ragdoll`, all at 3.017; no
  program after death. **First unmet:** end probe `on_ground` reads false. Known red 3 (a dead NPC
  stays standing; the corpse is not landed).

### `lifecycle_unhide_fights` — known red 5
- **First unmet:** `never[0]` `SEE_ENEMY` at 0.00 — the hidden NPC senses, sets
  `FLOATING_OFF_GROUND` and runs `FALL_TO_GROUND (0x3e)` before `ScriptUnhide`. Retail `ScriptHide
  0x100a8710` parks the think (`NULL` at `FLT_MAX`) until `ScriptUnhide 0x100a8990`
  (`lifecycle.md` § "The NPC makers"). Known red 5.

### `verbs_stealth_kill` — harness gap
- **First unmet:** `expect[1]` `death` by 12.0. Nothing happens on `+use` (2.0); `+attack` (4.0)
  only makes the mark hear the player (`HEAR_PLAYER` from 4.8). The knife is wielded (the same
  `slot2` + knife landed a 40-point hit in `damage_idle_reaction`); whether `+duck` took, and which
  `FindVictim 0x101be1f0` gate refused, the run cannot say: no probe of the player's posture or
  weapon, no trace of the stealth query (G2). No `known_red` set: unclassified until the harness can
  show the gate.

### `hub_crosswalk_wait` — known red 6
- **First unmet:** `expect[1]` `SCHED_TROIKA_WAIT_AT_CROSSWALK` by 280 (`DontWalk` met at 11.83).
  In 280 s of the hub no NPC runs `0xff` / `0x100 SCHED_TROIKA_WALK_TO_INTERESTING_PLACE` at all
  (0 occurrences in the trace), so no pedestrian route (goal type 8) reaches a curb. Known red 6
  (every `use_interesting` pedestrian is on the ambient executor). Wall 135 s for 280 game s.

### `damage_idle_reaction` — record errors fixed; now passes
- Run 1: the scalar `TakeDamage` with no activator raised no `LIGHT_DAMAGE`. Retail
  `0x10265ed0` step 6 returns before the conditions when there is no attacker: **record error**
  (and harness gap G1, `fire` has no activator).
- Run 2: the player's .38 at 275 cm missed (a ranged hit is a roll): record error.
- Run 3: the knife back-stab did 40 and killed the TutorialThug (`Max_Health 20`): record error.
- Run 4: `invincible 1` refuses the whole transaction in retail (`combat-and-damage.md`
  § "`invincible` is a total refusal"): record error. Now `Tutorial_Jack` (`Max_Health 819`).
- Run 5: `damage 40 from=!player` 3.42, `break LIGHT_DAMAGE` 3.50, `SHOT_BY_UNKNOWN (0x8a)` 3.50 —
  the spine holds. Then `TASK_FIND_COVER_FROM_SAVEPOSITION` fails `Couldn't find cover (0x8)` and the
  program falls to `FAIL`, `ALERT_WAIT`, back to idle at 9.6. The record's end probe `state Alert`
  assumed the 50 s program runs; retail's cover-position search in this room was not read, so the
  probe was a record error and is removed. Run 6: **pass**. Open for V2: read the find-cover search
  for the arena (is "no cover" retail's answer here?).

## Passes
`cover_reclaim` (claim → destructor release → re-claim of `cover_low_north`, both walks),
`damage_idle_reaction`,
`range_bands` (exactly one band at 96 cm and at 10 m; step-back program), 
`lifecycle_relationship_flip`, `verbs_feed_trance` (after a record error: releasing at 5 s drained
the victim, retail's own pulse cadence; now released after 1.3 s).
Known red 2 (stacking) and the "flipped NPC never enters Sighted()" half of known red 5 do not
reproduce in the arena. `TOO_CLOSE_TO_ATTACK` (0x5f) is not staged: its band edge is the weapon's
`m_fMinRange1 +0x8b8`, value unknown.

## Harness gaps
- **G1** `fire` cannot name an activator: a scripted `TakeDamage` has no attacker.
- **G2** no probe of the player's active weapon, posture (duck), grapple or stealth-kill eligibility;
  `player.armed` grants without wielding (a wield field, or `elysium.cmd slotN` made checkable).
- **G3** `never` cannot be windowed after a label (`after`/`from`): "nothing after death", "no
  restart after load".
- **G4** no `save` / `load` actions (V6).
- **G5** no arena variant with an unreachable-but-seen spot, a sealed pocket, a blocked route or an
  unreachable cover hint (chase-failed witness, `0x0c`/`0x0d`, the `+5.0` hint release).
- **G6** script actions are not logged in the run log; trace condition text is `(0x8)`, not the
  README's `(0xNN)`.

## Observation for V2 (not asserted)
A neutral idling `npc_VHumanCombatant` reports `seqfinished idle01` / `sequence idle01 rate=1` every
0.1 s (`damage_*`, `verbs_*` traces): the idle clip appears to restart each think.
