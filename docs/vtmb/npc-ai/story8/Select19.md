# Story 8 pass I — family Select19, walked

The schedule selectors: slot 437 `PreSelectSchedule` and slot 438 `SelectSchedule`, the base
`CAI_BaseNPC` body, the `CAI_BaseNPCTroika` pair, and the species overrides. Read off the listing
(`vtmb_asm`) and the decompile; schedule names are each class's own corpus registrations
(`Content/ElysiumCorpus/ai/schedules/<unit>/space.json`). Pass C folds this into
`schedule-kernel.md` and `conditions-and-states.md`.

Conventions below: "trace L" is the `+0x1b30`/`+0x1b34` `__FILE__`/`__LINE__` stamp (ABSENT in the
port; recorded through `RecordScheduleEvent`); "sel N" is the `+0x1b2c` selector id
(`SelectScheduleSelector`, carried as a word — the agreed visual-only modernization). `Has(c)` is
`HasCondition 0x10269aa0`, `Int(c)` is `HasInterruptCondition 0x10269d30` (needs the bit in the
running schedule's mask). States are retail's `m_NPCState` numbers.

## `0x1028a260` — the selector pair (no verdict row)

`+0x1b2c = 0`; slot 437; when it answers 0, slot 438 as a tail jump. `GetNewSchedule 0x102814d0`
calls it, and three Select19 bodies re-enter through it (base case 2, Troika case 2, Troika
PreSelect state 2 / 0xe). Port: `FElysiumNpcBase::SelectNewScheduleRetail`.

## `0x1028a380` `CAI_BaseNPC::SelectSchedule`

sel 1, then `switch (m_NPCState)`, table `0x1028a9f4`.

- **1 IDLE**: any of HEAR_DANGER/COMBAT/WORLD/BULLET_IMPACT/PLAYER → 6 `ALERT_FACE` (0xe5b);
  GIVE_WAY → `0x38` (0xe60); navigator path type (`0x102ee620`) 0 → 1 `IDLE_STAND` (0xe65);
  LIGHT_DAMAGE with a sequence for ACT 0x49 → clear `m_bCondTookDamage`, `0x14 SMALL_FLINCH`
  (0xe6b); else 2 `IDLE_WALK` (0xe70).
- **2 COMBAT**: NEW_ENEMY → 5 `WAKE_ANGRY`; ENEMY_DEAD → `SetEnemy(NULL)`, `ChooseEnemy`: chosen →
  clear 0x58 and re-enter `0x1028a260`; not → `SetState(3)` and re-enter; damage (0x4c/0x4d) with
  `m_afMemory & 0x40` clear and an ACT 0x49 sequence → `0x14`; `IRelationType(GetEnemy()) == D_FR`
  → `0xd FEAR_FACE` unless SEE_ENEMY/0x4c/0x4d, then `FearSound` and `0x1b RUN_FROM_ENEMY`; no
  SEE_ENEMY → `0xb COMBAT_FACE` or, occluded, `0xf CHASE_ENEMY`; then 0x5f → `0x15`, 0x4f → `0x21`,
  0x50 → `0x22`, 0x51 → `0x1f`, 0x52 → `0x20`, 0x61 → `0xb`, and neither 0x4f nor 0x51 → `0xf`
  (always, the ladder already answered both); otherwise DevWarning "No suitable combat schedule!".
- **3 ALERT**: ENEMY_DEAD with ACT 0x61 → 8 `ALERT_SCAN`; no damage → hear family 6, else 9
  `ALERT_STAND`; damage → clear `m_bCondTookDamage`; `|DeltaIdealYaw| < (1.0 - m_flFieldOfView) *
  60.0` → `0x19 TAKE_COVER_FROM_ORIGIN`, else an ACT 0x49 sequence → 7, else 6.
- **4 SCRIPT**: live `m_hCine` → `0x2e AISCRIPT`; else DevWarning "Script failed for %s",
  `CineCleanup`, 1.
- **6** → 1; **7 DEAD** → `BecomeClientRagdoll` ? `0x2c` : `0x2b`; **0xc** → 1; **0, 5, 8..0xb, >0xc**
  → DevWarning (`NPC_STATE_IS_NONE!` / `Invalid State for SelectSchedule!`) and `0x43 FAIL` (0xf21).

**Unrecovered:** nothing in the body. The port answers `BecomeClientRagdoll` false (no client
ragdoll forms; the death handoff is `CompleteDeathHandoff`) and the navigator path type `-1`.

## `0x102ae920` `CAI_BaseNPCTroika::PreSelectSchedule`

sel 2 at entry on every path; `m_InvestigateSound` reset (`0x101b9880`: handle -1, origin
vec3_origin, the rest 0). In order:

1. `m_iForcedSchedule` non-zero → zeroed and returned, **no trace stamp** (pass R correction).
2. Squad (`m_iSquadDisconnected < 1`, `m_pSquad`, flags2 `SQUAD_NEW_ENEMY`): clear the bit (and
   bit 31); `+0x65e4 != -1` and not frenzied `0x80` → `0xeb START_COMBAT_SQUAD` (0x481b); else
   `SquadNewEnemy` with slot 167's enemy.
3. `Int(WAS_BUMPED)` → `0x101e3df0` (remove every active discipline effect whose record byte
   `+0x33` is set; port `ElysiumDisciplines::NotifyBumped`).
4. Running schedule is `GetScheduleOfType(0x14a D_MESMERIZE)` → `0x101e3ee0` (record byte `+0x34`
   `InterruptSchedule` sweep).
5. State 2: `Int(0x22)` → when the closest player IS the supernatural offender (both null
   compares equal), `PlayerSupernaturalIncident(player, witnessed level, this, location)` and the
   processed count; then slot 596 and slot 597(offender, 5). `Int(0x20)` mirrors it with the
   decoded criminal level. ON_FIRE → `0x151` (0x4850). No slot-167 enemy → `SetState(no_alert ? 1 :
   3)`, trace 0x4862, re-enter `0x1028a260`. flags1 `ATTACK_UNKNOWN` → cleared; flags2 `0x80` clear
   and not frenzied → `0x5b` (0x4884), else flags2 `&= 0x7fffff7f`. NEW_ENEMY and not frenzied →
   `0xea START_COMBAT` (0x488c).
6. Any state: PLAYER_ON_HEAD `0x3b` and not busy with a discipline → cleared; no live dialog
   partner → `debug_player_on_head` (default 3): 0 → `0x7b`, 1 → `0x79`, 2 → `0x7a`, 3 →
   `RandomInt(0,99) < 0x50 ? 0x79 : 0x7a`; above 3 falls through.
7. State 0xe: the criminal half of 5 (slot 596 only, no slot 597), ON_FIRE (0x48c4), no enemy
   (0x48d6).
8. Base `0x1028a2a0`; non-zero returns.
9. flags2 `FINISH_SPECIAL_NAV` → clear flags1 `PRESERVE_PATH` and it; nav type 3 → `0xfc`, 1 →
   `0xfd`.
10. State 2, `m_bStayEntrenched`, slot 592 `CanSeekCover` → `0x102b7690(1,0,0,0)` non-zero returns.
11. flags1 `DO_STARTLED` → cleared, `0xf1`.
12. State 1: KNOCKBACK → `0x14c`, COMFORT → `0x12f`, D_CALM → `0x130`, D_FOLLOW → `0x131`,
    D_POSSESSED → `0x131`, live dialog partner → `0x6a`; state 2: KNOCKBACK → `0x14c`; state 3:
    `0x102b8a10` non-zero returns.
13. `m_fSavePositionWalk` → cleared, `0x89`; else 0 (`0x102af39a`, no stamp).

**Unrecovered:** the discipline record byte `+0x34` has no port surface (the sweep is counted);
the squad object does not exist here.

## `0x102af660` `CAI_BaseNPCTroika::SelectSchedule`

sel 2; `switch (m_NPCState - 1)` over 1..0xe, table `0x102b0bf0`; 0, 4..7, 9, 0xa, >0xe → base.

- **1 IDLE**: busy with a discipline or in a choreo scene → `0x6b` (0x49d2). Slot 607 (follower
  ladder) non-zero is **returned** (`0x102af6b0 JNZ 0x102b0af5`). Patrol path `+0x6590`: its
  `m_iSchedule` non-zero → `0x1029f650`, returned (0x498d); zero → DevMsg "WARNING:  Patrol path
  for '%s' has no schedule." and `0x1029f5d0`. `m_bUseInteresting`: path type != 8 and no place →
  `0xff` (0x49a9); else Int(0x13) → `0x102`, Int(0x10) → `0x106`, Int(0x11) → `0x105`, else
  `0x100`. `m_bAllowAlertLookaround`: `RandomInt(0,99) < min(0x1e, (sightings+2)*5)` → `0x4f`.
  A live `m_hBlockedDoor` or `m_hCondHitByDoor` → `SelectDoorObstructionSchedule` non-zero
  returns. `m_bReturnToInitialPos` → consumed, `0x45`. Else `0x6b`.
- **2 COMBAT**: clear `m_bCondTookDamage`. ENEMY_DEAD as the base (traces 0x4abf / 0x4acb). Damage
  flinch `0x14` (0x4ad5). D_FR → `0xd` / `FearSound` + `0x1b`. NO_PRIMARY_AMMO with a weapon whose
  reserve (`0x103346c0(+0x744)`) > 0 → `0x28 HIDE_AND_RELOAD`. The gate `+0x6444` (unnamed dword)
  zero or WAITING_ATTACK_TIME → the ladder; else a `0x6000` weapon → `0xec`, otherwise the base.
  Ladder: no SEE_ENEMY → `0xb` / occluded `0xb1`; 0x08 or 0x5f → a `0x6000` weapon with neither
  0x2f nor 0x63 → `0x102b7f40` ? `0xef` : `0xf0`, else `0xb8`; 0x4f → `0x101e3f50` → `0xef`, or
  `RandomInt < 0x14` with no hint node and `m_flEnemyDist < 800.0` → `0xef`, else `0xed`; 0x50 →
  `0xee`; 0x51 → `0xdc`; 0x52 → `0xdf`; 0x61 → `0xb`; 0x60 → `0xb1`; 0x63 or 0x2f → `0xbc`; 0x59
  → `0x17`; 0x66 with a `0x6000` weapon → `0xf0`; 0x4a with a weapon → `0x6000` → `0xf0`, `0x18000`
  → `0xca`; else `0xbf`.
- **3 ALERT**: `0x102b8a60`, `0x102b8c40`, DETECTED_ATTACK → `0x56`, `0x102b7370`, `0x102b9060`,
  else `m_bGoToIdleState = m_bForceStateChange = 1` and **`0x4b ALERT_WAIT`** (0x4a08).
- **8 FLEE**: COVER_FAILURE `0x39` → `0x73`. None of SEE_ENEMY, SEE_FEAR, 0x4c, 0x4d, 0x4e, 0x21,
  0x1f → DETECTED_ATTACK `0x56`, INVESTIGATE_SOUND (clear INITIAL_FLEE, investigate clock +2.0,
  `CommitBestSound`) `0x48`, else `0x77`. Otherwise clear `m_bCondTookDamage`; no INITIAL_FLEE →
  `0x73`; else clear it, flee-sound clock `RandomFloat(10,20)`, `FleeSound`; damage → `0x72`;
  supernatural flee `0x21` → offender to slot 596; player != offender → live offender `0x73`, else
  save the location `0x76`; player == offender → `InsertSound(8,…)`, then the incident (or, flee
  only with SEE_PLAYER, `0x1017fd60`) and the processed count; `m_flPlayerDist <= 512` and
  `RandomInt < 0x50` → `0x71`, else `0x70`. Criminal `0x1f` mirrors it. SEE_FEAR → slot 596 on
  `m_hLastSeenFearEnt`; `0x72`.
- **0xb HUNT**: Int(1)/Int(0x26) → `0x81`; `0x102b8c40`; Int(2) → `0x82`; Int(sound family) →
  investigate clock, `CommitBestSound`, (0x6d or 0x70) and `RandomInt < 100` → `0x7f`, else
  `0x80`; Int(0x72) → `CommitBestSound`, `0x50`; SEE_SOUND_SOURCE → `0x102b8d20(0x84, 0x73)`; no
  hunt path `+0x6598` → clear `MADE_HUNT_PATH`; bit clear → curtime at/past `m_flHuntExpireTimer`
  → `0x85`, a live slot-168 enemy → `0x7c`, else `0x7d`; bit set → `0x7e`.
- **0xc** → `0x6b`; **0xd** → `0x44`; **0xe** → the `m_iSubState` walk (default → 1 `0x116`, 1 → 2
  `0x117`, 2 → 3 `0x118`, 3 → 4 `0x119`, 4 → 5 SEE_ENEMY ? `0x11b` : `0x11a`, 5 → 2 `0x117`).

**Unrecovered:** the patrol-path object (no port `CAI_PatrolPath`), `InsertSound`'s two globals.

## Helpers `0x102b8a60`, `0x102b9060`, `0x102b8d20` (no verdict rows)

- `0x102b8a60`: Int(IGNORE_UNKNOWN) while not investigating (`m_afMemory & 0x8000000`) → `0x60`;
  Int(SEE_UNKNOWN) / Int(UNKNOWN_ADVANCING) / Has(INVESTIGATE_SIGHT) → alert level 3, then not
  investigating `0x59`, LOOKED_AT_UNKNOWN → UNKNOWN_RUN_TIMER ? `0x5e` : `0x5c`, else `0x5a`;
  Int(LOST_UNKNOWN) → investigating with the run timer `0x5f`, else `0x5d`; LOOKED_AT_UNKNOWN →
  `0x5c`; else 0.
- `0x102b9060`: Int(INVESTIGATE_SOUND): combat or bullet → clock, `CommitBestSound`, level 3,
  investigating → `0x52`, not frenzied `0x10000` and `RandomInt < 100` → `0x50`, else `0x51`;
  HEAR_WORLD → `m_BestSound = m_LastSoundWorld`, `m_InvestigateSound = m_BestSound`, `0x51`;
  player or danger → `CommitBestSound`, then TAIL-JUMPS into `0x102b8980`, whose answer IS the
  schedule (`0x4c`/`0x4d`/`0x51`/`0x52`). Then SEE_SOUND_SOURCE → `0x102b8d20(0x89, 0x73)`; not
  frenzied `0x10000` and Int(HEAR_FLINCH) → `CommitBestSound`, `0x50`; else 0.
- `0x102b8d20(hated, feared)` (`RET 0x8`; the answers are the two stack arguments): the best-sound
  source's enemy; `D_HT` → the source's memory of the enemy (when the source made the last combat
  or bullet sound) or its forward × 128 + origin into `m_vSavePosition`, `hated`; `D_FR` →
  `feared`; else investigate clock `curtime + 20.0`, 0. A dead source answers 0 with no re-arm.

## Species

- `0x1035fb50` **CNPC_VAnimal** (sel 5): DO_STARTLED → cleared, `0x15d`; idle: patrol node schedule
  (no stamp); use_interesting: path type != 9 and no place → `0x156`, else `0x157`; alert:
  `0x102b8a60`, `0x102b9060`; then the Troika body.
- `0x103742d0` **CNPC_VDog** (sel 0xd): SHOULD_SNARL `0x7b` → cleared, `0x166`; idle: patrol node,
  no use_interesting → `0x164`; combat clears `m_bCondTookDamage` and skips; idle/alert:
  Int(PLAYER_BEFRIENDED `0x7d`) → `0x167`; the animal's `0x6b` becomes `0x164`.
- `0x103ac610` **CNPC_VScurrying** (sel 0x20): inside `m_flFrightEndTime` → `0x162`; PLAYER_TOOCLOSE
  `0x78` → `0x162`; HEAR_BULLET_IMPACT → fright origin from the bullet sound, end time = now +
  duration, `0x162`; the animal's `0x6b` becomes `0x161`.
- `0x103df2e0` **CNPC_VZombie** (sel 0x2b): crawl-out → `0x161`; EF_NODRAW → slot 67 `Unhide`; not
  alive → `CreateCorpse(death force, death info)`; AI type 7/8: provoked (0x4c, 0x4c again, 0x4d,
  `0x79`) → type 1, else without GIVE_WAY → `0x16b`; idle: type 1 → `SetState(2)`, SEE_ENEMY ?
  `0x165` : `0x169`; type 5 → `0x169`; patrol node; combat/alert: a player enemy seen and not
  obfuscated (`0x10146a80`) → `0x165`, else `0x169`; then the animal.
- `0x10384ee0` **CNPC_VHuman** (sel 0x14): combat only — clear `m_bCondTookDamage`; DETECTED_ATTACK:
  no memory of the attacker → `0x56` (0x269), last seen at least 1.0 s ago → `0x56` (0x261); then
  the weapon word `& 0x18000` → slot 604, else slot 605, both given the word; non-zero returns;
  else the Troika body.
- `0x103872d0` **CNPC_VHumanCombatant** (sel 0x15), `0x103dd6b0` **CNPC_VYukie** (sel 0x2a): combat →
  clear the flag, the weapon split; else the parent.
- `0x10387d20` **CNPC_VHumanCombatPatrol** (sel 0x16): combat, not busy, not seeing (or occluded),
  a patrol node → its schedule as it stands (0x121); then the weapon split; alert, not busy, a
  node → its schedule (0x116); else the combatant.
- `0x1037bd60` **CNPC_VGhoulCroucher** (no sel): not disturbed → `0x158`; not exited → `0x159`;
  else the combatant.
- `0x1037d130` **CNPC_VGuard1** (sel 0x12): DO_STARTLED → `0xf1` (no stamp); state 0xc: Int(0x1e)
  and a live closest player → `0x1017e6f0(player, 0)`, `0x6d`; else `SetState(1)`; the human.
- `0x10371ee0` **CNPC_VCop** (sel 0xc): idle and from a spawner: with no patrol node, or a player
  that is not in heightened alert and has no cops in pursuit → the budget: the `+0x6672` claim
  byte clear and `[0x1093acac] - [0x1093acb0] <= 3` → `0x170`; else claim it, `++[0x1093acb0]`,
  `0x16e`; state 0xc as Guard1; else the combatant.
- `0x103a29f0` **CNPC_VPedestrian** (sel 0x1d): first think → `0xfe`; PASS_OUT not busy → `0xfa`;
  DO_STARTLED → `0xf1`; idle/alert INVESTIGATE_SOUND → clock, `CommitBestSound`; the SQUARED
  distance to `m_BestSound`'s origin at least 256.0, `SKIPPED_SOUND` clear and `RandomInt < 0x19`
  → set it, `0x158`; else clear it, set INITIAL_FLEE, `0x157`; else the human.
- `0x10360eb0` **CNPC_VAsianVampire** (sel 6): a closest player not hated → the human;
  `asianvamp_force_jump_up` → `0x15a`; path not blocked: not stationary too long and not standing
  on the player → the human; blocked in melee → slot 601 on the enemy, `0x15c`; else
  `GetJumpSchedule`.
- `0x1036b250` **CNPC_VChangBros** (sel 10): a closest player not hated → the human; the three force
  ConVars `0x15e`/`0x15a`/`0x15c`; conditions `0x7c` → `0x15e`, `0x79` → `0x15c`, `0x7a` →
  `0x15a`, ENEMY_UNREACHABLE → `0x15d`; else the human.
- `0x103788d0` **CNPC_VGargoyle** (sel 0x11): the stat-list dead test → `0x15c`; combat: a
  reachable enemy with a path → the human; else `0x10378f80` (pillar found → FINDING_BODY,
  `0x15a`; else `+0x6680 = 2`); else the human.
- `0x1037fca0` **CNPC_VHengeyokai** (sel 0x13): state 5 → `0x16f`; combat: ENEMY_DEAD while
  carrying → `0x15e`; finding a body: clear it; Int(0x51) or Int(0x5f)/Int(8) → release the pickup
  target, re-arm the collision ignore at 0, `0x10382f60`; else `0x10382d40` or `0x15f`; carrying:
  timer expired and reachable → `0x161`, occluded or unseen → `0x166`, too close → `0x15e`, no throw
  LOS → `0x166`, the one fake throw (`RandomInt < 5`) → `0x164`/`0x162`, else `0x163`/`0x160`;
  otherwise clear PRESERVE_PATH, `0x10382d40`; unreachable → `0x167`; occluded → `0x165`;
  TOO_FAR_FOR_MELEE → SEE_ENEMY ? `0x169` : `0x165`; else the human.
- `0x10394120` **CNPC_VMingXiao::PreSelectSchedule** (sel 0x19): state 5 → `0x157`; else
  `Weapon_Switch(ranged weapon or NULL, 0)`, 0.
- `0x103941e0` **CNPC_VMingXiao::SelectSchedule** (sel 0x19): idle/alert → `0x44`; combat: the
  grabbed-object arm; tentacle conditions `0x77+i` → `0x158+i`; spit → `0x15e`; occluded →
  `0x160`; `0x10396bc0`; a proxy → `0x162`; `ming_xiao_charge` and enemy nearer than 120: ready →
  re-arm from the tuning record, `0x15f`; MELEE_HELPLESS → now, then the tentacle gate 2/3/0/1 →
  `0x15a`/`0x15b`/`0x158`/`0x159`; else `0x165`; other states the Troika body.
- `0x103aa510` **CNPC_VSabbatLeader::PreSelectSchedule** (sel 0x1f): combat with no enemy → the two
  state words written 1 directly (not `SetState`); the Troika pre-selector.
- `0x103a70c0` **CNPC_VSabbatLeader::SelectSchedule** (sel 0x1f): state 5 → `0x158`;
  `andrei_force_player_collision` → reset to 0, SetAbsOrigin(player + 20 x); `CheckStuck`; no roars
  → refill 3, `0x165`; activated: force jump → `0x15b`, force charge → `0x166`, TIME_TO_JUMP → not
  after a nova `RandomInt(0,2)` 0 → `0x164`, 1 → `0x15b`, 2 → `0x166`; after a nova
  `RandomInt(0,1)` 0 → `0x164`, else `0x15b`; else the human.
- `0x103ae8c0` **CNPC_VSheriffMan** (sel 0x21): activated: `CategorizeHeights`; state 5 → `0x158`;
  alive: `sheriff_force_teleport > 0` → reset, `0x15a`; player low and self high → `0x15c`; player
  high and self low → `0x15d`; health lost since the record `< 0.05` and not idle 3.0 s → the human,
  else `0x15a`; dead → `KillSheriff`; the human.
- `0x103bb7c0` **CNPC_VTzimisce** (sel 0x26): DO_STARTLED → `0x187`; `RandomInt(0,99) < 0x32` →
  slot 627; combat: drop/throw arms `0x198`/`0x195` while carrying, first NEW_ENEMY → 5; not
  finding a body: carrying ladder (`0x193`, `0x169`, `0x198`, `0x169`, fake throw
  `0x196`/`0x194`, `0x195`/`0x192`); else clear PRESERVE_PATH, `0x103bc4e0`, unreachable → the hint
  pick (`0x17c`/`0x17e`/`0x17f`/`0x180`/`0x16a`), occluded `0x167`, reachable: pounce `0x186`,
  TOO_FAR `0x168`/`0x167`; the running-program continuations `0x103bca20`/`0x103bcaf0`/
  `0x103bcb60`/`0x103bcc00`; else `0x170`; finding a body: clear it, the melee-attack pick on
  Int(0x51) or Int(0x5f)/Int(8), else `0x103bc4e0` or `0x170`; alert: the two Troika helpers; hunt
  (0xb): carrying `0x198`, SEE_UNKNOWN `0x15d`, sound `0x15c`, the roll (`0x15f`/`0x160`/`0x161`),
  MADE_HUNT_PATH `0x15b`, no enemy `0x15a`, the hint pick, `0x159`/`0x15a`; else the Troika body.
- `0x103c1610` **CNPC_VTzimisceHeadClaw** (no sel): combat: SHOULD_CHARGE → `0x158`; ranged word:
  slow running or `RandomInt(0,100) < 0x28` → `0xca`, else slot 605; melee word: slot 604.
- `0x103c3310` **CNPC_VTzimisceRunner** (no sel): combat: slot 604; a live potential enemy discards
  the answer unless it is 199/200/`0xe4`/`0xe7`/`0x156` and the enemy is beyond 256 in 2-D (then
  `0x157`); else the Troika body.
- `0x103cee70` **CNPC_VWerewolf** (sel 0x29): slot 461 first; `werewolf_force_teleport` →
  `ClearMoveHint`, `0x157`; not viewable → `0x158`; states 0/1 → enemy ? `0x157` : `0x156`; 2, 3,
  8, 10, 0xe → no enemy `0x156`, dead enemy `0x157`, CAN_TELEPORT `TeleportOut` `0x158`,
  SHOULD_BREAKHINT without hint flag `0x100` → `0x15e`, TOO_CLOSE → `0xd3`, frustration → `0x15d`,
  special move with no melee and unreachable → the move hint's schedule, unreachable → `0x157`; 7
  → not alive `0x162`, DEATH_TRIGGERED `0x161`; 9 → teleport, random move hints, `0x157`; 0xb →
  death, teleport, break-hint, then special move / random hint; else the Troika body.
- `0x10375d90` **CNPC_VFrenzyShadow**, `0x103dceb0` **CNPC_VWolfMorph**, `0x103a46b0`
  **CNPC_VPlayerController::PreSelectSchedule** landed in story 5 fold A2 (their class files);
  their tail into `CNPC_VHuman` is an integrator redirect.

**Unrecovered:** the Tzimisce body search (`0x103be180`) and pounce hull probe (`0x103bf660`), the
Hengeyokai fish search (`0x10381cd0`), the Gargoyle pillar search (`0x100f7b20`) and navigator path
test (`0x102ee380`), the Ming Xiao tuning record (`0x101e8da0(0x10739d08)`), slot 627's fidget voice,
`m_fSequencePastHalf`, and the Werewolf random-move-hint pair (family Werewolf19's rows) — each a
named seam answering retail's "nothing".
