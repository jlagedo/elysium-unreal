# Conditions19 — the condition gather, walked (spec 0019 story 8, lane L07)

_Story 8 pass I. Every body below is ported arm by arm into
`Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseConditions19.cpp` (the two `CAI_BaseNPC` bodies),
`ElysiumNpcConditions19.cpp` (the Troika body and the Troika half of the base body) and
`ElysiumNpcConditions19Species.cpp` (the species overrides); the tests are
`Tests/ElysiumNpcKernelConditions19Tests.cpp`, suite `Elysium.Substrate.NpcKernelConditions19.`.
Pass C folds these sections into `conditions-and-states.md`. Condition names are the base registrar's
(`conditions-and-states.md` § "The base condition table"); addresses are `vampire.dll`._

## `CAI_BaseNPC::GatherConditions` — `0x1026ec30` (slot 433, 987 bytes)

The base body under every Troika override. In order:

1. `m_bConditionsGathered (+0x5ca4) = 1` (`1026eca9`) — the only word the body writes itself. The
   scope-trace push, the `rdtsc` bracket into `DAT_109204a0/a4` and the VProf pair are dead.
2. `0x102cc760(&m_DelayedConditionList +0x1a9c, this)` (`1026ecc1`): every entry stamped at or before
   `curtime` is promoted by `SetCondition` and removed by `0x102cc730`, which copies the LAST entry
   into the vacated slot; the walk re-examines that slot. A later (or NaN) stamp waits.
3. `m_NPCState (+0x5cc0)` 0 or 7 (`1026ecce` / `1026ecd7`) jumps to the epilogue: nothing below runs,
   not even `CheckAmmo`.
4. The sense gate: `m_spawnflags & 0x400` (`1026ece8`) OR `UTIL_FindClientInPVS` `0x101d1800`
   non-null (`1026ecfb`) OR `m_bfNPCStateFlags (+0x5b64) & 1` (`1026ed03`). None of them → slot 477
   `ClearSenseConditions` (`1026ed09`, the fourteen-entry `0x105c97dc` table) and the block is skipped.
5. The block: `CheckOnGround` `0x1026e5e0` (`1026ed16`); slot 509 `ShouldPlayIdleSound` (`1026ed1f`)
   gates the cascade — state-flags bit 1 → slot 499 `IdleAgitatedSound` (`1026ed57`); else
   `m_bfAINPCFlags2 & 0x10000` (`D_CALM`) → slot 504 `UpsetSound` (`1026ed75`; the checklist had the
   polarity inverted, the second judge settled it); else a running schedule whose slot 447
   `GetLocalScheduleId(m_pSchedule+0x1c)` is `0x12f` → slot 503 `ComfortSound` (`1026eda0`); else slot
   490 `IdleSound` (`1026edac`). Then `PerformSensing` `0x1026e4f0` (`1026ee04`: `m_iIsOblivious < 1`
   gates `CAI_Senses::PerformSensing`, and slot 459 `RemoveIgnoredConditions` runs on every path at
   `1026e573`), `CAI_Memory::RefreshMemories` on slot 541 `GetEnemies` (`1026ee15`), `ChooseEnemy`
   `0x10279dd0` (`1026ee1c`), and the better-weapon search `0x1026fb40` (`1026ee23`) whose true
   answer sets `BETTER_WEAPON_AVAILABLE 0x67` (`1026ee3b`).
6. Both paths: slot 167 `GetEnemy` non-null → slot 481 `GatherEnemyConditions(GetEnemy())`
   (`1026ee5b`); a live `m_hTargetEnt (+0x5ce4)` → `CheckTarget` `0x10271d10` (`1026eebc`); on the
   Troika (`+0x98`): slot 586 `GetBestSeeUnknown` resolved → `0x1028e480` (`1026ef43`), the handle at
   `+0x6240` (`m_hMoveTargetEnt`) resolved → `0x1028e980` (`1026ef9d`), then `0x1028e790`
   unconditionally (`1026efa4`); last, slot 565 `CheckAmmo` (`1026efad`).

`0x1026fb40`, 123 bytes: slot 513 `CapabilitiesGet() & 0x200000` (`1026fb4e`); `m_flNextWeaponSearchTime
(+0x5da0) < curtime` strictly (`1026fb69`), then re-armed `curtime + 2.0` (`_DAT_10452dc4`,
`1026fb76`); `GetActiveWeapon()` null (`1026fb83`); `Weapon_FindUsable((300, 300, 100))` (`1026fba4`)
non-null answers true.

`0x1028e790`, 126 bytes: with an enemy (slot 167), `0x1028e700(ENEMY_OCCLUDED 0x48,
&m_flOccludedReportTimeE +0x62cc)` (`1028e7aa`), then, when 0x48 no longer stands, the occlusion
edge `0x10270180(GetEnemy(), false)` (`1028e7cb`); with a live `m_hTargetEnt`,
`0x1028e700(TARGET_OCCLUDED 0x49, &m_flOccludedReportTimeT +0x62d0)` (`1028e807`).

`0x1028e700`, 99 bytes (checklist-0-9's `RefreshOccludedCondition`): with the condition standing,
a stamp equal to the pooled `0.0` (`0x104454c4`, `TEST AH,0x44`) is armed to `curtime +
m_flOccludedDelay (+0x62c8)` (`1028e72a`), and the condition is CLEARED while `curtime < stamp`
(`TEST AH,5 / JP` at `1028e745`) — occlusion is reported only once it has lasted the delay. Without
the condition the stamp is zeroed (`1028e754`).

Port: `FElysiumNpcBase::GatherConditions`; the Troika half is `FElysiumNpc::Conditions19TroikaGoalUpkeep`.
`PerformSensing` runs `FElysiumNpcSenses::PerformSensing` (the `m_bCanPerformSenses` gate, `Look`,
`Listen` -- the live `Tick` without the port's committed-enemy LOS debounce, which is slot 481's),
then slot 469's base body (`BaseOnLooked`, the Troika increment being folded into `TickSight`'s
tail) and the `OnListened` base effect on `m_Conditions` (clear the ten-entry `0x105c97b4` table,
then OR the promoted `HeardConditions`). `0x1028e700` is the landed
`FElysiumNpc::RefreshOccludedCondition` (`ElysiumNpcConditionsBodies.cpp`); `0x1028e790` calls it.

**Unrecovered:** `Weapon_FindUsable`'s search (seam answering none); `UTIL_FindClientInPVS` (the live
player stands in); `0x1028e480` / `0x1028e980` are `mechanism` rows behind the 0018 nav seam
(counted, nothing moved); `CheckTarget` `0x10271d10` is lane L12's body (the call is a counted seam
until the integrator redirects it).

## `CAI_BaseNPC::GatherEnemyConditions` — `0x10270b20` (slot 481, 2807 bytes)

Called by `0x1026ec30` with slot 167's enemy. Every later "GetEnemy()" is slot 167 again (`+0x29c`),
the parameter is used where retail uses it. In order:

1. Clear `ENEMY_FACING_ME 0x56`, `BEHIND_ENEMY 0x57`, `HAVE_ENEMY_LOS 0x4a`, `ENEMY_OCCLUDED 0x48`
   (`10270b4a..10270b65`); `SEE_ENEMY 0x46` is NOT cleared, so OnLooked's bit is what the body reads.
   `0x10270aa0(this, NULL)` resets `m_hEnemyOccluder (+0x5d90)` (`10270b71`).
2. Slot 201 `FVisible(enemy, 0x2804091, &blocker, m_eEnemyOccludedCheck)`: visible zeroes
   `m_eEnemyOccludedCheck (+0x5b98)` (`10270bb1`); a miss increments it, saturating at 10
   (`10270ba6` signed `JGE`).
3. Below 10 (`10270bc3`): `SetCondition(0x4a)`; slot 363 `FInViewCone` AND slot 468
   `QuerySeeEntity` → `SetCondition(0x46)` and `0x10270180(GetEnemy(), 0)` (snapshot the enemy
   origin, clear `m_bEnemyWentOccluded`). Whatever the cone said, while `m_afMemory (+0x5d8c) &
   0x20000` is clear the outputs fire — `m_OnFoundPlayer` (only for `[enemy+0xa8] m_pPlayer`) then
   `m_OnFoundEnemy`, each carrying the enemy EHANDLE as its value, activator and caller this NPC —
   and the bit is set on EVERY pass below the limit (`10270e4c`).
4. At 10: `m_hEnemyOccluder` := the blocker (`10270be1`), `SetCondition(0x48)`, `0x10270180(GetEnemy(),
   1)` (latches `m_bEnemyWentOccluded` once the enemy drifts past 64 units — 4096 squared — from the
   snapshot); with the bit set, `m_OnLostPlayerLOS` (player only) then `m_OnLostEnemyLOS`; the bit is
   cleared unconditionally (`10270c5d`).
5. Slot 158 `IsAlive` false on the parameter (`10270e62`): `SetCondition(0x58)`, clear 0x46 and 0x48,
   RETURN.
6. `d = 0x10270890(enemy)`: the two slot-217 origins, the vertical term replaced by the gap between
   the two surrounding boxes (`E.mins.z - M.maxs.z` above, `E.maxs.z - M.mins.z` below, else 0).
7. Under 0x46: with `+0x5b98 == 0`, slot 544 `UpdateEnemyMemory` with the enemy origin when its
   velocity equals `vec3_origin` exactly, else `origin - r * velocity`, `r = RandomFloat(-0.05, 0.0)`
   on `[0x1070b244]` (pass R's correction); then the enemy's own slot 363 on this NPC
   (`[enemy+0x9c]` the combat character): true → set 0x56 / clear 0x57, else clear 0x56 / set 0x57.
8. `limit = m_flDistTooFar (+0x5de4)`, raised to the weapon's `m_fMaxRange1 (+0x8c0)` with an active
   weapon under 0x46 when that range is (ordered) at or above it (`10271180 TEST AH,5 / JP`); `d <
   limit` clears `ENEMY_TOO_FAR 0x55`, else (NaN included) sets it. The distance survives the lead arm
   in `[ESP+0x10]` (`10270efd` / `102711a0`); the C's reuse of one variable there is the
   decompiler's. Slot 544's third argument is `&enemy->m_vecVelocity (+0x3d4)`.
9. Slot 564 `FCanCheckAttacks` → slot 561 `GatherAttackConditions(GetEnemy(), d)`, else slot 560
   `ClearAttackConditions`. Then `UpdateEnemyPos` `0x10271900`. `m_pNavigator +0x34` clear AND slot 530
   `IsUnreachable(GetEnemy())` → `SetCondition(ENEMY_UNREACHABLE 0x59)`.
10. The eluded tail, only when `curtime - LastTimeSeen(GetEnemy()) > 8.0` (`_DAT_1045597c`), the enemy
    is not already eluded (`0x102e0210` on `m_hEnemy`) and 0x46 is clear: `lkp =
    GetLastKnownPosition(GetEnemy())` (`0x102dfed0`). With `+0x98` set (every Troika NPC — it is the
    NPC itself), `m_bfAINPCFlags & 0x8000 DONE_EXTRAPOLATING` marks eluded (`0x102dfd90`) and is
    cleared; on a base-only NPC a 2-D distance to `lkp` not at-or-above 48 (`_DAT_10447ee8`,
    `TEST AH,5 / JP` at `10271373`: NaN proceeds) marks eluded.
    Either way, with 0x46 clear and 0x59 set, a ray from `EyePosition` to `lkp` (mask `0x2804091`)
    whose `fraction != 1.0` marks eluded.

`CAI_Enemies` helpers, from the listing: `LastTimeSeen 0x102e0150` — the matching record's time,
else the last position-only record's, else 0.0 with `"Asking LastTimeSeen for enemy that's not in
my memory!!"`; `GetLastKnownPosition 0x102dfed0` — the record's `+0xc`, else the last
position-only record's with the "(using danger pos)" warning, else `vec3_origin`.

Port: `FElysiumNpcBase::GatherEnemyConditions`. Reproduced: the found outputs fire on every
below-limit pass while the bit is clear, not only on a cone admission; the dead enemy keeps
`HAVE_ENEMY_LOS` and has already fired its outputs; the 48-unit arm is unreachable on the Troika line.

**Unrecovered:** `CBaseEntity::FVisible`'s use of its fourth argument; the blocker cell (the port's
slot 201 cannot return one, so the at-limit write is an invalid handle); `m_fMaxRange1` (the seam
answers none, so the limit stays `m_flDistTooFar`); `m_pNavigator +0x34`; `UpdateEnemyPos` (a
`mechanism` row behind the nav seam); the `CAI_Enemies` notifier calls (`vfunc 0xe4 / 0xe8`,
`0x10316ab0 / 0x10316bc0`); the ray's `CONTENTS_MONSTER` bit (the embodiment's line query stands in).

## `CAI_BaseNPCTroika::GatherConditions` — `0x102b27f0` (slot 433, 2133 bytes)

1. Clear `INVESTIGATE_SIGHT 0x26`, `COMFORT 0x27`, `0x01..0x07` (the see-unknown family), `ON_FIRE
   0x30`, `DETECTED_ATTACK 0x0b` (`102b2863..102b28bd`).
2. `CAI_BaseNPC::GatherConditions` `0x1026ec30`, direct (`102b28c4`). Its delayed flush can re-raise
   a due 0x0b this very pass.
3. Slot 168 `GetEnemy` once (reused below); alive false → `SetCondition(0x58)`, clear 0x46, 0x48.
4. The law sweep `0x1028efc0` (`102b290f`), then the SEE_CORPSE sweep `0x1028fa50` (`102b2916`; the
   verdict called it a second law sweep): `curtime >= m_flCorpseConditionTimer (+0x6608)` re-arms it
   `curtime + RandomFloat(2.0, 2.5)`, clears `SEE_CORPSE 0x3d` / `SEE_CORPSE_FRIEND 0x3e`, and walks
   the corpse query (`0x102cabd0`, up to four) from the last: no corpse or no `+0x98` → 0x3e and 0x3d;
   slot 404 `IRelationType` 3/4 → 0x3e and 0x3d, 1/2 → 0x3d (jump table `0x1028fb20`).
5. Clear `0x15, 0x14, 0x17, 0x16, 0x19, 0x18`. `m_flInsideInterruptDistanceSqr (+0x6324) > 0`: the
   navigator's route (`0x102ee6a0`) with `nav+0x14 <` it → `INSIDE_INTERRUPT_DIST 0x15`; the enemy's
   squared slot-220 distance `<` it → 0x17; slot 293 `GetFollowerBoss`'s → 0x19. The same three on
   `+0x6328` with `>` → 0x14, 0x16, 0x18. `m_flInterruptTime (+0x632c) > 0` and `<= curtime` →
   `INTERRUPT_TIME 0x1a`.
6. The three sweeps in order: see-unknown `0x102b15c0`, comfort `0x102b1a20`, sound `0x102b1cd0`.
7. `0x10269c70` (the running program's mask lists `STOP_BACKUP 0x2c`): clear it, then with an enemy
   the X/Y dot of the normalised horizontal enemy direction with `m_vecForward (+0x6290)` AT OR BELOW
   0.707 (double `0x1049ae78`) sets it (pass R's correction); without the mask it is cleared.
8. `RefreshCombatConditions` `0x102b2570`. The eighteen `m_hBodyFireParticles (+0x0ff8)`: the first
   live one whose `+0x484` is non-zero sets 0x30 and breaks.
9. The squad sweep `0x102b2730`: only with a connected squad (`+0x5bb0 < 1`, `+0x5da4`): an enemy
   seen within 0.2 s (`_DAT_10451ab4`) of curtime sets `SQUAD_SEE_ENEMY 0x31`, and `SQUAD_LOS_ENEMY
   0x32` under `HAVE_ENEMY_LOS`; it clears neither.
10. `m_hBlockedDoor (+0x5d28)` resolving live AND (0x46 OR `NEW_ENEMY 0x54`) → reset to -1.
11. The program masks 0x0b, `curtime < +0x65c4` (the notice's expiry) and `+0x65c0` resolves → push a
    delayed 0x0b at `RandomFloat(0.9, 1.3)` (`0x102cc6c0`: at most eight entries; an existing entry
    keeps the earlier stamp) and `+0x65c4 := curtime`.
12. `curtime >= m_flWeaponThroughWallTime (+0x6600)`: re-arm `+ 3.0`, then a RAY (not a hull: zero
    extents, `m_IsRay` at `102b2ef7`) from `EyePosition` along `m_vecForward * 32` with mask `0x2000b`
    and `CTraceFilterSimple(this, 0)`; `fraction < 1.0`, allsolid or startsolid → set
    `WEAPON_THROUGH_WALL 0x3c`, else clear.
13. Any of `0x4c/0x4d/0x4e` → `m_bCondTookDamage (+0x5b80) = 1`; else a set latch raises
    `LIGHT_DAMAGE 0x4c` (the latch is not cleared here).

Port: `FElysiumNpc::GatherConditions`. `m_vecForward` is NPCThink's `AngleVectors(GetAngles())`
write, which the port never makes; the forward is derived from the angles at the two readers
(`FElysiumNpcSenses::ViewForward`). The port's notice record stores the notice stamp where retail
stores the expiry, so the expiry is read as `stamp + 5.0` and the restamp writes `curtime - 5.0`.
The wall ray, like the eluded ray and the Hengeyokai / Tzimisce throw lines, runs through
`Conditions19RayReaches` (the embodiment's `QueryLineOfSight`, world geometry only: the masks'
monster/debris bits are the named divergence). The timer tests are ordered (`FCOMP` + `AND 0x100`):
a NaN stamp skips (`102b2e10`, `1028fa6c`).

**Unrecovered:** the corpse query `0x102cabd0` (a seam answering none); the fire particles' `+0x484`
word (no port system spawns body fire particles); `nav+0x14` (the navigator's route-end distance,
read only behind the idle `0x102ee6a0` seam); who clears `0x31` / `0x32`; `0x1028efc0` is served by
the port's `ElysiumNpcWitness::GatherLawConditions`, not re-transcribed in this lane.

## Species overrides of slot 433

Every species body calls `0x102b27f0` directly (`CALL 0x10013f2f`); no intermediate class
(`CNPC_VVampire`, `CNPC_VVampireBoss`, `CNPC_VAnimal`, `CNPC_VHuman(Combatant)`, `CNPC_VBaseBoss`)
has a slot-433 body of its own.

- **`CNPC_VAndreiBlood` `0x1035d180`** (19 bytes): Troika, then clear `0x79 TIME_TO_TELEPORT`
  (`1035d18c`). No setter of 0x79 on this class exists in the image. **Unrecovered:** its producer.
- **`CNPC_VBach` `0x10365a70`** (16 bytes): Troika, then tail-jump to `0x10365a90` (Misc19's row; the
  port body is lane L11's `BachGatherCamperConditions`, which L07's walk agrees with). That routine: the
  target is the slot-167 enemy, else the local player (none → return); ten slot-201 probes with the
  probe index as the fourth argument. Seen: with `m_iWasOccluded (+0x6674)` set, clear it and, with
  `m_bCamperFlag (+0x66a0)`, stamp the selector trace (line `0x57b`) and `SetSchedule(0x15f)`
  (`10365fa5`). Unseen and newly occluded: `m_iWasOccluded = 1`, `m_flOccludeEnterTime (+0x6670) =
  curtime`, the axis-aligned area to `m_vecLastOccludeOrigin (+0x6664)` at or above 20000 (or NaN)
  resets `m_iReusedOccludeCount (+0x6678)`, below it increments and a count above 1 latches the
  camper flag; the origin is re-snapshotted. Still occluded after more than 4.0 s: the larger axis
  move under 200 latches; origin and time re-snapshotted. `m_iGrenadeActive (+0x667c)` 10 or 7
  forces the latch (and the warning, every pass). Latched: grenade active and
  `m_bBachInStartingPosition (+0x66a1)` → `ThrowGrenade("grenade_spawn_5|6|7|9|10", 300|80|-26|85|
  -26)` (8 throws nothing); grenade active, not in the starting position and `m_iBachTeleportState
  == 0` → `grenade_spawn_8` at 15 when active is 8; either reaches the tail with the grenade arm,
  anything else without it. A NEW latch plays `bach_grenade.wav` (grenade arm) or
  `bach_camp_warn.wav` on channel 2, volume 1.0, attenuation 0.8, pitch 100.
- **`CNPC_VChangBros` `0x1036b590`** (also Blade and Claw): Troika, `UpdateFacingTimer`
  `0x1036d600`, clear `0x79 TIME_TO_JUMP_ATTACK` and `0x7c TIME_TO_UNITED_ATTACK`, then
  `CheckForJumpAttack` → 0x79, `CheckForTeleport` → `0x7a TIME_TO_TELEPORT` (never cleared here: it
  latches), `CheckForUnited` → 0x7c.
- **`CNPC_VDog` `0x10374b00`**: Troika; `m_bPlayerAttackedMe (+0x6660)` → `0x7e PLAYER_ATTACKED`;
  friendship level `(+0x6664) == 6` returns; neither `HEAR_PLAYER` nor `SEE_PLAYER` → return, after
  `0x7c PLAYER_MOVEDAWAY` when snarling (`m_Activity == 0x6e`); `m_iPlayerFriendshipState (+0x665c)
  == 2` → `0x7d PLAYER_BEFRIENDED` and return; state 0 with level 5 → state 2 and 0x7d (the body goes
  on), level 4 → state 1; the local player's slot-220 distance: snarling inside
  `m_flConflictRange` → state 0 becomes 1, state 1 raises `0x78 PLAYER_TOOCLOSE`; else inside
  `m_flWarnRange` → `PLAYER_SNARL_RANGE 0x2b`; else (not NaN) → 0x7c; the distance is stored at
  `+0x667c`. The port's enum carries 0x78/0x7c/0x7d/0x7e under State19's placeholder names.
- **`CNPC_VFrenzyShadow` `0x10375ed0`**: Troika; `m_iHostileEnemyCount (+0x6664) > 1` (signed) → set
  `0x79 TWO_HOSTILES`, else clear. Ported before this story (`ElysiumNpcFrenzyShadow.cpp`); re-read
  here and found arm-for-arm; the "global event" it counted is the `ent_trace_conditions` read.
- **`CNPC_VGargoyle` `0x10378df0`**: Troika; clear `0x0f, 0x0c, 0x0d`; with an enemy, the 2-D
  distance AT OR BELOW 50.0 (double `0x104493c0`) clears `0x5f` and `0x0e`.
- **`CNPC_VGhoulCroucher` `0x1037b570`**: undisturbed (`+0x6666` clear): `NEW_ENEMY` (the previous
  pass's) → `OnDisturbed(this)` and return; else `0x79 UNAWARE` and return — the Troika body does not
  run for an unaware croucher. Disturbed: Troika, then `NEW_ENEMY` with a connected squad and an
  enemy → `SquadNewEnemy` `0x103161a0`.
- **`CNPC_VHengeyokai` `0x103803d0`**: Troika; clear `0x0f, 0x0c, 0x0d`; `CARRYING_BODY` and
  `HAVE_ENEMY_LOS` and the throw line `0x10382020` (eye + 80·right to the enemy's eye, mask
  `0x600400b`, clear) → set `HAVE_ENEMY_THROW_LOS 0x1b` (a base condition, not a species one) and
  return; else clear 0x1b.
- **`CNPC_VMingXiao` `0x10394e40`**: clear `0x77..0x7d` (`0x7e MELEE_HELPLESS` latches); Troika;
  idle returns; six attack slots through `0x10398030(i, false)` then `(i, true)` → `0x77 + i`, none
  admitting → 0x7e; then the spit test: `m_flPlayerDist > 200`, not `m_bBlockedByFriend (+0x6750)`,
  `m_hRangedWeapon (+0x6680)` and its `+0xa0` weapon, an enemy, `m_flNextPrimaryAttack (+0x730) <=
  curtime`, `curtime >= m_flSpitAttackTimer (+0x66c0)`, and the weapon's slot 364 line from this
  origin to both sides of the enemy's `BodyTarget` (± 0.3 × its OBB width) → `0x7d CAN_ATTACK_SPIT`.
- **`CNPC_VMingXiaoTentacle` `0x1039ec10`**: clear `0x77 FLEE`, `0x79 PHASE_EXPIRED`; Troika; an enemy
  with `curtime >= m_flFailedEvadeTimer (+0x6678)` and `m_flEnemyDist <= 256` → 0x77;
  `curtime >= m_flPhaseExpireTimer (+0x6674)` → 0x79.
- **`CNPC_VPedestrian` `0x103a2c30`**: clear `PASS_OUT 0x24`; Troika; under `SEE_SOUND_SOURCE 0x2d`,
  the committed sound owner `+0x5b78` equal (as resolved pointers) to the last combat or
  bullet-impact sound's owner; its combat character with an active weapon whose crime level
  (`0x102517e0 +0x3c4`) is at or above `m_iPLCriminalFleeLevel (+0x634c)` → `0x1028ea60` with its
  slot-220 origin, and `CRIMINAL_FLEE_LEVEL 0x1f`.
- **`CNPC_VSabbatLeader` `0x103a77f0`**: Troika; `CheckForJumpCondition` → `0x79 TIME_TO_JUMP`; clear
  `0x0f, 0x0c, 0x0e`.
- **`CNPC_VScurrying` `0x103ac500`** (also `CNPC_VRat`): clear `0x78 PLAYER_TOOCLOSE`; Troika;
  `curtime >= +0x6678` (no writer: always open) and the `+0x667c` target no longer detectable →
  `+0x667c := 0x103aca80()` (player 1 when detectable, else -1); a resolving `+0x667c` → 0x78.
- **`CNPC_VTzimisce` `0x103bce40`**: Troika FIRST; clear `0x77 SHOULD_DROP_BODY`, `0x1b`, `0x78
  FORCE_THROW_BODY`; carrying: the pickup target farther than 25600 squared → 0x77;
  `HAVE_ENEMY_LOS` and the throw line `0x103be630` → 0x1b; `0x103be150` → 0x78. A claw hint
  (`m_pHintNode` type 14000/14001): no slot-168 enemy → set 0x1c and 0x1d; a usable hint
  (`0x103bfc20`) → clear both; else set 0x1c and set 0x1d when the hint-to-enemy squared distance is
  below 10000, above 40000 or unordered, clearing it only inside [10000, 40000] (the second judge's
  correction). `curtime > +0x66ac`: re-arm `+ 2.5`; the program masks `CAN_POUNCE 0x23`, an enemy,
  `tzimisce_pounce` and the pounce test `0x103bf660` (a hull trace from here to the lead-translated
  last known position, both raised 0.1, inside [40000, 360000] squared, that must end on the enemy)
  → set 0x23, else clear. `curtime > +0x66b0`: `m_iShunnedFindBody = 0`, re-arm `+ 10`.
- **`CNPC_VTzimisceHeadClaw` `0x103c17f0`, `CNPC_VTzimisceRunner` `0x103c35a0`**: Troika, clear
  `0x0f`, `0x0e`.
- **`CNPC_VWerewolf` `0x103d0410`**: the enemy (slot 167) BEFORE the Troika body: set 0x46, slot 544
  with its slot-220 origin, and `m_flPlayerDist < m_flTargetHullRadius + m_flHullRadius + 1.0` →
  0x5f; Troika; with that (pre-gather) enemy, the five updaters in order (CanTeleport,
  EnemyUnreachable, DeathTriggered, CanSpecialMove, ShouldBreakHint); `0x09` → set 0x60; `0x60` →
  set 0x09; `m_pMoveHint` → `0x78 CAN_SPECIAL_MOVE`; `werewolf_force_teleport` clears 0x51, 0x52,
  0x78, 0x79.

Port notes (integration): `AngleVectors`' right vector in the port frame is Source's with Y
negated (`Conditions19SpeciesRightVector`), so the Hengeyokai start point is 80 units to the NPC's
RIGHT and Tzimisce's `-tzimisce_throw_pos_y` likewise. Croucher's `SquadNewEnemy` call counts on
State19's `SelectIdealStateSquadNewEnemyCalls`. Dog's move-away arm is the ordered `d >= m_flWarnRange`
(`10374cad..10374cbe`). The Ming Xiao, tentacle and Scurrying timers are ordered `curtime >= stamp`;
the Tzimisce pounce band refuses only an ordered `d < 40000` or `d > 360000` (`103bf740..103bf75c`),
its two `+0.1` lifts are the double `0x104493d0`; the Werewolf limit sums at x87 precision with the
double 1.0 `0x10449280` before its one `FSTP float` (`103d049c`).

**Unrecovered (species):** Andrei's 0x79 producer; Bach's FVisible probe points (the port's slot
does not vary them); Ming Xiao's weapon slot 364 line and `+0xa0`'s writer; the pedestrian weapon's
crime level (`0x102517e0 +0x3c4`); Scurrying's `+0x6678` / `+0x667c` retail names; the Tzimisce hint
usability test `0x103bfc20` (a seam answering unusable); the three Werewolf19 updaters (lane L12).

Integration: `CAI_BaseNPC::ChooseEnemy` is lane L11's retail body, called for every NPC.
