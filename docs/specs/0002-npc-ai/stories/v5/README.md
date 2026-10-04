# V5b — the rest of V5: the combat interrupts (red 4), the NPC's reload, the weapon word behind it

Planner's design, 2026-10-04, `spec-0002/step-2`. **Not started; planned only.** V5b is what V5
keeps after V5a (`../v5a/README.md`, commit `a6bd4add`: `GatherAttackConditions 0x1026dd10` whole,
N1, N2) and V5a-3 (slot 562, commit `1442fdc2`). It runs after V4d and before V6
(`spec.md` § "The sequence"). Both owner's rules were applied while planning: every retail body
below was **read from the listing this sitting** (no brief says "unrecovered"; the one body that
could not be read is named in §7 and nothing depends on it), and each deliverable names the arena
record that proves it.

Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise. Line numbers
are today's and move: **re-locate every site by Grep on the function name.**
Provenance: *(read)* listing or decompilation read this sitting; *(data)* the deployed corpus under
`Content/ElysiumCorpus/`, read this sitting; *(doc)* `docs/vtmb/` or a packet, cited, not re-read;
*(port)* the port's source, read; *(I)* inferred, with its basis.

| brief | who |
|---|---|
| `brief-V5b-1-fake-reload.md` | coder 1: the ranged pre-pass `0x102b8620`'s reload arms, the template's count |
| `brief-V5b-2-weapon-reload.md` | coder 2: the weapon side — slots 280 / 322 / 323, the per-set count, the Presence seam's text |
| `brief-V5b-3-capability-and-mask.md` | coder 3: slot 360's word behind the Motor seam, the interrupt-mask arm tests |
| `brief-V5-integrator.md` | the wave's one integrator |

## 0. What was owed to V5b, and what the reads did to it

| owed (source) | verdict after the reads |
|---|---|
| Red 4, "`0xef` is a sink: its mask not honoured for 99 s" (`spec.md` § known reds; `0018…/story8/arena-run.md` rows 36–48, 83, 85) | **Not a mask defect.** The installed text of `0xef` does not list `NOT_FACING_ATTACK` or `WEAPON_THROUGH_WALL`; the port's mask and `HasInterruptCondition` are retail's. The 99 s were `0xef task 5` = `TASK_RANGE_ATTACK1` never finishing (reds 1 and 3, fixed by V3a / V4a). §1.1. A record and two arm tests close it; no kernel code |
| `HasInterruptCondition 0x10269d30` (`spec.md` V5 box) | read; ported, verdict `present`. §1.1 |
| `TASK_RELOAD`, slots 322 / 323, `m_bInReload`, the empty-gun path, `NO_PRIMARY_AMMO` (J12; `../v5a/README.md` §7) | read whole. **Found beside it: the reload an NPC actually performs on the witness maps is the FAKE one** — `m_iFakeReloadCount +0x65f0`, counted down per `FireBullets`, re-rolled from the NPC template. The port never arms it (three seams). §1.2–§1.4. This is the larger half of the story |
| The four species with task-code fire paths (J11) | **Confirmed absent** *(data)*: no `npc_VChangBros`, `npc_VFrenzyShadow`, `npc_VBach` or `npc_VManBat` in the staged entity manifest; the reach tables carry one row naming any of them, `0x10385a10`, a slot-432 body `CNPC_VVampire` / `CNPC_VHuman` inherit, verdict `dead`. They stay filed on `spec.md`'s "on demand" line (`:812-816`). Nothing in V5b |
| The Presence rate-doubling status (`0x1033d940`; S4 f.2, S5 item 5) | **Source read**: §1.5. The writer is the discipline manager's status apply; an NPC gets the bit only when a discipline is cast on it. Stays a seam answering false, its comment corrected; owner 0006. Judge item |
| J6 (c), "the reading owed, filed to V5" (the C integrator's list) | S12 item c emptied it: the species `HandleAnimEvent` bodies are walked and ported. What the C integrator still files (the Tzimisce melee's 3045 / 3046 seam; each (model, sequence) C1's Warning names) is **read by the V5 integrator at its start**; a class on a witness map is a stop, anything else is filed on the "on demand" line |
| `ranged_open_fire`'s later waits (commit `1442fdc2`) | settled by S11 item 2.3 and landed by O3. Nothing |
| `m_flNextAttack +0x1564` "has no writer in the port" (`../v5a/README.md` §7) | the innate arm (`caps & 0x20000`) is no class on the two maps; slot 322 reads the word (§1.3) and the port's word answers 0, which is retail's "`<= curtime`". Named at the line, unchanged |

## 1. What retail does

### 1.1 The interrupts *(read: `0x10269d30`, `0x1026a0f0`, `0x102ad140`, `0x10387520`; data: the `.sch` texts)*

- **`HasInterruptCondition 0x10269d30`**: `m_pSchedule (+0x5c38) == NULL` → false; the id translated
  through slot 580 (`+0x910`) when below 1e9; true iff the bit stands in **both** the condition set
  `+0x5c5c` and `m_ScheduleTestBits +0x5c74`. It never reads the inverted mask `+0x5c8c`.
- **`CacheInterruptConditions 0x1026a0f0`**: stamps `m_flCacheInterruptTime`; no schedule → both
  masks zeroed; else `+0x5c74 = schedule[10..15]`, `+0x5c8c = schedule[0..5]`, then slot 453
  `BuildScheduleTestBits` (`0x1026a211`), slot 411 (`0x1026a21b`, `0x10280fd0` on the whole NPC
  line), `SetScheduleTestBits(0x75)` (`0x1026a225`).
- **Slot 453** `CAI_BaseNPCTroika 0x102ad140`: adds `0x1e 0x1f 0x21` (not investigating / fleeing,
  not busy with a discipline, not possessed, no hint of type `0x2774`); with no enemy `0x72` under
  state-flag bit 4 and `0x20 0x22` under bit 5; `0x27` unless cowering; removes `0x31` under
  flags2 `0x40`. `CNPC_VHumanCombatant 0x10387520` adds `0x3e` under slot 158 and slot 464 == 1.
  **None adds `0x61`, `0x3c`, `0x08` or `0x5f`.**
- **The texts** *(data, `Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/`)*:

  | program | interrupts |
  |---|---|
  | `0xef` `SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1` | `NEW_ENEMY ENEMY_DEAD LIGHT_DAMAGE HEAVY_DAMAGE ENEMY_OCCLUDED NO_PRIMARY_AMMO` |
  | `SCHED_TROIKA_RANGE_ATTACK1` | the same six **+ `TOO_CLOSE_TO_ATTACK`** |
  | `0xf0` `SCHED_TROIKA_FORCED_RANGE_ATTACK1` | `ENEMY_DEAD` |

  So in retail `0xef` runs through `NOT_FACING_ATTACK 0x61`, `WEAPON_THROUGH_WALL 0x3c` and
  `TOO_CLOSE_FOR_RANGED 0x08` to its last task. `0x3c`'s one producer is
  `CAI_BaseNPCTroika::GatherConditions 0x102b27f0` (`0x102b2dfa..0x102b2fd6`); it is read by the
  ranged selectors only (`→ 0xb8`, and the dodge test `0x102b7f40`), at a selection.

### 1.2 The ranged pre-pass's reload arms, `0x102b8620` *(read, whole)*

Offered first by all six ranged selectors (doc: `conditions-and-states.md` § "`FUN_102b8620`").

1. **The fake reload** (`0x102b8626`): `debug_allow_fake_reload` (`DAT_10923d3c`, default 1), an
   active weapon, the weapon's slot 360 (`+0x5a0`) `& 0x6000`, and **`m_iFakeReloadCount (+0x65f0)
   < 1`** → **`0x102c54c0(this)`** (the re-roll), then `m_pHintNode (+0x5ddc) == 0` → `0xc4`
   `SCHED_TROIKA_HIDE_AND_FAKE_RELOAD1` (line `0x5e8f`), else `0xc6`
   `…_FAKE_RELOAD_COVER` (`0x5e93`).
2. `0x102b86ba`: a weapon with `clip[0] (+0x74c) > 0` → 0 (the pre-pass declines).
3. **The real reload** (`0x102b86e5`): the weapon's slot 280 (`+0x460`) `(0)` true, and
   `GetAmmoCount(weapon +0x744)` (`0x103346c0`) `>= 1` → `SEE_ENEMY 0x46` or `NEW_ENEMY 0x54` →
   `0xc2` `…_HIDE_AND_RELOAD1` (`0x5ecb`), else `0xc3` `…_HIDE_AND_RELOAD2` (`0x5ecf`).
4. The draw, the melee switch, the spacing trio, `0x98` — ported, not V5b's.

- **`0x102c54c0`** (43 bytes): `m_iFakeReloadCount = RandomInt(tpl[+0x34], tpl[+0x38])`, `tpl =
  0x10207c40(this)` (`GetCharTemplate` through the manager `0x101d5e80`).
- **The template words** *(read: `0x101d3f10`, the template loader, `General` block)*: `+0x34 =
  ftol(GetFloat("NpcFakeReloadCountMin", 8.0))`; `+0x38 = ftol(GetFloat("NpcFakeReloadCountMax",
  (float)that Min))`. *(data)* 20 `npctemplate*.txt` files author Min (1..20); none authors Max.
  `TutorialThug` (`npctemplate_tutorial.txt:32`): `6.0`.
- **The count-down**: `FireBullets 0x10268900` (slot 185), first statement, `0x10268919 DEC
  [m_pBaseNPCTroika + 0x65f0]` — once per call, i.e. once per bullet set (`Shot 0x102387b0` step 9).
- **The programs** *(data)*: `0xc4`: `STOP_MOVING`, `SET_FAIL_SCHEDULE …FAKE_RELOAD2`,
  `FIND_COVER_FROM_ENEMY`, `SET_NPC_FLAG FORCE_RELAXED_ANIMS`, `RUN_PATH`, `WAIT_FOR_MOVEMENT`,
  `REMEMBER INCOVER`, `SET_SCHEDULE …FAKE_RELOAD2`; interrupt `HEAVY_DAMAGE`. `0xc5`
  `…FAKE_RELOAD2`: `FACE_ENEMY`, `PLAY_SEQUENCE ACT_RELOAD_FAST`; `HEAVY_DAMAGE`. `0xc6`: the
  `PLAY_SEQUENCE` alone. No `TASK_RELOAD` in any of the three: **the clip is not touched.**

So a retail gunman fires `Min..Max` sets, runs for cover (or, with none, fails over to `0xc5`),
plays the fast-reload clip and resumes. J12 stands — `NO_PRIMARY_AMMO` cannot rise from firing —
and this is the reload the player sees.

### 1.3 `TASK_RELOAD` and the weapon's finish *(read: `0x10288780` `0x102890f3..0x102891b9`, `0x10255050`, `0x102552c0`, `0x10253ab0`)*

- `StartTask` arm (`0x102842cf`, *(port)*): `RestartIdealActivity(ACT_RELOAD)`.
- `RunTask` arm: `AutoMovement` (`0x102890f5`); unless `TASK_RELOAD_NOTURN 0x3d`: motor stop,
  ideal yaw to the enemy's last known position; slot 251 false → return; an active weapon →
  **`weapon +0x898 m_bInReload = 1`** (`0x1028918d`), **weapon slot 322 `(+0x508)`**
  (`0x1028919d`), clear `0x40`, clear `0x41`; `TaskComplete`. No weapon: `TaskComplete` only.
- **Slot 322 `0x10255050`**. `m_bReloadsSingly (+0x8c8)` clear: owner (`0x102521f0`) with a combat
  pointer (`+0x9c`), `m_bInReload`, and owner `m_flNextAttack (+0x1564) <= curtime` → **slot 323
  `(+0x50c)`**, then both next-attack words (`+0x730`, `+0x734`) `= curtime`. `m_bReloadsSingly`
  set: every arm needs the owner's **player** pointer (`+0xa8`) — an NPC gets nothing, and
  `m_bInReload` stays 1.
- **Slot 323 `0x102552c0`** (owner resolved, else nothing). Not single: for magazine `i` in 0..1
  with slot 277 (`+0x454`) "uses a clip": `n = min(slot 275 (+0x44c)(i) − clip[i],
  GetAmmoCount(m_iAmmoTypes[i]))`; `clip[i] += n`; `RemoveAmmo(n)` only for a player owner under
  the cvar `DAT_1088aef4` (`IsCommand()` or int `< 1`). Single: `0x10254cd0(this, 0xc3)`. Then
  `m_bInReload = m_bIsJammed = m_bInterruptReload = 0`.
- **Slot 280 `0x10253ab0(i)`** (the pre-pass's "may reload"): `m_iAmmoTypes[i] < 0`
  (`0x10253b40` false) → 1; uses a clip and `clip[i] > 0` → 1; an owner whose
  `GetAmmoCount(type) > 0` → 1; else 0.
- `NO_PRIMARY_AMMO 0x40`'s producer is the weapon's slot 365 `0x1024f670` (`clip[0] < 1`), landed
  by V5a. On the two maps nothing lowers an NPC clip (S4 item a; the flamethrower's `Attack
  0x103e2f30` does and is on neither map, S5 item 5).

### 1.4 The weapon's capability word, slot 360 *(read)*

`+0x5a0`: `CBaseCombatCharacter` / NPC line `0x1014f930` → 0; `CBaseCombatWeapon 0x10149e80` → 0;
the weapon subclasses answer their word (`0x6000` ranged, `0x18000` melee; the port's
`ElysiumNpcCond::CapabilityBits(WeaponCapability(…))`, already "the real word" at ten sites).
Slot 513 `CapabilitiesGet` is `m_afCapability | activeWeapon->slot360()`.

### 1.5 The status that doubles the attack rate *(read: `0x101e3560`; doc: S5 item 5)*

`0x1033d940(owner, rate)` doubles under `0x101e3f50(&DAT_10739a4c, owner)`: a Presence level bit
(discipline id 10) in `m_iDisciplineFlags2 (+0xeb4)`. **The word's writers**: zeroed by
`CAI_BaseNPCTroika::NPCInit 0x1029a0b0` and `0x10326de0`; cleared by `RemoveDiscFlag 0x1033d190`;
**set only by `CBaseCombatCharacter::AddDiscFlag 0x1033cfb0`**, called from the discipline
manager's status apply `0x101e3560(manager, cc, bit)` (when `HasStatusEffect` is false: `AddDiscFlag(cc,
bit, 0.1, cc, −1)`, the record's effect `0x101dd090`, `AddMiscFlag(0x200000)`, an AI sound when
the record's `+0x35` is set) and `0x101dfc20`; `0x101e3560`'s callers are `0x101e2f50`,
`0x101e3380`, `0x101e33c0`, `0x101f8620` (the discipline activation bodies). So an NPC carries the
bit only while a Presence effect is applied to it by a cast: spec 0006's substrate.

## 2. What the port does instead

| # | port (today's line) | divergence |
|---|---|---|
| P1 | `FElysiumNpc::RangedWeaponPrePass` `ElysiumNpcCombat10_2.cpp:132-152` | the fake-reload gate reads `ActiveWeaponCapabilityWord()`, a seam answering 0, so **the arm never fires**; and where retail calls `0x102c54c0` it counts `++RangedReloadPrepCalls` — no re-roll |
| P2 | `FElysiumNpc::CharTemplateFakeReloadRange` `ElysiumNpcConditions10.cpp:521-529` | a seam answering 0 / 0 ("`FElysiumClanTemplate` exposes no such columns"): `ResetFakeReloadCount` (`:531`, called at `ElysiumNpcLifecycle2.cpp:405`) rolls `RandRange(0, 0)`. The columns are `General` keys the footstep reader already takes the same way (`ElysiumFootsteps.cpp:15-43`, `FElysiumClanTemplate::GeneralFloat`) |
| P3 | the shot's per-set loop, `ElysiumWeaponClasses.cpp` (`TraceShotImpact`, in the queued-attack commit ~:2637-2642, and `ShotFromAnimEvent`'s stage ~:1557) | stands for slot 185 per set and **never counts down**. `FElysiumEntity::FireBullets` (`ElysiumEntitySlotBodies.cpp:617`), which does (`:628-631`), has no caller. So with P1 alone opened, every gunman would fake-reload at once and for ever (the count stays 0): **P1, P2 and P3 land together or not at all** |
| P4 | `FElysiumNpc::ActiveWeaponWantsReload` `ElysiumNpcCombat10_2.cpp:116-122` | a seam answering false for the weapon's slot 280: the real-reload arm is unreachable |
| P5 | `FElysiumNpcBase::WeaponFinishReload` `ElysiumNpcBaseRunTask.cpp:259-264` | a counting seam: no `m_bInReload`, no slot 322 / 323. The task arm around it (`:577-603`) is retail |
| P6 | `FElysiumNpcBase::ActiveWeaponCapabilityWord` `ElysiumNpcBaseMotor.cpp:361-366` | answers 0. Its remaining readers after V11 and V4o: the pre-pass (P1), `CapabilitiesGet` (`ElysiumNpcBaseMisc.cpp:53`, slot 513), the aim-activity gates (`ElysiumNpcAnim10.cpp:814`, `ElysiumNpcHuman.cpp:307`), Yukie (`ElysiumNpcYukie.cpp:88`), the frenzy shadow (`ElysiumNpcFrenzyShadow.cpp:451`), the man-bat (`ElysiumNpcRunTaskSpecies.cpp:1097`). `FElysiumNpc::SelectActiveWeaponWord` (`ElysiumNpcSelect.cpp:230`) is the same read, real |
| P7 | `FElysiumWeapon::PresenceDoublesAttackRate` `ElysiumWeaponClasses.cpp:1394-1401`; `RangedDisciplineGate` `ElysiumNpcCombat10_2.cpp:~85` | seams answering false; their comments say "nothing writes a Presence bit … the bit values are the run-time table's". Kept false; the text takes §1.5 |
| — | `ElysiumSchedule::EffectiveInterrupts` / `HasInterruptCondition` (`ElysiumSchedule.cpp:223-249`), `FElysiumNpc::BuildScheduleTestBits` (`ElysiumNpc.cpp:1547-1592`), `CacheInterruptConditionsForMaintenance` (`ElysiumNpcMaintain.cpp:316-332`) | **retail** *(port, read against §1.1)* in the mask and its two testers. One thing is not: the cache's last two lines (`:330-331`, `RemoveIgnoredConditions()` "`0x1026a267`, slot 459" and `Conditions.Clear(NpcFreeze)` "`0x1026a274`") cite addresses past `0x1026a0f0`'s `RET` (`0x1026a232`), in bytes no function of the corpus index covers; the listed tail is slot 453, the empty slot 411 `0x10280fd0`, `SetScheduleTestBits(0x75)`. Not read, so not changed: lane 3 states it at the line; §7, for the judge |

Already retail, reuse: the task arms (`ElysiumNpcBaseStartTask.cpp:1200`, `ElysiumNpcBaseRunTask.cpp:577`),
`ResetFakeReloadCount`, `m_iFakeReloadCount` (saved, `ElysiumNpcKernelBindings.cpp:1106`),
`ActiveWeaponFirstAmmoEntry` / `ActiveWeaponReserveAmmo`, the player's `BeginReload` /
`CommitQueuedReload` (not touched).

## 3. The seam

None new on the kernel. One shared name, declared by lane 2 and called by lanes 1 and 2:

```cpp
// ElysiumWeaponClasses.h, FElysiumWeapon
bool CanReloadMagazine(int32 MagazineIndex) const;   // slot 280, 0x10253ab0
void FinishReload();                                 // slot 322, 0x10255050
void FinishReloadBulk();                             // slot 323, 0x102552c0
bool bInReload = false;                              // +0x898 m_bInReload
```

The port's weapon carries one magazine (`FElysiumItem::MagazineCount`, index 0): magazine 1 is a
named seam answering "uses no clip" (`slot 277`), stated at the line. Left answering nothing, with
the retail field named: Presence (`m_iDisciplineFlags2 +0xeb4`, §1.5).

## 4. The lanes (one wave, three coders, disjoint files, then one integrator)

| lane | files (only these) |
|---|---|
| **V5b-1** | `ElysiumNpcCombat10_2.cpp` (`RangedWeaponPrePass`'s first three arms, `ActiveWeaponWantsReload`, `RangedDisciplineGate`'s comment), `ElysiumNpcCombat10.inl` (their declaration comments, `RangedReloadPrepCalls`), `ElysiumNpcConditions10.cpp` (`CharTemplateFakeReloadRange`, `ResetFakeReloadCount`'s comment), `ElysiumNpcConditions10.inl`, `Tests/ElysiumNpcKernelCombat10Tests.cpp`, `Tests/ElysiumNpcKernelConditions10Tests.cpp` |
| **V5b-2** | `ElysiumWeaponClasses.h`, `ElysiumWeaponClasses.cpp` (the three new bodies, the per-set count-down, `PresenceDoublesAttackRate`'s comment), `ElysiumNpcBaseRunTask.cpp` (`WeaponFinishReload` only), `ElysiumNpcBaseRunTask.inl` (its declaration), `Tests/ElysiumWeaponTests.cpp`, `Tests/ElysiumNpcKernelRunTaskTests.cpp` |
| **V5b-3** | `ElysiumNpcBaseMotor.cpp` (`ActiveWeaponCapabilityWord` only), `ElysiumNpcBaseMotor.inl` (its declaration comment), `ElysiumNpcMaintain.cpp` (a comment at `CacheInterruptConditionsForMaintenance`'s last two lines only), `Tests/ElysiumNpcKernelMotorTests.cpp`, `Tests/ElysiumNpcKernelSpeciesTests.cpp`, new `Tests/ElysiumNpcKernelInterruptMaskTests.cpp` |

Checked by listing: 18 paths (6 + 6 + 6), none twice. `ElysiumEntitySlotBodies.cpp` (`FireBullets`),
`ElysiumNpcSelect.cpp`, `ElysiumNpcLifecycle2.cpp`, `ElysiumSchedule.{h,cpp}`, `ElysiumNpc.cpp`,
`ElysiumRulebook.{h,cpp}`, `ElysiumFootsteps.cpp` and `Arena/` are nobody's. Lane 1 does not depend
on lane 3: its gate reads `SelectActiveWeaponWord()`, the same word.

## 5. Records ("testable first")

| record | state | proves | after the wave |
|---|---|---|---|
| **`ranged_step_back_holds`** (new; the integrator writes it and runs it **before** the build) | expected green on today's tree | red 4: `0xef` runs to its end through `NOT_FACING_ATTACK`; a listed condition breaks it | green; red 4 closed as "not a mask defect" with the trace. Red → the trace line places it, the wave goes on |
| **`ranged_fake_reload`** (new, red with `known_red: "V5b lanes 1 + 2"`) | red: no `0xc4` ever | §1.2 end to end: six sets, the re-roll, `0xc4` → `0xc5`, `ACT_RELOAD_FAST`, firing resumes | green |
| `ranged_sustained_fire` | green | J12 | **record correction** (retail source §1.2): shots 7 and 8 follow a fake reload — `shot_7`'s `within` widens to admit `0xc4` + `0xc5`; its `about` gains the sentence; its three `never` stand (`0x40`, `task_reload`, `BEHIND_ENEMY`) |
| `cover`, `cover_armed`, `cover_move_shoot`, `ranged_open_fire`, `range_bands`, `ranged_friend_in_line_of_fire` | green | — | re-run by name. Each cast is a `TutorialThug` (count 6): a run long enough for seven sets now meets a fake reload. A moved verdict is read against §1.2, never loosened |

The staging and the `expect` / `never` of the two new records are in `brief-V5-integrator.md`.

**What cannot be tested end to end, and what would have to move** (the owner's rule):

- **The real reload's finish** (§1.3, lane 2's slots 322 / 323 and lane 1's arm 3). It needs an
  NPC whose clip reaches 0. On a step-2 path nothing lowers one; the one retail spender is
  `CWeaponRanged_FlameThrower::Attack 0x103e2f30` (S5 item 5), a weapon no row of either map
  holds, whose fire path (the flame, not a `Shot`) belongs to no story of spec 0002. **Pulling it
  forward would mean porting the flamethrower's attack**: not proposed. Proof is the arm tests
  only — stated, not hidden (the J2b precedent). **For the judge**: *implement now with arm tests*
  (the planner's proposal: ~40 lines against a read body, replacing a seam whose comment already
  carries the read) or *leave the seam*.
- **Presence** (§1.5): needs a discipline cast on an NPC — spec 0006. Not pullable; the seam's
  text is corrected and the item is filed with 0006. **For the judge** (later phase).
- V6 (save / resume): nothing — `m_iFakeReloadCount` is already a saved field; `bInReload` is a
  weapon word set and cleared inside one `RunTask` call for an NPC. V7, V10, V12, 0015: nothing.

## 6. Tests

Added, each pinning an address:

- `Elysium.Arm.NpcKernelCombat10.FakeReloadArm` (`0x102b8626`): a `0x6000` weapon and count `< 1`
  → `0xc4` with no hint, `0xc6` with one, and the count re-rolled inside `[Min, Max]`; count `>= 1`
  → the arm is skipped; the cvar at 0 → skipped; a melee weapon → skipped.
- `.RealReloadArm` (`0x102b86e5`): clip 0, slot 280 true, reserve `>= 1` → `0xc2` under `SEE_ENEMY`
  or `NEW_ENEMY`, else `0xc3`; reserve 0 → 0; clip `> 0` → 0.
- `Elysium.Arm.NpcKernelConditions10.ResetFakeReloadCount` **rewritten** (`0x102c54c0`,
  `0x101d3f10`): `NpcFakeReloadCountMin 6.0` and no Max → exactly 6; no key → 8; Min 4, Max 9 →
  inside `[4, 9]`; no template → 8.
- `Elysium.Arm.Weapon.FakeReloadCountPerSet` (`0x10268919`): an NPC wielder's count drops by one
  per bullet set of a committed shot; the player's shot touches no count.
- `Elysium.Arm.Weapon.ReloadFinish` (`0x10255050`, `0x102552c0`): bulk — `clip += min(Size − clip,
  reserve)`, an NPC's reserve untouched, both stamps `= curtime`, `bInReload` cleared; a
  single-round weapon on an NPC — nothing moves and `bInReload` stays 1; `m_flNextAttack > curtime`
  → nothing. `.CanReloadMagazine` (`0x10253ab0`): the four arms.
- `Elysium.Arm.NpcKernelRunTask19.ReloadFinish` (`0x1028918d..0x102891b9`): `bInReload` set before
  slot 322; `0x40` and `0x41` cleared; no weapon → complete, nothing cleared.
- `Elysium.Arm.NpcKernelInterruptMask.RangedPrograms` (the `.sch` texts): `0xef`'s cached mask is
  its six plus slot 453's overlay and `0x75`, and holds none of `0x61`, `0x3c`, `0x08`, `0x5f`;
  `RANGE_ATTACK1`'s holds `0x5f`; `0xf0`'s text is `ENEMY_DEAD` alone. `.Testers` (`0x10269d30`):
  no schedule → false with the bit set; the bit in the set and not the mask → false.
- `Elysium.Arm.NpcKernelMotor.WeaponCapabilityWord` (slot 360): `0x6000` for a firearm, the melee
  word for a bat, 0 unarmed; slot 513 carries it.

Deleted or rewritten (they pin the seams): `ElysiumNpcKernelConditions10Tests.cpp` ~:693-696 ("is
rerolled … 0"); the `RangedWeaponPrePass` assertions of `ElysiumNpcKernelCombat10Tests.cpp`
~:1040-1056 that pass only because the gate reads 0; `ElysiumNpcKernelSpeciesTests.cpp` ~:528 (the
`0x18000` gate "closed by the seam"); any `ElysiumNpcKernelMotorTests.cpp` assertion on the word
being 0; any `WeaponFinishReloadCalls` count. Each lane lists what it deleted.

## 7. Risks

- **Every gunman's timing moves.** After six sets a `TutorialThug` leaves the line for ~2–4 s.
  `cover`'s and `cover_move_shoot`'s later shots, and any map record that watches a gunfight, move.
  Triaged against §1.2.
- **`ACT_RELOAD_FAST` on the body.** `0xc5` is `TASK_PLAY_SEQUENCE ACTIVITY:ACT_RELOAD_FAST`; if the
  regular body's vocabulary resolves no sequence for it under the held weapon, the task's own
  retail refusal runs. The integrator reads the trace's `sequence` line; a miss is triaged (the
  weapon ladder for `ACT_RELOAD_FAST`, `Visual/ElysiumWeaponActivityTables.cpp`), not patched.
- **Sets per event** *(I)*: the record expects six `3031` before `0xc4` because the .38's shot is
  one set (`ranged_sustained_fire`'s trace). A burst weapon counts faster; the record's cast keeps
  the .38.
- **Lane 3 moves slot 513 for every armed NPC** (`CapabilitiesGet` gains the weapon's bits) and
  opens the aim-activity gates (`ElysiumNpcAnim10.cpp:814`, `ElysiumNpcHuman.cpp:307`): an old bug
  in landed work, fixed to retail; the full arena is its proof. If a moved verdict cannot be read
  as retail inside the build cap, lane 3's one body is reverted, the red placed, and lanes 1 + 2
  stand (they do not read the seam).
- **The stream moves**: the re-roll draws from `NpcSchedule` at each fake reload.
- **One body this plan could not read** (the only one): the bytes `0x1026a233..0x1026a29f`,
  which the port's cache cites twice (§2, last row) and the corpus does not index. No V5b
  deliverable depends on it (the mask is the three listed statements). **For the judge / a
  reader with the pinned DLL's bytes** (the S4 "(B)" method): what holds `0x1026a267` and
  `0x1026a274`, and whether the port's two lines belong in the cache.
- Query budget: no query of this plan passed 2 s (the longest: `research where`, four addresses).

## 8. Size

**S–M.** Three coders (S, S, XS), one integrator, one build (cap two), two new records, one
record correction. No pipeline, no bake, no re-import.
