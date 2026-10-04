# V11 — the attack coordinator's list (N3)

Planner's design, 2026-10-04, `spec-0002/step-2`. **Not started.** Pulled forward from R4 (a
planning bug, N3 in `stories/v1/triage.md`): no NPC ever enters melee, so `chase_melee` and
`melee_swing` cannot go green. One coder; it can share a wave with V4b's lanes B1 / B2
(`../v4/brief-B1-body-speed.md`, `../v4/brief-B2-move-yaw-facing.md`) — files disjoint, §4.
Squads and followers stay in R4.

Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise. Line numbers
move: **re-locate every site by Grep on the function name.** Provenance: *(read)* listing or port
body read this session; *(doc)* `docs/vtmb/`, cited; *(triage)* not re-verified; *(I)* inferred.

| brief | who |
|---|---|
| `brief-V11-1.md` | the one coder |
| `brief-V11-integrator-notes.md` | additions to `../v4/brief-B-integrator.md` when V11 shares V4b's wave |

## 1. What retail does

**The object** *(doc: `docs/vtmb/npc-ai/social.md` § "The attack coordinator object —
`0x1025d880` … `0x1025df40`", :838-897; the planner did not re-read the six bodies)*. Three plain
heap objects of `0x28` bytes — no vtable, not entities, never saved — built by `0x1025d880` as the
last statement of the `CWorld` constructor and freed by `0x1025d940` as the first of its
destructor: `DAT_1090fbec` "Normal", `DAT_1090fbf0` "Player", `DAT_1090fbf4` "Boss", **cap 2
each**. Layout: `+0x00` cap, `+0x04` `EHANDLE*`, `+0x08` allocated, `+0x0c` grow 8, `+0x10` count,
`+0x18` `char name[16]`. Every map starts with three empty coordinators.

| address | answer |
|---|---|
| `0x1025db50` | `count < cap`: "has room" |
| `0x1025db70` | add: already listed → 1, no insert; `count >= cap` → `0x1025dca0(npc, 1)`; else append, `count++`, 1 |
| `0x1025dca0` | add-or-evict `(npc, useDist)`: room → `0x1025db70`. Full: threshold = the candidate's `m_flEnemyDist +0x6268` when `useDist`, else 0.0; the member with the strictly greatest `m_flEnemyDist` above the running best (ties and NaN keep the earlier); none → 0, nothing changed; else release it (`0x1025ddd0`) and `0x1025db70` the candidate |
| `0x1025ddd0` | release: the first entry resolving to the NPC is overwritten by the last, `count--`; absent or null: no-op |
| `0x1025de90` | 1 when the NPC is **absent** (or null, or count 0) |
| `0x1025df40` | `(npc, enemy)`: 0 when either is null or `count < 2`; else over every other member the signed `AngleDiff 0x1013d580` between its bearing to the enemy and this NPC's (`atan2` over slot-220 positions), keeping the smallest magnitude from 360; `<= 0` → −1, else 1. Caller `StartTask 0x102a1910` |
| `0x1025e120` | the name (`this + 0x18`) |

An unresolvable handle is skipped, never purged.

**The binder**, slot 608 `0x102c48b0` *(doc, :821-836)*: walks the three globals, first name match
→ `m_pAttackCoordinator +0x65e8`, `m_sAttackCoordinatorName +0x65ec`. Its one dispatch site is
`CAI_BaseNPCTroika::Precache 0x10298ad0` with the literal `"Normal"`: **every Troika NPC binds
"Normal"**; "Player" and "Boss" are built and never bound.

**The quartet** *(doc, :778-819; slot 599 and the human selector re-read this session)*: slot 599
`0x102b5650` (entry; last term `frenzied & 0x1000 || 0x1025db70`), slot 600 `0x102b57c0` (the
attacker-side entry from slot 322 `0x102a0910`; weapon slot 360 `& 0x18000`, not in melee, then
`0x1025dca0(npc, 0)`), slot 601 `0x102b5880` (exit: the event, `m_bInMelee = 0`, with slot 308
`m_flMeleeCanEnterTimer = curtime + RandomFloat(5, 10)`, then `0x1025ddd0` — guarded on `+0x65e8`
on the Troika line, unguarded on the `CNPC_VAndreiBlood` copy `0x10385cf0`), slot 602 `0x102b5900`
(leave?). Slot 601 is the only remover: 23 dispatch sites, `Event_Killed` and `UpdateOnRemove`
among them.

**The selector the two records run**, `CNPC_VHuman::SelectScheduleMeleeCombat 0x10385e40` *(read:
decompile)*: out of melee and slot 599 refusing → `0xe7` / `0xe5` / `0xe4`; in the common tail,
`CAN_MELEE_ATTACK1 0x51` → **slot 308 true `0xdc`, false `0xdd`** (lines `0x659` / `0x65d`); the
last pair → slot 308 true `0xca`, false `0xcb`; the circle pair `0xe0` / `0xe1` behind
`0x102a11d0`. Slot 308 `HasUsableRangedWeapon 0x10336d70` *(read)* scans the inventory for a
weapon with slot 360 `& 0x6000` that has a clip or ammo. **A bat-only brawler therefore runs
`0xdd SCHED_TROIKA_MELEE_ATTACK1_NR` and `0xcb SCHED_TROIKA_MELEE_ADVANCE_NR`**
(`ai/schedules/cai_basenpctroika/sched_troika_melee_attack1_nr.sch`, `…_advance_nr.sch`), both
ending in the same `SET_SCHEDULE MELEE_ATTACK1_SWING` / path tasks as the ranged-capable pair.

**`ResolveTaskDistance`, base `0x102702d0`** *(doc: `schedule-kernel.md` § "`0x102bf6e0`", closed
2026-09-21)*: −1000000 `ACCUM` → `m_flSpecialDistanceAccum +0x5bac`; −1000002 `DIALOG` → 160.0
(`0x1047a3ac`); −1000003 `COMBATMOVE` → the melee-range ConVar `DAT_10924a1c` (0 when its slot 1
answers true, else `+0x28`; shipped 100).

**`TASK_CHOOSE_BEST_MELEE_WEAPON`**, base arm `0x10286e2a` → `ChooseBestMeleeWeapon 0x10337230`
*(read)*: `GetBestMeleeWeapon`; non-null → this slot `+0x610` `(weapon, 0)` and true; null →
false → `FAIL_NO_WEAPON_TO_CHOOSE`. `GetBestMeleeWeapon` is **not read**.

**The swing and the contact** *(doc: `../v4/packets-R2.md` item 3)*: `StartTask` arm `0x102a45c6`
calls the weapon's slot 326 `PrimaryAttack` (`CWeaponMelee 0x103eaca0` → the swing sequence); the
contact has **no anim event** — it is the NPC's own slot 312 `UpdateCharacter 0x103246d0` →
`MeleeSwingUpdate 0x10346cd0`, from the Troika think's tail `0x1029365b`, after `PostRun` and
`PerformMovement`.

## 2. What the port does instead

| # | port (today's line) | divergence |
|---|---|---|
| P1 | `ElysiumNpcTroikaHelpers.cpp:134-162`, `.inl:55-73` | the four entry seams `MeleeCoordinatorHasRoom` / `Admits599` / `Admits600` / `HoldsMe` answer false: "`+0x65e8` is an index of three globals with no object behind it" |
| P2 | `AttackCoordinatorNameOf` `:204-211` | answers the empty string, so slot 608 (`:740-781`, body retail) never binds: `AttackCoordinator` (`ElysiumNpc.h:856`) is 0 on every NPC |
| P3 | the release `0x1025ddd0` | a counter (`MeleeCoordinatorReleases`) at `ElysiumNpcTroikaHelpers.cpp:506-509` (slot 601), `ElysiumNpcHuman.cpp:554` (`FUN_10385cf0`), `ElysiumNpcTzimisceHeadClaw.cpp:146`, `ElysiumNpcTzimisceRunner.cpp` ~:111 |
| P4 | `FElysiumNpcHuman::FUN_10385cf0` `ElysiumNpcHuman.cpp:549-553` | `MeleeCanEnterTimer = now + 5.0`, the `RandomFloat(5.0, 10.0)` draw deliberately not taken ("consumer does not exist") |
| P5 | `FElysiumNpc::Slot602` `:548-570` | the Troika-only null guard applied to both lines as a **named divergence** that exists only because there is no object |
| P6 | `FElysiumNpc::Slot600` `:451-456` | reads `ActiveWeaponCapabilityWord()` (`ElysiumNpcBaseMotor.cpp:361`, a seam answering 0), so the attacker-side entry is closed; the real word is `SelectActiveWeaponWord()` (`ElysiumNpcSelect.cpp:230`) |
| P7 | `TaskTailCoordinatorCircleSide` `ElysiumNpcStartTask_2.cpp` ~:399 | seam for `0x1025df40`, answers 0 |
| P8 | `FElysiumNpc::ResolveTaskDistance` `ElysiumNpcSchedule.cpp:201-230` | the base sentinels −1000003 / −1000002 / −1000000 fire a stub and return the truncated value: `DIST:COMBATMOVE` is −1e6 *(triage N3)* |
| P9 | `StartTaskChooseBestMeleeWeapon` `ElysiumNpcBaseStartTask.cpp:2921` | answers false: `task_choose_best_melee_weapon` fails `0x1f` for a bat-armed NPC *(triage N3)* |
| P10 | `ScheduleMeleeReachGate` `ElysiumNpcSchedule.cpp:583` | seam for `0x102a11d0` answering false: `0xe0` / `0xe1` never selected. Read this session: a hull trace (mask `0x2000000`, mins/maxs doubled in X/Y, Z ±6) from this NPC toward the enemy's slot 192 point, true when it hits an entity whose slot `+0x650` relation is not 1. Not ported here (§7) |
| P11 | `Anim10_2HasRangedWeapon` (`ElysiumNpcAnim10_2Shared.h:66`) stands for slot 308 by the **active** weapon's family; the generated slots 307 / 308 (`ElysiumCombatCharacterSlots.cpp:434-448`) answer false | equal for a one-weapon NPC; a divergence for an NPC holding a bat with a pistol in its inventory. Slot 599 itself calls the stub (`HasUsableRangedWeapon()`), the human selector the stand-in |

Already retail: the quartet's bodies (`Slot599` … `Slot602`, `ElysiumNpcTroikaHelpers.cpp`), slot
608, the human selector (`FElysiumNpcHuman::SelectScheduleMeleeCombatHuman`,
`ElysiumNpcHuman.cpp:323`, arm for arm against `0x10385e40`), the Precache dispatch
(`ElysiumNpcPrecache10.cpp:160`), the restore's gated slot 601 (`ElysiumNpcSaveRestore10.cpp:104`).

## 3. The seam

None needed as a separate commit: `AttackCoordinator` stays the `int32` index 1..3 (0 = retail's
null) and now names one of three real objects owned by the entity world, so `ElysiumNpc.h`, the
shape map and the save walk are untouched (retail does not save the pointer; `+0x65ec`, the name,
is saved and has no reader).

## 4. The lane (one coder)

`ElysiumAttackCoordinator.h`, `ElysiumAttackCoordinator.cpp` (new); `ElysiumEntityWorld.h`,
`ElysiumEntityWorld.cpp` (the three objects' owner and their reset only);
`ElysiumNpcTroikaHelpers.cpp`, `ElysiumNpcTroikaHelpers.inl`; `ElysiumNpcHuman.cpp`
(`FUN_10385cf0` only); `ElysiumNpcTzimisceHeadClaw.cpp`, `ElysiumNpcTzimisceRunner.cpp` (the
release sites only); `ElysiumNpcStartTask_2.cpp` (`TaskTailCoordinatorCircleSide` only);
`ElysiumNpcSchedule.cpp` (`ResolveTaskDistance` only); `ElysiumNpcBaseStartTask.cpp`
(`StartTaskChooseBestMeleeWeapon` only); `Tests/ElysiumAttackCoordinatorTests.cpp` (new),
`Tests/ElysiumNpcKernelTroikaHelpersTests.cpp`, `Tests/ElysiumNpcKernelSpeciesTests.cpp`,
`Tests/ElysiumNpcKernelScheduleTests.cpp`.

**Disjointness, by listing.** B1: `Visual/ElysiumNpcBody.cpp`, `Visual/ElysiumAnimationDriver.cpp`,
`Visual/ElysiumLocomotionSample.cpp`, "the embodiment interface header" (unnamed in its brief), its
gait tests. B2: `ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`,
`ElysiumNpcBaseMotor.cpp`, `ElysiumNpcThink.cpp`, `Tests/ElysiumNpcKernelFacingTests.cpp`,
`Tests/ElysiumNpcKernelMotorTests.cpp`. No V11 file is in either list. Two cautions: B1's unnamed
header must not be `ElysiumEntityWorld.h` (the coordinator must confirm when B1 names it); and V11
does not touch `ElysiumNpcBaseMotor.cpp` (P6 is fixed inside `Slot600`, not in the seam).
V11 shares `ElysiumNpcSchedule.cpp` with V5a-2 and `ElysiumEntityWorld.cpp` with V4a's A1:
**V11 must not share a wave with V5a or V4a**; after them it is clean.

## 5. Dependencies

- **On V4a: none.** The coordinator, the quartet and the selector read no clock word and no anim
  event; `TASK_MELEE_ATTACK1` completes on today's `IsActivityFinished`. (After A1 it completes
  one look-ahead earlier — a timing, not a verdict.)
- **On V5a: order only** (shared file, above). `CAN_MELEE_ATTACK1` comes from
  `GatherAttackConditions`' melee band, a stand-in before and after V5a-1 (`v5a/README.md` §7).
- **The melee contact.** Retail runs it in the NPC's slot 312 from the think's tail; the port
  sweeps from the world interaction tick (`AdvanceMeleeSwings`,
  `ElysiumEntityWorldInteraction.cpp:299-326`). **V11 fixes none of it**: V11 makes the NPC reach
  and start the swing. Where the sweep runs, the `ContactEventCycle` estimate and
  `Weapon_FrameUpdate` stay with V4c lane C1. `melee_swing`'s damage probe (`health < 100`) is met
  today only if the world-tick sweep lands the hit; if it does not, that residue is C1's.

## 6. Records and tests

| record | today | after V11 |
|---|---|---|
| `chase_melee` | 0.500 `WAIT_FOR_MELEE_ADVANCE 0xe7` | green, once the record errors below are corrected |
| `melee_swing` | 0.500 `WAIT_FOR_MELEE 0xe4` at 47 units | green if the sweep lands the hit; else red on the hit alone → V4c C1 |

**Record errors** (bug protocol step 1; the integrator corrects them with the retail source, no
expectation loosened): (a) both records expect `SCHED_TROIKA_MELEE_ATTACK1 (` / `MELEE_ADVANCE (`
and their `about` says "`0xdd` only with a ranged weapon": the listing is the reverse
(`0x10385e40`, lines `0x659` / `0x65d`, `0x6ac` / `0x6b0`), so a bat-only NPC shows `…_ATTACK1_NR
(0xdd)` and `…_ADVANCE_NR (0xcb)`; the matches take the `_NR` forms. (b) `melee_swing`'s
`hit_event` (`animevent`) is not retail — no attack clip of the bat bank authors an event (R2 item
(c)); it is dropped and the `about` corrected. (c) both `about` texts cite slot 555 `0x1026d9a0`
for the melee band; a weapon-armed NPC's is the weapon's slot 367 (`0x103eac30` → `0x103ea7e0`).

Arm tests added: `Elysium.Arm.AttackCoordinator.Admit` (`0x1025db70`: append under the cap,
listed → 1 with no insert, full → the evict path with `useDist` 1), `.Evict` (`0x1025dca0`:
strictly greatest `m_flEnemyDist`, the threshold with and without `useDist`, ties keep the
earlier, none → 0 unchanged), `.Release` (`0x1025ddd0`: last-over-first, absent no-op), `.Query`
(`0x1025db50`, `0x1025de90`), `.CircleSide` (`0x1025df40`), `.Lifetime` (`0x1025d880` /
`0x1025d940`: three objects, cap 2, names, empty after a world reset);
`Elysium.Arm.NpcKernelTroikaHelpers.BindNormal` (`0x102c48b0` binds "Normal");
`Elysium.Arm.NpcKernelSchedule.ResolveTaskDistanceBase` (`0x102702d0`'s three sentinels).

Deleted or rewritten (they pin the missing object): `ElysiumNpcKernelTroikaHelpersTests.cpp`
~:224-289 and ~:569-573 (the null-coordinator divergence, "leaves `m_pAttackCoordinator` alone");
`ElysiumNpcKernelSpeciesTests.cpp` ~:484, ~:556-563, ~:1790-1887 (seams answering false, the
counter); `ElysiumNpcKernelScheduleTests.cpp` ~:592-594's premise. The dispatch-table rows
(`MeleeSlotBody`) stay.

## 7. Risks, unrecovered, for the judge

- **Cap 2 changes group fights everywhere**: a third brawler waits (`0xe4` / `0xe5` / `0xe7`)
  and the farthest member is evicted by a nearer candidate. That is retail; any arena or map record
  with three melee NPCs moves.
- **`0x102a11d0`** (P10) stays a seam: no step-2 record needs the circle pair. R3 / R4.
- **`GetBestMeleeWeapon`** is unread: the coder reads it first; if it needs an inventory scan the
  port lacks, P9 stays a named seam and is reported (it is behind `0xe3 SWITCH_TO_MELEE`, not on
  either record's path once slot 599 admits).
- **Slots 307 / 308 as inventory scans** (P11) are not in V11's files (`ElysiumCombatCharacterSlots.cpp`
  is A3's): recorded, proposed for V5 proper or R4.
- **The six coordinator bodies were not re-read** by the planner: the coder reads each listing
  before porting it (≈1 KB total).
- **`Event_Killed` / `UpdateOnRemove`'s slot 601**: if the port does not dispatch it there, a dead
  brawler holds a slot until the map ends. The coder checks by Grep and reports the exact line; it
  is a fix in landed work, taken by the integrator if it is one line.
- **The melee band `0x103ea7e0`**: with the judge (`v5a/README.md` §7).
- A save restores `AttackCoordinator` only through Precache's re-bind; the lists start empty, as
  retail's (the objects are never saved).
