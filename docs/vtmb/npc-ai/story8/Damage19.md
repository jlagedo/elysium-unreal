# Damage19 — the damage entry chain, walked (spec 0019 story 8, lane L09)

The bodies of the NPC damage entry: slot 142 (`OnTakeDamage`), slot 390 (`OnTakeDamage_Alive`), the
three Troika melee-reaction slots 316 / 318 / 320, the species `UpdatePresenceEffect` (slot 313)
and every species override of 142 / 320 / 390. Read off the `vampire.dll` listing (`vtmb_asm`); the
port is `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseDamage19.cpp`, `ElysiumNpcDamage19.cpp`
and `ElysiumNpcDamage19Species.cpp`, the cases `Tests/ElysiumNpcKernelDamage19Tests.cpp`.

**The chain.** `CBaseEntity::TakeDamage` → slot 142. The Troika-line slot 142 (`0x102bed30`) is
the invincibility gate and tails to `CAI_BaseNPC::OnTakeDamage` (`0x10265e90`), which runs
`CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`, the life-state dispatcher into slot 390/391/392)
and then `RemoveIgnoredConditions`. Slot 390 is the Troika `0x102beda0` (the corpus labels it
"OnTakeDamage"; pass R's second judge settled it is slot 390), which caches the packet and calls
`CAI_BaseNPC::OnTakeDamage_Alive` (`0x10265ed0`), which calls `CBaseCombatCharacter::OnTakeDamage_Alive`
(`0x103302e0`, the health commit; one exit, `0x10330abe`, answering 1). Every species slot-390 body
reaches `0x102beda0` through `0x10001b45 → 0x10385a50 → 0x10012611`.

**The packet** (`CTakeDamageInfo`, 0x4c bytes; `Init` `0x101c2890`): `+0x00` the `CVDmg_t*`,
`+0x04..+0x24` force / position / reported-position vectors, `+0x28` inflictor, `+0x2c` attacker
(defaults to the inflictor), `+0x30` damage, `+0x34` max damage, `+0x38` damage bits, `+0x3c`,
`+0x40` ammo type, `+0x44` a float 1.0, `+0x48..+0x4a` three bytes. Every body reads the amount as
`CVDmg_t::GetDmg()` when word 0 is set and `+0x30` otherwise, and the bits as the descriptor's
`m_bdmgTypes` (`+0x10`) OR'd onto `+0x38`.

## `0x10265ed0` `CAI_BaseNPC::OnTakeDamage_Alive` (slot 390, 1184 bytes)

1. Slot 491 `PainSound()` (`0x10265ed9`), then `m_afMemory &= ~2` (INCOVER, `0x10265eea`).
2. `CBaseCombatCharacter::OnTakeDamage_Alive(info)` (`0x10265ef5`); 0 returns 0 (`0x10265efc`).
3. `m_OnDamaged` (this, this, 0) unless `m_flLastDamageTime == curtime` (`0x10265f1a`, `TEST AH,0x44 /
   JNP`: ordered-equal skips, unordered fires).
4. `m_OnHalfHealth` when `m_iHealth <= m_iMaxHealth / 2` (C division, `0x10265f3e`).
5. `FL_NPC` clear on this body → return 1 (`0x10265f59`). No attacker at `+0x2c` → return 1
   (`0x10265f64`, no sound).
6. Attacker flags lacking `0x2080` (`FL_CLIENT | FL_NPC`) → straight to the sound tail
   (`0x10265f74`), skipping 7–12.
7. Seen = slot 363 `FInViewCone(attacker)` && slot 201 `FVisible(attacker, 0x2804091, 0, 0)`.
   Both arms write `m_vecLastDamageAttackPos`: the inflictor's origin, or with no inflictor this
   body's origin + `_DAT_1070ba40..48` (the file-static death-throw direction) × 64.0.
8. Unseen only: with a live enemy, the attacker unknown to slot 541's memory (`0x102dfa20`) and
   `SEE_ENEMY` (0x46) clear → slot 544 `UpdateEnemyMemory(GetEnemy(), pos, &DAT_1070d1b0)`
   (`0x10266135`); otherwise membership is asked again and slot 544 gets the attacker if known,
   NULL if not (`0x1026616c`). Seen: no memory call.
9. With a live enemy (both paths): `0x102e0b40(m_pMotor)`, the enemy's LKP (`0x102dfed0`), then
   `0x102e2020(m_pMotor, &lkp, 0)` (`0x10266186..0x102661b9`).
10. `m_hLastDamageEnt` = attacker handle (`0x102661cc`), `m_bCondTookDamage = 1` (`0x102661de`).
11. Slot 576 `IsLightDamage` (`0.0 < dmg`) sets 0x4c, slot 577 `IsHeavyDamage` (`20.0 < dmg`) sets
    0x4d (`0x10266239`, `0x10266293`).
12. `m_flSumDamage` resets to this hit when `curtime - m_flLastDamageTime >= 1.0` (double
    `0x10449280`; unordered resets) else accumulates; `m_flLastDamageTime = curtime` (`0x10266310`);
    0x4e when `m_iMaxHealth * 0.3 < m_flSumDamage` (double `0x1047b868`, strict, `0x1026631d`).
13. Tail: `CSoundEnt::InsertSound(SOUND_COMBAT, origin, DAT_1072bc84, 0.2, DAT_1072bcc1, this)`
    (`0x1026635b`), return 1.

Reads `+0x5d8c`, `+0x5d98`, `+0x5d94`, `+0x208`, `+0x210`, `+0x5d44`; writes `+0x5d8c`, `+0x5b9c`,
`+0x5b7c`, `+0x5b80`, `+0x5d94`, `+0x5d98`.

**Unrecovered:** the ConVar at `DAT_10924a6c` polled (slot 1, discarded) before each SetCondition;
the runtime-filled sound cells `DAT_1072bc84` / `DAT_1072bcc1` (no corpus writer; the port uses the
`NPC_TAKE_DAMAGE` volume row).

## `0x10265e90` `CAI_BaseNPC::OnTakeDamage` (slot 142, 33 bytes)

`CBaseCombatCharacter::OnTakeDamage(info)` (`0x10265e99`), then slot 459 `RemoveIgnoredConditions`
unconditionally (`0x10265ea4`); returns the first answer.

## `0x102bed30` `CAI_BaseNPCTroika::OnTakeDamage` (slot 142, 66 bytes)

`m_bInvincible` (`+0x63d8`) clear → tail to `0x10265e90` (`0x102bed6d`). Set → when
`m_flLastDamageTime != curtime` (`0x102bed4e`): stamp it (`0x102bed56`) and fire `m_OnDamaged`
(`0x102bed63`); return 0. A refused NPC still fires OnDamaged, at most once a tick.

## `0x102beda0` `CAI_BaseNPCTroika::OnTakeDamage_Alive` (slot 390, 535 bytes)

1. Cache the whole packet at `+0x660c..+0x6656` (`0x102bedab..0x102bee48`).
2. `CAI_BaseNPC::OnTakeDamage_Alive` (`0x102bee50`); 0 → return it.
3. Amount `> 0.0` (`0x102bee84`; `<=` and unordered go to 7):
   `AddExpressionForEvent(0)` (`0x102bee8e`); `m_bfAINPCFlags & 0x40000000` → slot 144
   `Event_Killed(info)` and return (`0x102beea4`); an interesting place (`+0x62ec`) whose type has a
   death activity (`0x102daf50`): when `m_iInterestingDeathActivity == -1` (`0x102beed7`) resolve the
   type's activity name (`0x102daf20`) through `0x10412520` into `+0x6308` (`0x102beef2`); `-1` →
   DevWarning `"Can not find interest death activity '%s' in the activity list.  Dying
   immediately.\n"` and `Event_Killed` (`0x102bef13`); otherwise stamp line `0x6121` and
   `SetSchedule(0x104, false)` (`0x102bef3f`). **Every** place sub-arm returns without the tail —
   including an activity already resolved (`JNZ 0x102bef44`), which installs no program.
4. Neither → the tail.
7. Zero/unordered: `SetCondition(0x4c)` (`0x102bef5c`), `m_hLastDamageEnt` (`0x102bef6f`/`0x102bef77`),
   `m_bCondTookDamage = 1` (`0x102bef81`), then the tail.
8. Tail: a live attacker extends the FVisible override by 5.0 s (`0x1028e8b0`, `0x102bef97`), then
   slot 600 with `GetEnemy()` (`0x102befa9`). Every exit returns step 2's answer.

**Unrecovered:** the interesting-place type row's death-activity byte (`+0x19c`) and name
(`+0xd0`/`+0xd4`) have no `interestingplacetypelist.txt` key recovered; `0x10411f90` before the
DevWarning.

## `0x1029fa50` `CAI_BaseNPCTroika` slot 316 (217 bytes)

`SetCondition(0x0a BEING_ATTACKED)` first (`0x1029fa63`); null target returns. Dodge arm:
`bCheckDodge && target->IsMeleeSwingInRange(GetAbsOrigin()) && IsHoldingMeleeWeapon() && slot 325`
→ `SetCondition(0x0c)` (`0x1029faba`) and, when slot 590 `OkToInterruptForMelee` agrees, stamp line
`0x28e0` and `SetSchedule(0xd5, false)` (`0x1029faea`). Block arm: `bCheckBlock &&
IsHoldingMeleeWeapon() && slot 324` → `SetCondition(0x0d)` (`0x1029fb1f`), no range, no program.
`IsMeleeSwingInRange` (`0x10345760`): the swinger's 2-D distance to the point `<= 100.0
(_DAT_1049e048) + seqdesc(m_nSequence)+0x2d0`. `IsHoldingMeleeWeapon` (`0x10345d00`): active
weapon's slot 360 `& 0x18000`.

**Unrecovered:** the sequence descriptor's `+0x2d0` swing reach (the port answers 0).

## `0x1029fcf0` `PlayerDefenderBlockReaction` (slot 318, 137 bytes)

Null roll → false (`0x1029fcfa`); `0x1028a190` refusing → its false (`0x1029fd0c`); class
`0x103498b0(roll)` (margin `word1 - word3 - word2` against the four `Melee_Reactions` cells) 3 →
line `0x2935`, `0xd8`; else line `0x2939`, `0xd7` (`0x1029fd4d`, unforced); `m_flNextAttack =
(0.3 - 1.5) + 1.5 + curtime` (`0x1029fd52..0x1029fd6f`) — the constant-folded remains of a lerp
whose parameter is 1, so the delay is **0.3 s**, not a random draw; returns true.

## `0x102a01b0` `PlayerKnockbackReaction` (slot 320, 170 bytes)

`0x1028a190` false or `IsInDialog` → false (`0x102a01bb`, `0x102a01ca`); `TranslateActivity(act, 0)`
then `SelectHeaviestSequence(translated, -1) < 0` → false with nothing written (`0x102a01ea`);
`m_knockbackType = act` (`0x102a01f0`), slot 614 `ResetThinkTimers` (`0x102a01f6`), line `0x29b5`;
`0x8a < act < 0x94` (`0x10344da0`) → knockback velocity `0x102a0290(attacker, act)` and `0x14d`, else
`0x14c`; true.

## Species slot 320

- `0x10378d30` Gargoyle: `m_iCanKnockback` (`+0x6688`) 0 → false; `RandomInt(1,100) > 50` → false;
  else the Troika body and **true** whatever it answered.
- `0x10380320` Hengeyokai: the same with no `m_iCanKnockback` test.
- `0x103c43f0` TzimisceRunner: the Troika body with activity `0x79` replacing the caller's,
  tail-returned.

## Species slot 313 (`0x1037a5b0` Gargoyle, `0x10381b10` Hengeyokai, `0x103ab270` SabbatLeader)

`0x101e3ff0(&DAT_10739a4c, this)` — dismiss the running discipline-10 (Presence) effect through the
lazily cached masks `DAT_10739478` / `DAT_10739484` and the effect word `+0xeb4` — then zero `+0xe80`
`m_iFriendPresenceEffect`, `+0xe88` `m_iEnemyPresenceEffect`, `+0xe84` `m_flEnemeyPresencePercent`.
The leader's copy is wrapped in a scope-trace push/pop (121 bytes).

**Unrecovered:** the per-target effect word `+0xeb4` and the mask table (the port dismisses nothing).

## Species slot 390

- `0x1035e6d0` AndreiBlood (452 bytes): copy the packet; amount; stat `0x11` (cap) and `0x0f`
  (wounds). Activated (`+0x66cc`): `++m_iHitCounter` (`+0x66d8`); `cap <= wounds + amount` →
  `m_bDead` (`+0x66cd`) and neuter, else the copy goes on untouched. Not activated: always
  neutered. Neuter = copy `+0x30 = 0` and, through the SHARED descriptor, `m_iDiceAmt` (`+0x4`) and
  `m_iToHitSuccesses` (`+0xc`) = 0. Chains the copy; returns its answer.
- `0x103601a0` Animal (Dog, Rat, Scurrying, Zombie inherit): attacker's `+0xa8` equal to the local
  player → `m_bPlayerAttackedMe = 1` (`+0x6660`, never cleared here); chains unchanged.
- `0x10363c70` Bach (pass R: both readings wrong): `m_bCanFightYet` (`+0x66a8`) clear → 0;
  `+0x66a6 = 1`; bits `& 0x4000002`: `m_bShieldActive` (`+0x66a5`) → chain a copy with `+0x30`,
  `+0x34` zeroed; else zero `+0x6688` and chain; other bits → zero `+0x6690` and chain.
- `0x10378c10` Gargoyle: `m_iCanKnockback = 1`, 0 on bit `0x4000000`; unconditionally
  `AddEntityRelationship(m_hClosestPlayer or NULL, D_HT, 10)`; chain.
- `0x1037bc90` GhoulCroucher: `OnDisturbed(attacker)`, `m_bUnawareExited = 1`, chain.
- `0x103801d0` Hengeyokai: chain FIRST; attacker classname `== "point_explosion"` (case-insensitive;
  the `+0x26c` and `'*'` arms are dead for this literal) → `0x103830e0` (line `0x985`,
  `SetSchedule(0x16e, false)`, `SetSkinFadeTime(0)`, `FadeToSkin(1)`); returns the chain's answer.
- `0x1038e880` ManBat: `m_iHealth < 25` and a non-player attacker → return 1, chain refused.
- `0x10395ae0` MingXiao: a proxy (`m_iTentacleID != -1`) chains unmodified; the head copies the
  packet, takes the copy's `+0x28` object's `+0xa0` weapon, maps the hit with `0x10395650`
  (melee weapon → `0x103952b0`; else hitgroup 1→-2, 4→1, 5→0, 6→5, 7→4, 8→3, 9→2, else -1), runs
  `0x10395750` on the copy and chains the modified copy.
- `0x1039e890` MingXiaoTentacle: `m_flHideReadyTimer = 0` (`+0x6680`), line `0x549`,
  `SetSchedule(0x168, false)` on every hit, chain.
- `0x103aa480` SabbatLeader: a pure forward.
- `0x103b0e90` SheriffMan (471 bytes): copy, amount, cap, wounds; `cap <= wounds + amount` →
  `m_takedamage = 0`, `SetCondition(0x79)`, `m_bDead` (`+0x66e5`), neuter; else `m_bTeleporting`
  (`+0x66e4`) → neuter; chain the copy.

`0x103952b0` (the melee map): hitgroup 1 → -2; 4 → tentacle 1 if connected (`0x10398000`) else
falls into 8's block: 3, else 5 or -1; 5 → 0 else falls into 9's: 2, else into 7's: 4 or -1; 6 →
5 or -1; any other → `RandomInt(0,99) < tuning+0x2c` then the first connected of 1,0,3,2,5,4, else -1.

**Unrecovered:** the Ming Xiao tuning record (`0x101e8da0(0x10739d08)+0x2c`).

## Species slot 142

- `0x103cccc0` Werewolf: default-construct a packet, copy the caller's over it, zero the copy's
  `+0x44`, then chain `0x102bed30` with the ORIGINAL — observationally a pure forward; the werewolf
  does not modify incoming damage.
- `0x103e06d0` Zombie (491 bytes): stat 0x0f then 0x11; amount; head threshold `cvar - wounds + cap`
  (`DAT_1094049c`, 0 when its slot 1 answers true); `threshold < amount` → `m_bShouldGib` (`+0x66e0`);
  chain `0x102bed30` with the original; with a nonzero answer and a running schedule whose
  class-local id is not `0x161` → line `0x469`, `SetSchedule(0x164, false)`; otherwise, unless
  `m_bShouldRagdoll` (`+0x6675`), line `0x46e`, `SetSchedule(0x162, true)`; head-hit byte `+0x66e1` →
  `zombie_headshot_dmg_emitter` (nonzero answer) or `zombie_headshot_death_emitter` through
  `0x103e05e0` (a `CPASFilter` particle at the look data); returns the chain's answer.

**Unrecovered:** the ConVar object at `DAT_1094049c` (never constructed in the corpus).
