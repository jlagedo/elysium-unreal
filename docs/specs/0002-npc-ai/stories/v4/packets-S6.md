# Packets S6 — the retail settling read, sixth sitting (2026-10-04)

Read-only on source, on `Arena/` and on the briefs. Seven unknowns the V5a coders left. Every claim
carries its address. **(L)** = read from the listing or the decompilation this session, verified;
**(D)** = data read this session (staged corpus or the port's generated table); **(doc)** = already
in `docs/vtmb/`, cited, not re-read; **(I)** = inferred, with the reason. `vampire.dll` throughout.

## 1. Which mode record the NPC's wait reads — settled (L)

- **The stamp and the record are picked by different words.** `StartTask` arm `0x102a337d`:
  `0x10252450(weapon, id == 0xb1)` is `FLD [ECX + EAX*4 + 0x730]` (three instructions): the
  primary stamp `+0x730` for `0xb0`, the secondary `+0x734` for `0xb1`. `0x102c5730(npc, weapon)`
  takes **only the weapon** and asks `0x102517e0(weapon)` for the record: the task id never reaches
  it. So `0xb1` adds the *secondary stamp* to a delay rolled from the *current* mode record.
- **`0x102517e0`**: walks the three mode rows (`0x102517b0(weapon) + 0x238e0`, stride `0xec84`) and
  returns the first whose id word `row+0x104` equals **`weapon+0x848`**
  (`m_iItemCurActivateMode`, datamap). No match: row 0 copied into the static `0x1088af38` with its
  type word (`+0x108`) zeroed.
- **Writers of `weapon+0x848`, all of them** (`vtmb_grep` on the store, plus the datamap restore):
  1. the constructor `0x10250ac0`: `+0x848 = 0`, `m_iItemActivationModes[0] (+0x84c) = 0`,
     `[1] (+0x850) = 2`;
  2. `ModeDispatch 0x102383b0`: `+0x848 = m_iItemActivationModes[DAT_1088aee4]` on entry, and
     `= [0]` again at the end of its type-4 arm. `DAT_1088aee4` is a process global written by the
     four wrappers: `PrimaryAttack 0x102382f0` and the event wrapper `0x10238320` store 0;
     `SecondaryAttack 0x10238350` and `0x10238380` store 1;
  3. `CWeaponRanged::Shot 0x102387b0`: `= 2` around one call, then restored;
  4. `0x10239270` (the fire-mode toggle): flips `m_iItemActivationModes[0]` between 0 and 1 (and
     `m_iAtkMode +0x86c`), not `+0x848` itself.
  No `CWeaponMelee` body writes it: a melee weapon's word is the constructor's 0 for life.
- **For an NPC the word is `m_iItemActivationModes[0]`, always.** `CWeaponRanged::HandleAnimEvent
  0x10238160` takes the secondary wrapper only when `DAT_1088aee4 != 0 && operator+0xa8 != 0` (a
  player); an NPC operator goes through `0x10238320` (global = 0). The only NPC callers of a
  weapon's slot 326 are `PrimaryAttack` (S2). So the record is the primary mode in force — row id 0,
  or id 1 after a toggle. *(I)*: no NPC path reaches the toggle `0x10239270` (it is `ModeDispatch`'s
  type-4 arm, which needs a type-4 record under mode index 0; shipped `Toggle_Primary_Mode` is a
  secondary mode).
- **The port's `Weapon->ModeFor(EIntent::Primary)` (the mode `PrimaryModeIndex` names) is retail's
  record for both tasks.** Nothing to change. A "secondary mode in force" does not exist for an NPC.

## 2. `GetSequencesForActivity`'s fifth argument — settled (L); inert by data (doc)

- The call (`0x103ea924..0x103ea950`, the same shape at `0x10347180` and in the player's
  `0x10160f90`): `rec = 0x102517e0(weapon)`; `v = 0x10204900(weapon + 0x7a0 + rec[0]*0x44, owner)`;
  `GetSequencesForActivity(owner, activity, seqOut, weightOut, v)`.
- **`weapon+0x77c` is `CVDmg_t[3]`** (the constructor `0x10250ac0` builds three at `param_1+0x1df`,
  size `0x44`), and `+0x7a0 + k*0x44` is record `k`'s **source** sub-struct at `+0x24` (the one
  `CVDmg_t::SetSrc 0x101fa960` fills): `{kind @0, id @4, …, multiplier @0xc}`. `rec[0]` is the mode
  row's damage-record index.
- **`0x10204900(src, character)`** evaluates that source on the character: `id == −1` or no
  character → 0; kinds 0..3 → the character's stat list of that family (`+0x13bc / +0x13c0`,
  matched on list `+0x10 == 0x10202780(kind)`), `GetValue(id)`; kind 4 → the feat table
  (`0x10739d20 / 0x10739d24`) entry `id`, `0x101e56e0` (attribute + ability sums, stealth / presence
  / shaky-hands terms, clamped to `[0, cap]`); kind 9 → a function pointer. The result is scaled by
  `0x10204c20` (`× multiplier`, or `÷ −multiplier` when negative). It is the **same integer
  `GetRawAttackValue 0x10346070` returns**: the owner's rank in the weapon mode's attack feat (the
  `Close_Combat_Melee` / `Close_Combat_Brawl` / `Ranged_Combat` word of the item's `Dmg` line).
- **How it filters** (`Studio_GetSequencesForActivity 0x10427df0`): a descriptor is admitted when
  `v < 0 || seq[+0x2b8] < 0 || seq[+0x2b8] <= v` — `+0x2b8` is a per-sequence minimum rank.
- **By data it filters nothing** (doc, `mdl_v2531.md` § "The Troika block starts at 696"):
  `+0x2b8` is `−1` on 14,005 of 14,012 shipped descriptors and `0` on the 7 zero-block scenery
  idles. Every attack sequence is ungated. The port may pass any value or none; if it carries the
  argument, the value is the owner's attack-feat rank, named with `0x10204900`.

## 3. The weapon's activity translation, and `NPC_TranslateActivity` for `0x4b` — settled (L, D)

- **Order in the band** (`0x103ea810..0x103ea82d`): `t = weapon slot 361 (+0x5a4)(activity)`, then
  `act = owner slot 376 (+0x5e0)(t)`; `act` is what `GetSequencesForActivity` and slot 331
  (`+0x52c`, pushed as `(weapon, target, act, &out)`) receive. **Slot 331 `0x10347180` translates
  nothing itself**: it uses its third argument as given.
- **Slot 361 is one body for all 254 weapon classes**, `CBaseCombatWeapon::ActivityOverride
  0x1024f210` (`vtmb_slot 361`): it walks the class's 12-byte rows (slots 362 / 363, e.g. the bat's
  `0x106668a8`, `0x14d` rows) and returns the first row's target the owner can play — owner slot 376
  then `SelectHeaviestSequence`, else the same on the hands view-model — else the input unchanged.
- **The `0x4b` (`ACT_MELEE_ATTACK`, 75) ladders** (D: the generated
  `Visual/ElysiumWeaponActivityTables.cpp`, decoded from the pinned DLL; the base lists `Bases_111_A`,
  `_111_B`, `_109_A` carry `ACT_MELEE_ATTACK`, the 3-, 4- and 5-entry lists do not):

  | class | `ACT_MELEE_ATTACK` → first playable of |
  |---|---|
  | `_BaseballBat` | `_BASEBALLBAT`, `_MELEESHARED_ONEHAND`, `_KATANA` |
  | `_Knife` | `_KNIFE`, `_MELEESHARED_ONEHAND` |
  | `_Katana`, `_AVampBlade`, `_OccultBlade` | `_KATANA`, `_MELEESHARED_ONEHAND`, `_BASEBALLBAT` |
  | `_Fists` (and `ZombieFists` per `activity_enum.md`) | `_FISTS`, `_MELEESHARED_ONEHAND` |
  | `_TireIron`, `_Baton`, `_Torch` | `_TIREIRON`, `_MELEESHARED_ONEHAND` |
  | `_SeveredArm` | `_TIREIRON`, `_BASEBALLBAT`, `_MELEESHARED_ONEHAND` |
  | `_Sledgehammer`, `_FireAxe` | `_SLEDGEHAMMER`, `_MELEESHARED_TWOHAND`, `_BUSHHOOK` |
  | `_BushHook` | `_BUSHHOOK`, `_MELEESHARED_TWOHAND`, `_SLEDGEHAMMER` |
  | `_ChangBlade` | `_CHANG_BLADE`, `_KATANA`, `_MELEESHARED_ONEHAND`, `_BASEBALLBAT` |
  | `_ChangClaw` | `_CHANG_CLAW`, `_CLAWS`, `_MELEESHARED_ONEHAND` |
  | `_Claws`, `_Claws_Ghoul`, `_Claws_Protean4/5` | `_CLAWS`, `_MELEESHARED_ONEHAND` |
  | `_Sheriff_Sword` | `_SHERIFFSWORD` |
  | `_GargoyleFist`, `_HengeyokaiFist`, `_ManBatClaw`, `_MingXiaoMelee`, `_MingXiaoTentacle`, `_SabbatLeaderAttack` | no row: **`0x4b` passes through unchanged** |

  (each name is `ACT_MELEE_ATTACK` + the suffix). Classes not listed above were not opened.
- **Slot 376 on the Troika line is the identity for `0x4b` and for every translated melee
  activity.** `CAI_BaseNPCTroika 0x10295710` rewrites only activity 1 (to `0x105d`, under
  `m_bfAINPCFlags2 & 0x80000`); `CNPC_VHuman 0x103858b0` and `CNPC_VMingXiao 0x10394690` rewrite only
  `0x3d, 0x3e, 0x9f, 0xa0, 0xa3, 0xa4` and chain to the Troika body; `CAI_BaseNPC 0x10271f70` (the
  non-Troika classes) rewrites 1, 6 and `0x54` under capability `0x8000000`. `CNPC_VCamera
  0x103690c0` was not read (it holds no weapon).

## 4. The descriptor's `+0x2cc` when the model authors none — settled (D, L)

- **The raw word is `0x00800000` = `FLT_MIN` (1.1754944e−38)**, studiomdl's unset marker for this
  field, on 13,496 of 14,012 descriptors (D: `pipeline/…/formats/mdl_skel.py` `_LOW_REACH_UNSET`,
  its census and `tests/test_mdl_melee_sequence_fields.py`; not 0, not −1, not absent). 72
  descriptors state a reach with no low edge. The far word `+0x2d0` unset is `FLT_MAX`. The runtime
  reads both in place from the model image (`GetSeqDesc`); *(I)* no load-time rewrite — the only
  descriptor word the DLL is known to fill at load is the activity at `+0x2e0`.
- **`0x103ea7e0` has no sign or sentinel test** (`0x103ea98b..0x103ea9a2`): `if (seq[+0x2cc] < lo)
  lo = seq[+0x2cc]`, for every sequence with `+0x2c4 >= 1` (and `+0x10 >= 1` when the target is not
  a character). An unstated low edge therefore makes `lo = FLT_MIN`: `dist < lo` (`→ 0x5f`,
  `0x103eaad6`) is true only for `dist == 0` exactly (or a denormal), and the clamp `avg < lo → lo`
  (`0x103eaa2c`) moves `avg` only from 0 to `FLT_MIN`.
- **`0x10347180` likewise** (L): the reach bit 8 is `seq[+0x2cc] <= mag && mag <= seq[+0x2d0]`,
  raw, and the running pair is folded raw: `weapon+0x8b8 = min(self, seq[+0x2cc])`, `weapon+0x8c0 =
  max(self, seq[+0x2d0])`. So an unstated low edge drags `m_fMinRange1` to `FLT_MIN`, and **an
  unstated far edge (`FLT_MAX`) sets bit 8 for any distance and drags `m_fMaxRange1` to
  `FLT_MAX`**.
- **Verdict on the port's read:** taking the unstated low edge as `0.0` is retail's behaviour for
  every distance `> 0`; it differs only at `dist == 0.0` exactly (retail: `0 < FLT_MIN` → `0x5f`
  ahead of the `ready` arm; port: falls to the `ready` arm). The exact value is `FLT_MIN`; the
  bake's −1 is a marker, never a number to compare.

## 5. Condition `0x63` (`WEAPON_BLOCKED_BY_FRIEND`) — every producer (L)

`vtmb_grep` on the `SetCondition` call finds three bodies and no other:

1. **`CBaseCombatWeapon 0x1024f3d0`** (weapon vtable `+0x470`), reached only from weapon slot 364
   `0x1024f330` (`+0x5b0`). One line, mask `0x46004003`, the owner ignored, from the shoot position
   to the target point. Clear (fraction 1.0) or the hit entity is the owner's `GetEnemy` → true. A
   hit with no combat-character cast (`+0x9c == 0`): collision group 4 and fraction `> 0` → re-trace
   from the hit point; else `0x66` (when the set-conditions flag is up) and false. A hit character:
   owner slot 404 (`+0x650`) `IRelationType == D_HT (1)` → true (shot through); else **`0x63`** (flag
   up) and false.
2. **`CAI_BaseNPC 0x1026fcf0`** (slot 573, `+0x8f4`), the innate arm: the same line from
   `ownerPos + m_vecViewOffset`; a hit with `+0x9c` set and `IRelationType != D_HT` → **`0x63`**;
   a hit without → `0x66` and `0x10270aa0(hit)`.
3. **`GatherAttackConditions 0x1026dd10`'s own tail** (`0x1026e087`): re-raised while `curtime <
   m_flWeaponBlockedByFriendTimer (+0x5b88)` — a hold, not a source.

Both sources are reached through **slot 562 `WeaponLOSCondition 0x1026fbe0`** (one body, 77
classes): active weapon → weapon slot 364; none and capability `0x20000` → slot 573; none and no
capability → `0x42` and false; then, under capability `0x10000000`, `0x10266b10(pos, target, 0.92)`
→ `0x64` and false. **Slot 562 has exactly two dispatch sites, both in `0x1026dd10`**, on a `0x4f`
ranged answer (V5a README §1 step 5), with the set-conditions flag up.

- Inputs: the owner's shoot position, the enemy's two target points (slot 197, then slot 193), a
  world line trace with a character hit, the owner's relation to the hit character.
- **The port has the hold (3) and neither source.** Owner: **V5b** (V5's ranged half) — it is the
  ranged arm's line of fire (V5a README §7 keeps "slot 562's two muzzle traces" as the named
  stand-in P3; `spec.md` names the condition nowhere). V11's `melee_ally_in_the_way` cannot use it:
  slot 562 is asked only on a ranged `0x4f`; the melee friend test is `0x102a11d0` and slot 331's
  `0x3a`.

## 6. `m_flStealthVisionCone +0x63c8` — settled (L): the port is right

- **One writer**: `0x1028fc90`, three stores — `0x1028fc95` `+0x63cc = 0`, `0x1028fc9f` `+0x63c4 =
  1.0`, `0x1028fca5` `+0x63c8 = 1.0`. `vtmb_readers 0x63c8` and a corpus grep on the offset find no
  other store. It has **no direct caller because it is called through its thunk `0x1000ccd4`**
  (that is what the earlier reader missed): `CAI_BaseNPCTroika::RunAI 0x1028fcc0` (first call of
  every pass, before `UpdatePedestrianInfo` and the base `RunAI`),
  `CAI_BaseNPCTroika::ProcessTweakParam 0x1029aa10` (four sites) and the NPC maker's spawn
  `0x1034b7b0` (on the maker itself). The datamap saves and restores the word.
- **One reader**: slot 29 `CAI_BaseNPCTroika::GetStealthVisionCone 0x101aa630`.
- **An NPC candidate hands the player's `FInViewCone 0x10326750` 1.0** (`(*candidate + 0x74)()`
  passed as the cone scalar to `FinViewCone3dNew` / `2d`), on any NPC that has run one AI pass; a
  Troika NPC that never ran `RunAI` hands the allocation's 0. `senses.md` already carries the
  V5a integrator's correction; this confirms it and adds the two other callers.

## 7. `StudioFrameAdvance 0x1008f120`'s early-out, and the gib bit

**The early-out (L, `0x1008f1e9..0x1008f209`).** `dt = interval + curtime − m_flAnimTime (+0x174)`
(`interval == 0.0` replaced by 0.1 first); `FCOMP double [0x1044f020]`, `TEST AH,0x41; JP body`:
the body runs when `dt > 0.001` (or is NaN); **`dt <= 0.001` returns `float [0x104454c4]` = 0.0**
*(I for the cell's value: the same cell is the `m_flPrevAnimTime == 0` compare, the non-looping
cycle's lower clamp and `GetRawAttackValue`'s no-weapon answer)*. Before the test, and so on both
paths: when `m_flPrevAnimTime (+0x170) == 0`, `+0x170 = +0x174 = curtime` (`0x1008f1a5..b3`).
**Skipped by the early-out, i.e. everything else:** `+0x170 = +0x174`; the cycle (`+0x6f8`) and
`m_flAnimTime` advance; `m_bSequenceFinished (+0x65c)`; the past-half byte (`+0x568`);
`m_flYawSpeed (+0x560)` and `m_flGroundSpeed (+0x654)` (the body rewrites both on every real
advance); `OnSequenceFinished` (`0x10012bf2`, only on a false→true finish). **The normal path
returns `dt`** (`FLD [ESP+0xc]`, `0x1008f321`). So `CBaseAnimatingOverlay 0x10098bb0` hands its
layers 0.0 on an inert advance and `dt` otherwise (V4o's README item is right as written).

**The gib bit** (`brief-D` :153, S5 §9: "whether any shipped damage sets bit `0x2000`", the route
to slot 402 `0x102658f0` from `OnTakeDamage 0x1032ef60`, doc `combat-and-damage.md` § "Corpse
construction"). Read as far as data goes:

- **Item damage records cannot carry it** (L): `CVDmg_t::StrToDMGFlags 0x101fab10` maps the `Dmg`
  line's names to `0x80, 4, 2, 0x4000000, 0x40, 0x10000000, 8, 0x80000000, 0x40000000, 0x8000000`
  only. (D) the 244 staged item texts use only `DMG_SLASH, CLUB, BULLET, BUCKSHOT, BLAST, BURN,
  FAITH, FIST, SUPERCLAWBITE` (`DMG_FIST` matches no name: 0).
- **No map entity carries it** (D): over the 108 staged maps' entity tables the `damagetype`
  values are 8 (175), 0 (41), 256 (12), 64 (3), 1 (3), 32 (2), 4 (1), 32768 (1).
- **Not swept:** damage infos built in code with a literal type (`info+0x38`), and the retail
  Python scripts. So: no authored data sets the bit; a code literal is still possible. No V4d
  record depends on it (unchanged).

## Budget

No query over 10 s (the longest: the `damagetype` census over 108 `.ents` tables, 2.3 s). Three
corpus replies ran to the size cap (`vtmb_slot 361` without a class filter, twice; a grep on
`+0x2cc`); nothing was retried as-is.

## Changes to the plan

1. **Mode record** — **none.** V5a-2's `ModeFor(EIntent::Primary)` is retail for `0xb0` and `0xb1`
   alike; its comment may cite `0x102517e0` / `+0x848` / `0x10238160`. V4o **O3**: an NPC's shot is
   always dispatched with the global at 0 (`0x10238320`); do not port a secondary NPC shot.
2. **Fifth argument** — **V11-3** (slot 331) and **V11-2**: none required; the gate `seq+0x2b8` is
   −1 on every shipped attack sequence. If either carries the argument, name it the owner's
   attack-feat rank (`0x10204900`, = `GetRawAttackValue`) and leave it unfiltered with that comment.
   Other lanes: none.
3. **Activity translation** — **V11-3**: slot 331 receives the *already translated* activity; do
   not translate inside it. **V11-1 / V11-2** (and V5a-1's band, at its next touch): the band asks
   `weapon.ActivityOverride(0x4b)` then the owner's slot 376 — use the existing ladder resolver
   (`ElysiumWeaponActivityTables.cpp`); the candidates are the table above (bat →
   `ACT_MELEE_ATTACK_BASEBALLBAT`, …), and a species weapon keeps `ACT_MELEE_ATTACK`. Slot 376 is
   the identity here on the whole Troika line: no seam. If V5a-1's landed band collects sequences
   under the untranslated `0x4b`, that is a divergence for the **V5a integrator** to fix or record
   (a held bat's swing clips are `_BASEBALLBAT` rows, not `ACT_MELEE_ATTACK` rows).
4. **`+0x2cc`** — **V5a integrator** (one line in `MeleeWeaponBand`): replace the "DOUBTFUL" note —
   the raw word is `FLT_MIN`; reading it as 0.0 is retail except at `dist == 0` exactly; either use
   `FLT_MIN` for an unstated low edge or keep 0.0 with that sentence. **V11-3**: in slot 331 an
   unstated low edge compares as `FLT_MIN` and an unstated far edge as `FLT_MAX` (reach bit 8 set at
   any distance); the running pair on the weapon (`+0x8b8` / `+0x8c0`) is folded with those raw
   values — so "reach not stated" must not be read as 0 or skipped. No pipeline change (the bake's
   −1 / unset markers identify both cases).
5. **Condition `0x63`** — **V5b**: add slot 562 `WeaponLOSCondition 0x1026fbe0` whole — weapon slot
   364 `0x1024f330` → `0x1024f3d0`, the innate slot 573 `0x1026fcf0`, the `0x42` and `0x64` arms —
   replacing the occlusion-latch stand-in (P3); it is the only producer of `0x63` and `0x66` on the
   attack path, and V5a-1's friend timers stay test-driven until then. **V11-1**: do not wait on
   `0x63` for `melee_ally_in_the_way`; it is never raised on a melee answer.
6. **`m_flStealthVisionCone`** — **none** (A3 / A4: the NPC candidate's scalar is 1.0 after one
   `RunAI`; the port's per-pass write is retail's).
7. **Early-out** — **A2**: none (the inert second advance returns 0.0 and writes nothing but the
   first-advance stamp initialisation; if the port's `StudioFrameAdvance` returns the interval on
   the early-out, make it 0.0). **O1**: none — "returns 0.0 when `dt <= 0.001`, the owner still
   calls the layer body with that 0" is confirmed; the normal return is `dt`. **V4d**: none (gib
   bit: no authored source).
