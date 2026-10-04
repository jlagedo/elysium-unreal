# V4r — packet S4: the settling reads of the judge's second sitting

Judge, second sitting, 2026-10-04, `spec-0002/step-2`. Read-only on code; no build, no run.
**(V)** verified this session in the listing or the decompilation; **(B)** bytes read from the pinned
`Vampire/dlls/vampire.dll` (image base `0x10000000`; `.rdata` / `.data` file offset = RVA; the
reading instruction proves the type); **(M)** the staged entity manifest
(`$ELYSIUM_WORK_ROOT/import/map_entities/manifest.json`, 1.9 MB: searched by pattern, never read
whole); **(D)** taken from `docs/vtmb/` or a packet, not re-read; **(P)** the port, with its line;
**(I)** inferred, with what it rests on. No query ran over 10 s. The rulings that follow from this
packet are in `stories/v1/triage.md` § "Judge's rulings, V4 — second sitting".

## a. Who fills an NPC weapon's clip (V)

`m_iMagazineCurAmts` is `CBaseCombatWeapon +0x74c`, two words; `m_iAmmoTypes` `+0x744`. The magazine
record (`0x10258ef0`, key block `Magazine`, stride `0x1093c`): `Size` → weapon data `+0x2668`,
`ammo_worth` `+0x266c`, `Default_Size` `+0x2670` (default: `Size`), `Dropped_Ammo` `+0x2674`,
`ReloadTime` `+0x2678`; `Type` (`0x105402f8`, default `"None"`) is the ammo name (B for the strings).

Writers that reach an NPC-held weapon, in the order they run:

1. **`CBaseCombatWeapon::Spawn 0x10250ce0`**, per magazine `i` in 0..1: slot 277 (`+0x454`, "uses a
   clip") false → `−1`; no weapon data → `0`; else `clip = max(clip, Size)`.
2. **`CBaseCombatCharacter::Inventory_Insert 0x10334e70`** (first call of `Weapon_Equip
   0x1032d380`), only when the owner has an NPC pointer (`+0x98`) and `0x102585c0(weapon)` (weapon
   data `+0x24d0` in 0..2): `n = max(Default_Size, 1)`; `Size == −1` → owner slot 374 (`+0x5d8`,
   give ammo) `(n, data +0x277c, 0)`; else **`clip[0] = n`**. This is the NPC's clip.
3. Nothing after that. `CWeaponRanged::Shot 0x102387b0` caps the sets by the clip and subtracts
   only inside the player block (`0x10238a15 TEST EAX,EAX / JZ` on the player pointer, then the
   cvar test, then `0x10238a4b SUB`): re-read, S2 item 2.7 stands.

The reload an NPC takes: `CAI_BaseNPC::RunTask 0x10288780`, `TASK_RELOAD` / `_NOTURN`, at activity
finished: `weapon +0x898 m_bInReload = 1` (`0x1028918d`), weapon slot 322 (`0x1028919d`), clear
conditions `0x40`, `0x41`, `TaskComplete`. Slot 322 is `0x10255050` on every weapon class:

- `m_bReloadsSingly +0x8c8` set → the whole arm needs the owner's player pointer (`+0xa8`): **an
  NPC with a single-round weapon gets nothing.**
- else: owner's combat pointer (`+0x9c`), `m_bInReload`, owner `m_flNextAttack +0x1564 <= curtime`
  → slot 323 `0x102552c0`, then both next-attack words (`+0x730`, `+0x734`) `= curtime`.
- Slot 323 (bulk): per magazine that uses a clip, `clip += min(Size − clip, owner
  m_iAmmo[type])` (`0x103346c0` = `GetAmmoCount`, owner `+0x15a4 + type × 4`);
  `RemoveAmmo` only for a player; then `m_bInReload = m_bIsJammed = m_bInterruptReload = 0`.

So: **an NPC's `CWeaponRanged` clip is `max(Default_Size, 1)` from equip to death; firing never
lowers it; `NO_PRIMARY_AMMO 0x40` (weapon slot 365 `0x1024f670`, `clip[0] < 1`) cannot rise from
firing.** A reload moves `min(missing, reserve)` with the reserve untouched; missing is 0. **(I)** an
NPC's reserve for a clip weapon is 0 (step 2 gives ammo only to a clipless weapon; the other callers
of owner slot 374 on an NPC were not enumerated). Two other decrements exist and are not on the two
maps: `CWeaponRanged_FlameThrower::Attack 0x103e2f30` (`+0x74c −= 1`, not walked for an NPC gate)
and mode type 6 (the throw, S2 item 2). The slot 325 request (`0x10239570` → `0x10254b00`) is the
player's reload start; no NPC body calls it (its one non-`Attack` candidate, `0x103ed8f0`, is a
slot-325-then-`ThinkSet(NULL)` body on another class).

Port (P): `FElysiumItem::Spawn` gives `Default_Size` (`ElysiumItemClasses.cpp:111-113`);
`FElysiumWeapon::CommitQueuedAttack` refuses a shot the magazine does not cover and **subtracts
`Ammo_Cost` for every wielder** (`ElysiumWeaponClasses.cpp:2313-2323`);
`FElysiumNpcBase::WeaponFinishReload` is a counted seam that fills nothing
(`ElysiumNpcBaseRunTask.cpp:259-264`) while the task still clears `0x40` / `0x41` (`:598-599`).

## b. The melee band `0x103ea7e0`'s five constants (B, each with its reading instruction)

| cell | instruction | type | value | use |
|---|---|---|---|---|
| `0x104492d0` | `FCOMP double ptr` (`0x103ea8a4`, `0x103eaab7`) | f64 | **0.7** | the facing dot: `> 0.7` for `0x51`, `< 0.7` → `0x61` |
| `0x104454d0` | `FMUL float ptr` (`0x103ea9e4`) | f32 | **0.5** | the envelope mean, `(rec[0] + rec[3]) × 0.5` |
| `0x1049ae90` | `FMUL float ptr` (`0x103eaa47`) | f32 | **1.2** | `hi × 1.2` |
| `0x1044ddb0` | `FLD float ptr` (`0x103eaa4d`) | f32 | **256.0** | the floor of the far limit |
| `0x10449260` | `FMUL double ptr` (`0x103eab11`) | f64 | **0.25** | `dist < mean × 0.25` → `0x5f`, else `0x60` |

So the far brawler's word is settled (S3 left it open): `dist > max(hi × 1.2, 256)` → **`9`**;
`hi < dist <= max(hi × 1.2, 256)` → **`0x60`**. Seeds: `lo` starts at 100000.0, `hi` at −100000.0
(`0x103ea904`, `0x103ea90c`). S2 item 4's arm order is confirmed against the listing.

Owner slot 331 (the `0x51` arm's gate, `out >= 0`) is `CBaseCombatCharacter::ChooseMeleeAttackSequence
0x10347180` on all 83 character classes (the player: `0x10160f90`) (V for the identity). Its body
(3,160 bytes) was not walked here; `combat-and-damage.md` § "The melee sequence selector is two
systems…" and `animation_rig_resolution.md` § "The NPC melee selector" hold it (D).

Port (P): no line under `Source/ElysiumUE/Private/Substrate` cites `0x103ea7e0`, `0x103eac30` or
`0x103eac60`. Whether an uncited body stands for the melee weapon's band was not established.

## c. The `Ray.Init` flag in `0x102fb4e0` (V, decompilation)

`0x102fb4e0` is `CAI_Node::InitLinks` (its VProf scope string). It builds three rays — the fly
link, and the two climb links — each `0x1006dec0(ray, start, end, hullMins, hullMaxs, 1, 0)`: **the
last argument, Troika's attack-extents flag, is 0 at all three sites.** S2 item 7's list is closed:
the only queries that grow a partition element by its attack extents are the player's acquire cone
(`0x1040f550`, `0x1040f080`).

## d. Who can carry flags2 `0x400` on the two witness maps (M + D)

Nobody carries it at spawn: no keyfield writes it and the one native writer is Ming Xiao's (S3
item 3). An NPC carries it only while it runs `SCHED_TROIKA_TAKE_COVER_HINT`,
`_RUN_AWAY_FROM_ENEMY` or `_TAKE_COVER_NO_AMMO`; the overlay also needs `CAP_MOVE_SHOOT`
(`npc_VHumanCombatant`, `npc_VVampire`, S3) and an active `0x6000` weapon. The placed rows that
meet the class and the weapon (`additionalequipment` is the active one):

- **`sp_tutorial_1` — nine**: `thug_3` (`item_w_thirtyeight`), `Hunter1`, `sentry3`,
  `sabbat_redshirt_1`, `_2`, `_5`, `sabbat_redshirt_2_proxy` (`item_w_mac_10`),
  `mercenary_upstairs` (`item_w_ithaca_m_37`), `condotierre_upstairs` (`item_w_steyr_aug`).
- **`sm_hub_1` — none with a ranged primary.** `Noir_Cop` (baton; `alternateequipment
  item_w_glock_17c`) and `Chunk` (no primary; alternate glock) qualify only after a weapon switch.
- Not candidates: the melee rows (`thug_2` bat; `sabbat_redshirt_3`, `_4`, `sheriff`, `Jack`,
  `Bertram` claws; `sentry2`, `monk_upstairs_podium`, `Hunterv`, `Caine`, `obfuscator` nothing),
  and every maker child (the makers' `NPCType` is `npc_VCop`, `npc_VHuman`, `npc_VRat` or
  `npc_VVampire` with `item_w_fists`; the cops' class has no `CAP_MOVE_SHOOT`, S3).
- With item a, `_TAKE_COVER_NO_AMMO` is not reachable by firing: the two live writers on these
  maps are `TAKE_COVER_HINT` and `RUN_AWAY_FROM_ENEMY`.

Class counts in the manifest (both maps): `npc_VHumanCombatant` 11, `npc_VVampire` 11,
`npc_VPedestrian` 25, `npc_VDialogPedestrian` 3, `npc_VCop` 4, `npc_VRat` 3, `npc_VTaxiDriver` 1,
`npc_maker` 62. **No `npc_VChangBros`, `npc_VFrenzyShadow`, `npc_VBach` or `npc_VManBat` row, and no
maker names one**; the reach tables' `class` rows agree (`npc-kernel/reach/*.tsv`).

## e. `CNPC_VPedestrian::CreateCorpse 0x103a38c0` (V, decompilation, whole)

Slot 301. Straight-line, no branch:

1. `m_vecPreDeathMins (+0x6660) =` the collision's slot 1 (`OBBMins`); `m_vecPreDeathMaxs
   (+0x666c) =` slot 2 (`OBBMaxs`) — taken **before** the base, which zeroes the bounds.
2. `CBaseCombatCharacter::CreateCorpse(force, info)` — the whole base body: the `OnDeath` latch, the
   bone, the three arms, and the tail (so `SUB_PVSRemove`, or `BurnModel` + `SUB_Remove`, is armed
   here).
3. `ThinkSet(this, NULL, 0.0, NULL)` — the think the tail just armed is dropped.
4. `SetSolid(SOLID_NONE)` (`0x100dc480(&m_Collision, 0)`).

It has no arm of its own: the base's player and `No_Ragdoll_Death` arms run inside step 2, and on
the static-corpse arm step 3 also drops the hidden NPC's `SUB_Remove` at +0.5 s. `Event_Killed`'s
fade step runs after slot 301 (S1 item 1), so **a pedestrian with spawnflag bit 9 still fades**
(I, from the order; no placed pedestrian on the two maps has the bit, item f.1).

## f. The rest of what S1–S3 left unrecovered

### f.1 Read this sitting

- **`SUB_FadeOut`** — thunk `0x100152b2` → **`0x10269960`** (B for the jump, V for the body):
  render alpha (`m_clrRender` byte 3) `> 7` → `alpha −= 7`, `m_flNextThink = curtime + 0.1`
  (`0x104493d0`, f64); else `alpha = 0`, `m_flNextThink = curtime + 0.2` (`0x10449198`, f64),
  `ThinkSet(SUB_Remove 0x101c0b10)`. With `SUB_StartFadeOut`'s first think at +10.0 s (S1) and
  alpha 255: 36 steps to 3, then 0, then removal — **the entity is gone about 13.8 s after the
  death, seen or not.**
- **Who has the fade bit (M + D).** No placed NPC on either map has spawnflag bit 9 (`0x200`). But
  `CNPCMaker::MakeNPC 0x1034b7b0` gives a child spawnflags `| 4`, or **`| 0x204` when `m_bFade`**
  (`lifecycle.md` :1333), and `m_bFade` is the `Flag_Fade` key or forced by `Flag_InfChild`
  (:2126). In the manifest: **`Flag_InfChild 1` on 22 makers, `Flag_Fade 1` on 3** — among them the
  tutorial's `stealth_victim_maker` and `guard_maker` (`npc_VVampire`, `TutorialShovelhead`, both
  `Flag_InfChild 1`). **Fade corpses are on both witness maps.** Port (P): `StartFadeOut` is a
  counted seam (`ElysiumNpcBaseRunTask.cpp:253-257`) with one caller, `TASK_DIE`'s arm (`:682`);
  the port's `Event_Killed` never reaches it. Whether the port's maker passes `0x204` was not
  checked.
- **`0x1010e530`** (V): fills a vector with three `rand()` draws scaled into `[lo, hi]`
  (`× (hi − lo) × 1/32767`, `0x10451abc`). `CreateCorpse` discards it: three draws from the C
  runtime's `rand`, not the engine's stream. Nothing to port.
- **`0x1013d450`** (V body, I identity): `UTIL_ApproachAngle(target, value, speed)` — both angles
  reduced to 0..360, `delta = target − value` wrapped to ±180, `speed` made positive; `delta >
  speed` → `value + speed`; `delta < −speed` → `value − speed`; else `target`. So in the turn
  script, `0x1013d450(out, in, |diff| × 0.8)` starts from `in` and moves toward `out`.
- **`0x10262c20(i, j)`** (V): `yaw = VecToYaw(pos[j] − pos[i])` (entry `+0x2c..+0x34`, stride
  `0x38`, array `+0x6038`); `tIn = |AngleDiff(yaw, yaw[i])| / 150`, `tOut = |AngleDiff(yaw[j],
  yaw)| / 150` (`0x10457f58`, f32 1/150; entry yaw `+0x10`); `T = time[j] − time[i]` (`+4`). With
  `0x1044e658` (f64 **0.01**) and `0x104491a8` (f64 **0.8**): both `>= 0.01` and `tIn + tOut <= T`
  and `< 0.8 T` → two inserted entries (`0x10262ea0(i, tIn)`, `0x10262ea0(i, tOut)`), each with
  `yaw`, return 2; only `tIn >= 0.01` and `tIn <= 0.8 T` → one at `tIn`, return 1; `tIn < 0.01` and
  `tOut <= 0.8 T` → one at `T − tOut`, return 1; else 0. (`0x101d2c70` / `0x1013d580` as
  `VecToYaw` / `AngleDiff`: I, from their use.)
- **`0x1033d940`** (V, listing): `0x101e3f50(&0x10739a4c, owner)` true → `rate × 2`; else `rate`.
  The test is a bit family on the owner's `+0xeb4` word (five levels of one status); which status
  is unrecovered. For an NPC without it, slot 332 is `Attack_Rate` unscaled.
- **The three `GetBestMeleeWeapon` tables** (B + V): `0x10619eb4` = `{1, 2, 3, 7, −1}`.
  `0x10619d28[8]` is −1 in the file and is filled, with `0x10937cd0[8]` (bss), by
  `CBaseCombatCharacter::CacheInventorySections 0x10340180`: for the eight names `None,
  Weapon_Melee, Weapon_Ranged, Weapon_Thrown, Armor, Generic, Powerups, Hidden`,
  `0x10619d28[t] =` (the index of the inventory section of that name in the list `0x1073a2e0`) `− 1`
  and `0x10937cd0[t] = that × 32` when positive, else 0. So the walk is the Melee, Ranged, Thrown
  and Hidden sections, 32 slots each, and the section order is data (the inventory-section
  definition file; not opened).
- **The mode record's key at `0x105402f8`** is the string **`Type`** (B).
- **Slot 600's other dispatch sites, two of five** (V): `0x10385a50` (the combatant's
  `OnTakeDamage`): on a hit that did damage and did not divert to the interesting-death arm,
  `0x1028e8b0(attacker, 5.0)` then slot 600 `(GetEnemy())`. `0x1029f8f0` (slot 27): `0x102bf5d0`,
  `SetCondition(10)`, slot 600 `(argument)`.
- **`debug_allow_mf_turn` / `debug_allow_move_facing`**: no file under `Content/ElysiumCorpus/`
  names either (Grep, 0 hits); the registered defaults stand.

### f.2 Not read, with the reason

| item | depends on it | why not |
|---|---|---|
| slot 331 `0x10347180`, arm by arm | V11 (the `0x51` arm) | 3,160 bytes; held in two docs (D). V11-1 reads the doc section before it trusts `0x51`. |
| `0x102beda0`, `0x10374e50`, the Werewolf's slot-600 sites | V11 | the Troika twin of `0x10385a50` (the brawler's line is `0x10385a50`, S3 item 7); the other two are classes absent from both maps. |
| `0x102b7690(0, 1, 0, 1)` for a melee NPC | V11 | walked in `programs.md` § "The cover and kick chooser…" (D); it needs a hint node, and the arena stages none (I). |
| `0x10239f30` (the type-6 launch) | none | the decompilation is truncated (S2); no row on either map holds a thrown weapon (item d). |
| `0x101c2a90` / `0x101c2ad0` flag bytes; damage bit `0x2000` sources | none of V4 / V5a / V11 / V4o | the feed packet's crime-record byte and the gib arm; neither is on a step-2 lane. |
| `0x102e0bd0` | V4b B1 | walked in `navigation-jump-links.md` § "The motor status table" and `schedule-kernel.md` :1766 (D); its five trailing arguments stay unrecovered there. |
| what reaches `0x102b5bb0` | none | no static caller and no vtable holds it (S1): the corpus has no reference to follow. |
| the female `walk_0` `animdesc.fps` bytes | A0's N13 acceptance | model bytes are not in the decompilation corpus; the staged sidecar is the pipeline decoder's reading (S1 item 7). |
| the status behind `0x1033d940` | V4o O3 (cooldown) | the bit table (`0x101e1520(…, 10, 0)`) is built at run time. |
| the inventory-section order | V11 item 11 | a data file outside this sitting's reads. |

## Changes to the plan

- **V4o O3**: the shot neither spends nor refuses on the NPC's magazine beyond the set cap (item
  a); the cooldown rate is `Attack_Rate`, doubled only under the unrecovered status (a seam
  answering false). New record `ranged_sustained_fire`. `v4o/README.md` § 1: `_TAKE_COVER_NO_AMMO`
  is not a live writer of `0x400` (item d).
- **V4c C2**: the fade (`SUB_StartFadeOut` after slot 301, `SUB_FadeOut 0x10269960`), and the check
  that the maker passes `0x204`; item 9 (the pedestrian) as item e, including the `Think` gate
  (`ElysiumNpc.cpp:645`, `bDeathCommitted ||` runs `SUB_PVSRemove` whatever the think name).
- **V4d D**: four removal clocks, not one — unseen at a 10 s poll, +10 s burn, about +13.8 s fade,
  never (pedestrian).
- **V5a-1 / V11-1**: the melee band's constants and the `9` / `0x60` split (item b); no port line
  cites the band.
- **V11-1**: `0x102a11d0` (the rulings, J10); item 11's tables are the inventory sections (f.1).
- **V4b B1**: the turn script's two helpers (f.1).
- **C1**: slot 247's data half is not run (the rulings, J2b); item c closes the reader list.
- **V8**: the hub cannot witness the move-and-shoot overlay (item d); the tutorial can.

## Budget

No query over 10 s. The entity manifest (1.9 MB) was searched with `rg -o` patterns and never
parsed whole. Two decompilation replies ran to the 20 KB cap (`0x102fb4e0`, and the slot 331 class
table, which should have been asked with a small `limit`).
