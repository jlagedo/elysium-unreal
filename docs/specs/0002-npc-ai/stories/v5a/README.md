# V5a — the attack conditions' clears and timers, the wait before the shot, the cover tail's weapon read

Planner's design, 2026-10-04, `spec-0002/step-2`. **Not started.** V5a is V5's read half, pulled
ahead of V4's attack work by the owner's ruling (a story is given what it needs to be tested):
N1 and N2 of `stories/v1/triage.md` § "New reds" plus known red 2 (`spec.md` § "The known reds").
It runs as **one wave with lane A3 of V4a** (`stories/v4/brief-A3-view-cone.md`, slot 363).
V5's other half (red 4: why `0xef`'s mask does not break, `HasInterruptCondition 0x10269d30`,
packet first) stays in V5.

Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise. Line numbers
are today's and move: **re-locate every site by Grep on the function name.**
Provenance: *(read)* listing or port body read this session; *(doc)* `docs/vtmb/`, cited;
*(triage)* from `stories/v1/triage.md`, not re-read in the listing; *(I)* inferred, with its basis.

| brief | who |
|---|---|
| `brief-V5a-1.md` | coder 1: `GatherAttackConditions` whole |
| `brief-V5a-2.md` | coder 2: the wait (N2) and the cover tail's weapon read (N1) |
| `../v4/brief-A3-view-cone.md` | coder 3 (already written): slot 363 |
| `brief-V5a-integrator.md` | the wave's one integrator (integrates A3 as well) |

## 1. What retail does

**`CAI_BaseNPC::GatherAttackConditions 0x1026dd10`, slot 561** *(read: decompile and listing
`0x1026de02..0x1026e10c`)*. Called by `GatherEnemyConditions 0x10270b20` at `0x102711dc` when slot
564 `FCanCheckAttacks` passes, else slot 560 alone (`0x102711fa`). In order:

1. `dot` = (enemy origin − my origin, Z zeroed, normalised by `0x10137220`) · slot 368
   `BodyDirection2D` (`+0x5c0`) (`0x1026dd6c..0x1026ddfe`).
2. **Slot 560 `ClearAttackConditions 0x1026dc80`** (`0x1026de02`, `CALL [EDX+0x8c0]`): clears
   0x4f 0x50 0x51 0x52 0x2e 0x2f 0x62 0x63 0x64 0x65 0x66 — never the band words 0x08 / 0x5f /
   0x60 / 0x09.
3. `caps` = slot 513 `CapabilitiesGet` (`+0x804`, `0x1026de0c`), kept for both arms.
4. **The ranged arm.** `caps & 0x2000` with an active weapon: `0x10252410(weapon, 0)` (true when
   `curtime >= weapon[+0x730]`) false → `SetCondition(0x2f)`; answer = weapon slot 365 (`+0x5b4`,
   `CBaseCombatWeapon 0x1024f670`) `(enemy, dot, dist)`. Else `caps & 0x20000`: `curtime <
   m_flNextAttack (+0x1564)` → 0x2f; answer = slot 553 `RangeAttack1Conditions (+0x8a4)`. Neither
   bit: no ranged answer, straight to step 6.
5. Answer `0x4f`: slot 562 `WeaponLOSCondition (+0x8c8)` from the eye (slot 217 `(…, 1)`) to the
   enemy's slot 197 point; if it fails, **slot 560 again** (`0x1026df00`) and a second slot-562
   test to the enemy's slot 193 `BodyTarget (+0x304)`; either passing sets 0x4f; both failing sets
   nothing (`0x1026df45 JZ 0x1026df69`). Any other answer, 0 included: `SetCondition(answer)`.
6. **The melee arm, always run after the ranged one** (`0x1026df6d`): `caps & 0x8000` with an
   active weapon → weapon slot 367 (`+0x5bc`) `(enemy, dot, dist)`; else `caps & 0x80000` → slot
   555 `MeleeAttack1Conditions (+0x8ac)` `(dot, dist)`; `SetCondition(answer)`.
7. **The blocked-by-friend timers** (`0x1026dfd0..0x1026e062`). With 0x63 standing: if
   `m_flExtendedBlockedByFriendTimer (+0x5b8c) == FLT_MAX` it becomes `curtime + 2.5`
   (`_DAT_104629ec`); `m_flWeaponBlockedByFriendTimer (+0x5b88) = curtime + 1.5`
   (`_DAT_1044f02c`). Without 0x63: `+0x5b88 <= curtime` → `+0x5b8c = FLT_MAX`. Then `+0x5b8c <
   curtime` → `SetCondition(0x2e)`.
8. **The tail** (`0x1026e062..0x1026e107`). `curtime < +0x5b88` (the friend block still held):
   `SetCondition(0x63)`, clear 0x50, 0x4f, 0x52, 0x51. Otherwise (`+0x5b88 <= curtime`, NaN
   included: `TEST AH,5; JP`): if any of 0x50, 0x4f, 0x52, 0x51 stands, clear 0x08, 0x5f, 0x60,
   0x09, 0x63; if none stands, nothing.

The `(*DAT_10924a6c)->vfunc1()` calls before each `SetCondition` are a debug ConVar's
`IsCommand()` with the answer dropped: no observable.

**`TASK_WAIT_ATTACK_TIME1/2`, `StartTask` arm `0x102a337d`** *(read: listing
`0x102a337d..0x102a33f6`)*. No active weapon → complete. `m_flWaitFinished (+0x5db4) =
0x10252450(weapon, id == 0xb1) + 0x102c5730(this, weapon)`; `<= curtime` → complete (`0x102a4e51`);
else `m_pHintNode (+0x5ddc)` set → return, else `RestartIdealActivity(5)`.
- `0x10252450(weapon, i)` = `weapon[+0x730 + 4·i]`, the next primary / secondary attack stamp
  *(I: the 14-byte body returns a float; its sibling `0x10252410` reads the same cell; `(doc)`
  `ElysiumNpcConditions2Species.cpp:377` "`m_flNextPrimaryAttack (+0x730)`")*.
- `0x102c5730(npc, weapon)` *(read, listing)*: `data = 0x102517e0(weapon)` (the weapon's mode
  data); `v = RandomFloat(data[+0x264], data[+0x268])`; returns `0x102c5570(npc, data, v)`.
- `0x102c5570` *(doc: `shape.md` § "`FUN_102c5570`, which is not `IsNearShootTarget`")*:
  `scale = 1`; if `data[+0x26c] > 0`: distance to `m_hShootTargetOverride (+0x5ba8)` if it
  resolves, else to the enemy's last known position (slot 541), else `scale = sqrt(1 /
  data[+0x26c])`; `scale = dist > 0 ? sqrt(dist / data[+0x26c]) : dist`; returns `(v −
  data[+0x260]) × scale`. That section's "zero callers" is wrong: `0x102c5730`, `0x102c5780`,
  `0x102c57c0` call it.
- The words *(I: `item_w_thirtyeight.txt:100-103`'s own comment — "attack times scale down to
  Attack_Rate at a range of 0, and up proportionally" — and the client parser `client.dll
  0x101a3eb0`, which reads the three keys with defaults `2 × x`, …, 120.0)*: `+0x260`
  `Attack_Rate`, `+0x264` `NPC_Attack_Rate_Min`, `+0x268` `NPC_Attack_Rate_Max`, `+0x26c`
  `NPC_Attack_Rate_Base_Range`. **Unrecovered:** the server-side parser that fills them and the
  Min / Max defaults for an item that authors none (coder 2 reads `client.dll 0x101a3eb0` first).

**The cover chooser's tail, `0x102b7690`** *(triage N1)*: `0x102b78a2..0x102b78ee` reads
`GetEnemy()` (slot 167) → `+0x9c` (the combat-character self-cast) → `GetActiveWeapon` → weapon
slot 360 (`+0x5a0`) `& 0x6000`; non-zero → `bVar1 = false` (a ranged threat) → `0xa3` (line
`0x5cd9`) or the `0xa0` / `0xa1` roll; zero or no weapon → `0xa4` / `0xa5`.

## 2. What the port does instead

| # | port (today's line) | divergence |
|---|---|---|
| P1 | `FElysiumNpcBase::GatherAttackConditions` `ElysiumNpcBaseClosure.cpp:143-173` → `ElysiumNpcCond::GatherAttackConditions` `ElysiumNpcConditions.cpp:1053-1151` | no slot-560 call at the top (the comment at `:145-151` describes retail; the body does not do it), so every band word and CAN_* stacks from gather to gather; no second clear; **the two arms are exclusive** (`Capability != Ranged` → melee band and `return`; else ranged) where retail runs ranged then melee off the capability bits; no timers (`+0x5b88`, `+0x5b8c` have no writer but spawn: `ElysiumNpcBaseLifecycle2.cpp:265-266`); no 0x2e; no tail. An inert or unresolvable enemy returns before anything (retail has no such test: the caller holds a live enemy) |
| P2 | the same body's melee band `:1098-1114` | "CHOSEN, NOT RECOVERED": 0x60 past `MeleeReachSourceUnits`, 0x51 when facing and ready. Retail's weapon slot 367 is `CWeaponMelee 0x103eac30` → `0x103ea7e0(0x4b, enemy, dot, dist)`, which derives the bands from the swing sequences' attack data and answers 9 / 0x60 / 0x61 / 0x5f / (0x51). **Not read** (decompile damaged at `0x103ea8e5`): §7 |
| P3 | the 0x4f LOS re-test `:1137-1147` | ~~stood in by the eye's occlusion latch (named at the line); kept, in retail's position~~ ported in V4b's wave, V5a-3: slot 562 asked twice, the weapon's line of fire `0x1024f3d0` |
| P4 | `FElysiumNpc::StartTask19WeaponNextAttackTime` `ElysiumNpcStartTask.cpp:577-581` | a seam answering `curtime`: the wait completes on the frame it starts (N2). The arm around it (`:1664-1686`) is retail |
| P5 | `ElysiumItemTable.cpp:192` | parses `Attack_Rate` only; the three `NPC_Attack_Rate_*` keys are not read |
| P6 | `FElysiumNpc::SelectCoverOrKickSchedule` `ElysiumNpcSchedule.cpp:481-495` | `bNoRangedThreat` is never cleared: a stub fires "no cross-entity weapon capability reader" (N1). The reader exists: `ElysiumNpcCond::WeaponCapability(const FElysiumCombatCharacter&)` (`ElysiumNpcConditions.h:662`) |

Already retail, reuse: slot 560's body (`ElysiumNpcBaseConditions.cpp:38`), slot 553 (`:115`),
the weapon's slot 365 (`FElysiumWeapon::RangeAttack1Conditions`), `0x102c5570`'s arithmetic
(`FElysiumNpc::ScaleWeaponBurstPause`, `ElysiumNpcConditions10.cpp:430`), the shoot-target delta
(`FElysiumNpc::ShootTargetDelta`, `ElysiumNpcPositions.cpp:87`), the weapon's stamp
(`FElysiumWeapon::NextPrimaryAttackTime` / `NextSecondaryAttackTime`, `ElysiumWeaponClasses.h:437`,
written by the shot at `ElysiumWeaponClasses.cpp:2165, 2241`).

## 3. The seam

None new. `FElysiumNpcBase::WeaponBlockedByFriendTimer` / `ExtendedBlockedByFriendTimer`
(`ElysiumNpcBase.h:184-190`) and `NextAttackTime` (`+0x1564`, `ElysiumNpcConditionsBodies.inl:34`)
exist, saved and shape-mapped. Lane 2 adds three plain fields to the item mode record.

## 4. The lanes (one wave, three coders, disjoint files)

| lane | files (only these) |
|---|---|
| **V5a-1** | `ElysiumNpcConditions.h`, `ElysiumNpcConditions.cpp`, `ElysiumNpcBaseClosure.cpp`, `Tests/ElysiumNpcKernelConditionsTests.cpp`, `Tests/ElysiumNpcCombatTests.cpp` (only the assertions that drive `GatherAttackConditions`, ~:520-705) |
| **V5a-2** | `ElysiumNpcStartTask.cpp` (`StartTask19WeaponNextAttackTime` only), `ElysiumNpcStartTask.inl` (its declaration comment), `ElysiumItemTable.h`, `ElysiumItemTable.cpp`, `ElysiumNpcSchedule.cpp` (`SelectCoverOrKickSchedule`'s ranged-threat block only), `Tests/ElysiumNpcKernelStartTaskTests.cpp`, `Tests/ElysiumNpcKernelScheduleTests.cpp` |
| **A3** (V4a) | `ElysiumCombatCharacterSlots.cpp`, `ElysiumCombatCharacterSlotBodies.cpp`, `ElysiumNpcSenses.h`, `ElysiumNpcSenses.cpp`, `Tests/ElysiumCombatCharacterConeTests.cpp` — **not** `ElysiumPlayerEntity.cpp`: A3's final brief gives the player's `FieldOfView = 0.5` write to A4 (V4a), so in this wave the player's word is whatever the seam left; if `sense_enemy_facing_me` or `ranged_open_fire` stays red on that word alone, it is a placed red on A4, not loosened |

Checked by listing: no file appears twice. `ElysiumNpcBaseConditions2.cpp` (the slot-363 call
site and the gather's caller) is nobody's: both are already retail.

## 5. Records

| record | red today on | after the wave |
|---|---|---|
| `range_bands` | known red 2 (probe 16.2: 0x08 still set) | green (lane 1's tail). If it then stops on `0xef` not breaking, that is red 4 → V5, a placed red |
| `cover_armed` | N1 (`0xa4` against a pistol) | green (lane 2). Its shot, like `cover`'s, rides today's world-tick poll until V4a's A1 |
| `ranged_open_fire` | red 3 (`BEHIND_ENEMY`, slot 363), then N2 | green with A3 + lane 2. `shot_event` matches any `animevent` (the poll emits it today); R2 item (c): the integrator tightens it to `3031` |
| `sense_enemy_facing_me` | red 3 | green (A3) |
| `cover` | green | stays green; lanes 1 and 2 both move its timing (§7) |

## 6. Tests

Added, each pinning an address: `Elysium.Arm.NpcKernelConditions.GatherAttackClears`
(`0x1026de02`: a band word from the previous gather survives the top clear, a CAN_* does not;
`0x1026e0df`: with a CAN_* standing and `+0x5b88` lapsed, 0x08 / 0x5f / 0x60 / 0x09 / 0x63 go; with
none standing they stay); `.GatherAttackFriendTimers` (`0x1026dfd0..0x1026e062`: 0x63 arms `+2.5` /
`+1.5`; `0x2e` once the extended timer lapses; while `+0x5b88` holds, 0x63 is re-raised and the four
CAN_* cleared); `.GatherAttackBothArms` (`0x1026df6d`: the melee arm runs after a ranged answer);
`Elysium.Arm.NpcKernelStartTask19.WaitAttackTime` (`0x102a337d`: `m_flWaitFinished` = the stamp +
`(RandomFloat(min, max) − Attack_Rate) × sqrt(d / base)`; index 1 for `0xb1`; `<= curtime`
completes; a hint node suppresses the activity restart); `Elysium.Arm.NpcKernelSchedule.CoverTailRangedThreat`
(`0x102b78a2`: an enemy holding a `0x6000` weapon → `0xa3`, a melee or unarmed enemy → `0xa4`).

Deleted or rewritten (they pin the seam or the stacking): `ElysiumNpcKernelStartTaskTests.cpp`
~:652-657 ("a weapon's deadline answers curtime (the seam)"); any assertion in
`ElysiumNpcCombatTests.cpp` ~:520-705 that expects a condition to survive a gather that retail
clears, or the exclusive melee/ranged split; any `ElysiumNpcKernelScheduleTests.cpp` assertion that
expects `0xa4` because the stub fired. Each lane lists what it deleted.

## 7. Risks, unrecovered, for the judge

- ~~**The melee weapon's band `0x103ea7e0` is unread** and reads per-sequence attack data (seqdesc
  `+0x2bc..+0x2d0`) the bake may not carry. Lane 1 keeps the port's stand-in numbers in retail's
  position and records the divergence; a reading packet, then **the judge** (pipeline / re-bake?)
  before V4c or V11's close.~~ *(amended after S2 item 4, S4 item b and the judge's second
  sitting J14.4, 2026-10-04.)* **Read whole; the judge item is withdrawn.** Every input is
  sequence data the bake already carries (`weight`, `swings`, `LowReachCm`, `ReachCm`,
  `Envelopes`): no pipeline change, no re-bake. Constants 0.7 / 0.5 / 1.2 / 256.0 / 0.25; the
  far word is `9` beyond `max(1.2 × reach, 256)` and `0x60` between. **Lane V5a-1 states which
  port body stands for it and ports it whole** (`brief-V5a-1.md` item 4); V11's `chase_melee`
  depends on it. The records' "slot 555 `0x1026d9a0`, d <= 64" text is wrong for a
  weapon-armed NPC (it is the innate arm, `caps & 0x80000`).
- *(planner, after S5 item 4, 2026-10-04.)* **Slot 331 `ChooseMeleeAttackSequence 0x10347180`
  — the `0x51` arm's gate — is not in this wave.** Walked (`../v4/packets-S5.md` item 4); the
  bake carries every input (`LowReachCm` `+0x2cc`, `ReachCm` `+0x2d0`, `Envelopes`
  `+0x2bc/+0x2c0`, `Combo.Mask` `+0x2d4`, the movement path for `0x100c6020`): no pipeline
  change. Its stub is in `ElysiumCombatCharacterSlots.cpp` (A3's file here) and its only records
  are V11's, so the body is lane **V11-3** (`../v11/brief-V11-3-slot-331.md`) in V11's wave.
  V5a-1 calls the virtual and keeps no stand-in; the wave stays [V5a-1, V5a-2, A3].
- **Verdicts will move.** With the top clear, CAN_* and 0x2f are rebuilt every gather, and with a
  real wait every gunman fires slower. `cover`, `cover_armed`, `control_sequence` and the map
  records are re-run by the integrator; a moved timing is triaged against retail, never loosened.
- **0x2f's ranged test** in retail is the weapon's stamp; the port's matches. The innate arm's
  `m_flNextAttack` has no writer in the port (named at its declaration): unchanged.
- **Slot 562's two muzzle traces**: ported in V4b's wave, V5a-3 (`brief-V5a-3-slot-562.md`) — the
  gather asks `WeaponLOSCondition 0x1026fbe0`, the weapon's line of fire is `0x1024f330` →
  `0x1024f3d0` from slot 389 `0x103338c0`. The occlusion-latch stand-in (P3) is gone from the gather.
- ~~**`+0x260` = `Attack_Rate` and the three key offsets are inferred** from the item text's
  comment and the client parser; the server parser is unread. If coder 2's read of `client.dll
  0x101a3eb0` contradicts it, it stops and reports.~~ *(amended after S2 item 3.)* **Verified in
  both parsers** (server `0x10259230`, client `0x101a3eb0`, identical): the mapping stands, no
  stop. Defaults: `Attack_Rate` **1.0** (the port parses 0.0 — an old bug, fixed in V5a-2), Min
  `2 × Attack_Rate`, Max `3 × Attack_Rate`, Base_Range 120.0. The reader is `0x102c5730` →
  `0x102c5570`.
- *(J12.)* **No "empties the gun, then reloads" expectation, here or in V5b**: retail never
  lowers an NPC's clip (`../v4/packets-S4.md` item a), so `NO_PRIMARY_AMMO 0x40` cannot rise from
  firing. **V5b**: the reload finish as read — `TASK_RELOAD` at activity finished: `m_bInReload =
  1`, weapon slot 322 `0x10255050` (single-round weapons: player only; bulk, slot 323
  `0x102552c0`: `clip += min(Size − clip, owner's reserve)`, reserve untouched, both next-attack
  words `= curtime`), clear `0x40` / `0x41`; `WeaponFinishReload` stays a seam until then. A
  record that expects a reload after emptying a gun is a record error.
- **`UpdateBurstShootPause 0x102c5500`'s words** (`ActiveWeaponBurstPauseWords`,
  `ElysiumNpcConditions10.cpp:463`) are the same `+0x264` / `+0x268` through `0x102c5780` /
  `0x102c57c0`; still a seam answering 0 / 0. Not in V5a (no record observes it): V5.
- A3 alone moves `BEHIND_ENEMY` on every NPC in combat; any record that selected on it moves.
- Query budget: no query this plan ran passed 2 s.
