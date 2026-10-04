# Brief V11-1 — the attack coordinator's list and what stands behind it (coder; no build)

Read `README.md` here (all of it), `CLAUDE.md`, `spec.md` § Standing rules, `../v4/README.md`
§ "Rules for every agent of V4" (they apply to you), `docs/vtmb/npc-ai/social.md` § "The melee
coordinator" (:776-897: `uv run elysium research section 0x1025db70`), triage row N3 (Grep
`| N3 |` in `stories/v1/triage.md`; never the file whole), the records
`Arena/scenarios/combat/chase_melee.json` and `melee_swing.json`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

**Amended after the settling packets S2–S4 and the judge's second sitting, 2026-10-04**
(`../v4/packets-S2.md` items 4–6, `../v4/packets-S3.md` items 7–8, `../v4/packets-S4.md` items b
and f; `stories/v1/triage.md` § "Judge's rulings, V4 — second sitting" J10, J14.5, J14.7 — Grep,
read only that section). What changed for you: the six coordinator bodies are re-read and agree
with `social.md`'s table (item 1 is now a check); **`0x102a11d0` is yours** (item 14, J10);
`GetBestMeleeWeapon` is read (item 11); slot 331's doc section is read before you trust `0x51`
(item 15). Where this brief and the README's older text disagree, this brief wins.

**Amended after S5 (planner, 2026-10-04): the wave is [V11-1, V11-2, V11-3], after V4b — B1 and
B2 are no longer in it.** V11-2 (`brief-V11-2-melee-contact.md`) holds
`ElysiumWeaponClasses.{h,cpp}`, `ElysiumSwingContact.{h,cpp}`, `Public/ElysiumWorldServices.h`,
`Public/ElysiumMapActor.h`, `Map/ElysiumMapActor.cpp`, `Tests/ElysiumTestServices.h` and its
tests; V11-3 (`brief-V11-3-slot-331.md`) holds new `ElysiumMeleeSequenceChoice.{h,cpp}`,
`ElysiumCombatCharacterSlots.cpp` (slot 331) and its test. None is yours. The rule of the wave
check below is unchanged with V11-2 in B1's place: you **call** `TraceRetail`, you edit neither
that header nor `Tests/ElysiumTestServices.h`.

**Amended after S6 (planner, 2026-10-04; `../v4/packets-S6.md` items 3 and 5).** (a) **Never wait
on `0x63` for melee**: `WEAPON_BLOCKED_BY_FRIEND` is raised only through slot 562 `0x1026fbe0`,
asked only on a ranged `0x4f`; `melee_ally_in_the_way` is decided by your gate `0x102a11d0` (item
14) and slot 331's `0x3a` (V11-3), and no test or expectation of yours names `0x63`. (Slot 562 is
lane V5a-3, in V4b's wave, already landed when you run.) (b) **The melee activity is translated
before anyone uses it** (`0x103ea810..0x103ea82d`): `weapon.ActivityOverride 0x1024f210(0x4b)`
(slot 361: the class's ladder, first playable row — a bat gives `ACT_MELEE_ATTACK_BASEBALLBAT`, a
species weapon with no row keeps `ACT_MELEE_ATTACK`), then owner slot 376 (the identity here on
the whole Troika line: no seam). Where a line of yours names the swing's activity, it is that
translated one, through the existing ladder resolver (`Visual/ElysiumWeaponActivityTables.cpp`,
read and called, not edited); the band is not yours — if you see it collecting sequences under the
untranslated `0x4b`, report the line.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumAttackCoordinator.h`, `.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.h`, `.cpp` (the three objects' owner and their reset, nothing else)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers.cpp`, `.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcHuman.cpp` (`FUN_10385cf0`, and — item 14 — the gate's call site's name ~:480; these only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSchedule.inl` (item 14: the gate's declaration only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcMingXiao.cpp` (item 14: the gate's call site's name ~:366 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTzimisceHeadClaw.cpp`, `ElysiumNpcTzimisceRunner.cpp` (the `0x1025ddd0` release sites only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask_2.cpp` (`TaskTailCoordinatorCircleSide` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSchedule.cpp` (`ResolveTaskDistance`, and — item 14 — `ScheduleMeleeReachGate` ~:583 with its call site ~:388; these only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp` (`StartTaskChooseBestMeleeWeapon` only)
- `Source/ElysiumUE/Private/Tests/ElysiumAttackCoordinatorTests.cpp` (new), `Tests/ElysiumNpcKernelTroikaHelpersTests.cpp`, `Tests/ElysiumNpcKernelSpeciesTests.cpp`, `Tests/ElysiumNpcKernelScheduleTests.cpp`

Wave check ([B1, B2, V11-1], re-checked after the second sitting): none of these is B1's
(`Visual/ElysiumNpcBody.{h,cpp}`, `Visual/ElysiumNpcMoveScript.{h,cpp}`,
`Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h`, `Map/ElysiumMapActorEmbodiment.cpp`,
`Tests/ElysiumTestServices.h`, `Tests/ElysiumNpcMoveScriptTests.cpp`) or B2's
(`ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`,
`ElysiumNpcThink.cpp`, `Tests/ElysiumNpcKernelFacingTests.cpp`,
`Tests/ElysiumNpcKernelMotorTests.cpp`). **By ownership of functions**: item 14 **calls**
`IElysiumEmbodiment::TraceRetail`, declared in B1's `Public/ElysiumWorldServices.h` — you do not
edit that header, nor `Tests/ElysiumTestServices.h` (a line your test double needs there goes in
your report, exact, for the integrator). And if reader R1b names `ElysiumNpcBaseStartTask.cpp` or
`ElysiumNpcStartTask_2.cpp` for B2's slow turn, the file stays yours: B2 reports its line.

## The job

1. **The six coordinator bodies are re-read** (S3 item 7) and agree with `social.md`'s table and
   README §1; open a listing (`vtmb_code`: `1025db50`, `1025db70`, `1025dca0`, `1025ddd0`,
   `1025de90`, `1025df40`, `1025d9d0`) only where a line you port needs a detail the table does
   not carry. Where a body disagrees with the table, the listing wins and the doc is corrected
   in your report (exact line). As read: `0x1025db50` `count < cap`; `0x1025db70` listed → 1,
   room → append, full → `0x1025dca0(npc, 1)`, a NULL NPC recurses; `0x1025dca0` room →
   `0x1025db70`, threshold the candidate's `+0x6268` or 0.0, the strictly greatest member, none →
   0, else release and add; `0x1025ddd0` the first match overwritten by the last, `count--`;
   `0x1025de90` 1 when absent; `0x1025df40` `AngleDiff(this NPC's bearing, the member's)`,
   bearings by `atan2` from the enemy to each, best magnitude from 360.0, `<= 0` → −1, a stale
   member dereferenced. Cap 2, three objects (`0x1025d880`).
   **The arena brawler runs the other line**: `npc_VHumanCombatant`'s slots 599..602 are
   `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70` — its 599 calls `0x1025db70` twice, its
   601 releases unguarded, its 602 drops the null-coordinator test. Check the port's
   `FElysiumNpcHuman` slot bodies against those four addresses, not against `0x102b5650..5900`;
   a difference outside your files is reported with its line.
2. **`FElysiumAttackCoordinator`** (plain C++, no reflection, UE containers): cap (2,
   `0x1025d9d0`), the handle list (`FElysiumEntityHandle`), the name. Methods, each citing its
   address: `HasRoom` `0x1025db50`; `Add(npc)` `0x1025db70`; `AddOrEvict(npc, useDist)`
   `0x1025dca0` (strictly greatest `m_flEnemyDist +0x6268` = `ScheduleHost.EnemyDistUnits`; ties and
   NaN keep the earlier; none → false, unchanged); `Release(npc)` `0x1025ddd0` (last over first,
   order not preserved); `IsAbsent(npc)` `0x1025de90`; `CircleSide(npc, enemy)` `0x1025df40`;
   `Name` `0x1025e120`. An unresolvable handle is skipped, never purged. Two crash arms retail has
   (a null NPC recursing in `0x1025db70`; a stale member dereferenced in `0x1025df40`) are not
   reproduced: answer false / skip the member, and say so at the line. `AddOrEvict` takes its flag
   as a two-value enum, not a bool (`.claude/rules/cpp.md`).
3. **The owner**: three coordinators "Normal", "Player", "Boss" on `FElysiumEntityWorld`, built
   with the world and emptied where the port tears a map's entity world down (`0x1025d880` last in
   `CWorld`'s constructor, `0x1025d940` first in its destructor). An accessor by index 1..3 (0 is
   retail's null). Forward-declare in the header; keep the edit to the member, the accessor and
   the reset.
4. **The seams become calls** (`ElysiumNpcTroikaHelpers.cpp`): `MeleeCoordinatorHasRoom`,
   `MeleeCoordinatorAdmits599` (`Add`), `MeleeCoordinatorAdmits600` (`AddOrEvict`, no distance),
   `MeleeCoordinatorHoldsMe` (`!IsAbsent` — keep slot 602's negation reading `0x1025de90`),
   `AttackCoordinatorNameOf` (the object's name, so slot 608 binds "Normal" at Precache). The first
   three lose `const` where they mutate. With `AttackCoordinator == 0` or no world they answer
   what they answer today, named as retail's fault path.
5. **A release helper** for `0x1025ddd0` beside them, called from the four counted sites: slot 601
   (guarded, `0x102b5880`), `FElysiumNpcHuman::FUN_10385cf0` (unguarded in retail: with index 0 it
   is a no-op here, said at the line), the HeadClaw's and the Runner's. Keep the counters only if
   a retained test reads them.
6. **`FUN_10385cf0`'s draw** (`0x10385cf0`): `m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0,
   10.0)` on the `NpcSchedule` stream, as `Slot601` already does; delete the comment that excuses
   the missing draw.
7. **`Slot602`**: the object exists, so delete the "NAMED DIVERGENCE" paragraph; the Troika line
   keeps its own third test (`m_pAttackCoordinator == 0` → false). Whether the human line
   (`0x10385d70`) must now drop it is a read of how `FElysiumNpcHuman::Slot602` calls this body:
   if dropping it needs a file outside yours, report the exact line.
8. **`Slot600`** (`0x102b57c0`): the capability word is the weapon's slot 360; read it through
   `SelectActiveWeaponWord()` (already the real word), not the `ActiveWeaponCapabilityWord()` seam.
   Do not edit the seam (`ElysiumNpcBaseMotor.cpp` is another lane's).
9. **`TaskTailCoordinatorCircleSide`** → `CircleSide` (`0x1025df40`); 0 keeps the random side.
10. **`ResolveTaskDistance`'s base arms** (`0x102702d0`, `schedule-kernel.md` § "`0x102bf6e0`"):
    −1000000 → `SpecialDistanceAccum (+0x5bac)`; −1000002 → 160.0 (`0x1047a3ac`); −1000003 →
    `MeleeRangeUnits()` (`DAT_10924a1c`); delete the stub line.
11. **`StartTaskChooseBestMeleeWeapon`** (`0x10337230`) — the bodies are read (S2 item 5, S4
    f.1). `GetBestMeleeWeapon 0x10336f20`: walk the type list `0x10619eb4` = `{1, 2, 3, 7, −1}`;
    for a type whose `0x10619d28[type] >= 0`: `first = 0x10937cd0[type]`, `count = owner slot 298
    (+0x4a8)(0x10619d28[type])`; the first `GetWeapon(i)` in `[first, first + count)` whose slot
    360 (`+0x5a0`) `& 0x18000` is returned; none → 0. `ChooseBestMeleeWeapon`: a weapon → owner
    slot 388 (`+0x610`) `Weapon_Switch(weapon, 0)`, true; else false → `FAIL_NO_WEAPON_TO_CHOOSE`.
    The two tables are **the inventory sections**, filled by `CacheInventorySections 0x10340180`
    over the names `None, Weapon_Melee, Weapon_Ranged, Weapon_Thrown, Armor, Generic, Powerups,
    Hidden`: types 1, 2, 3, 7 are the **Melee, Ranged, Thrown and Hidden sections, 32 slots
    each**, walked in that order. **Port it if the port's inventory has sections in an order**
    (Grep the inventory for a section index or the section names); the walk is then over the
    port's sections in retail's type order. **The section order is settled (`../v4/packets-S5.md` item 6):
    `vdata\system\items.txt` § `InventorySections`, in file order — None, Weapon_Melee,
    Weapon_Ranged, Weapon_Thrown, Armor, Generic, Powerups, Hidden — so the walk is weapon slots
    `[0, n)` Melee, `[32, …)` Ranged, `[64, …)` Thrown, `[192, …)` Hidden, each `n` from owner
    slot 298 of section number 0, 1, 2, 6.** If the port's inventory has no section order,
    **leave the seam**, name `0x10336f20`, `0x10340180` and what is missing at the line, and report which
    (J14.5). It is behind `0xe3 SWITCH_TO_MELEE`, not on either record's path once slot 599
    admits.
12. **Check, do not edit** (Grep): does the port's `Event_Killed` / `UpdateOnRemove` path dispatch
    slot 601 (`social.md` :883-888)? Retail: `Event_Killed` dispatches it in three bodies
    (`0x102bf340`, `0x10385a90`, `0x103c4f40`), removal in `UpdateOnRemove 0x1028d6e0`; **a
    schedule change frees nothing** (`OnScheduleChange 0x102a0940` holds no `CALL [reg+0x964]`).
    Report the site or its absence, with the exact line to add; and report any port line that
    releases the slot on a schedule change (it is not retail's).
13. **Tests** (README §6): `Elysium.Arm.AttackCoordinator.*` on the object alone, plus the kernel
    tests; rewrite or delete the assertions that pin the missing object and list them.
14. **`0x102a11d0` — "a non-hated NPC stands in the way" (J10: yours, implemented now).** Today
    `ScheduleMeleeReachGate` (`ElysiumNpcSchedule.cpp` ~:583) is a seam returning `false`, so
    `0xe0` / `0xe1` are never selected. Retail (S2 item 6, listing): `(this NPC; const Vector&
    point)`, called by the four `SelectScheduleMeleeCombat` bodies (`CNPC_VHuman 0x10385e40`,
    `CNPC_VChangBros 0x1036d800`, `CNPC_VMingXiao 0x10396050`, `CNPC_VTzimisceRunner
    0x103c4430`). Hull `mins = (2·mins.x, 2·mins.y, −6)`, `maxs = (2·maxs.x, 2·maxs.y, +6)` of
    this NPC's collision box; a swept ray from `WorldSpaceCenter` (slot 192) to the point
    (`Ray.Init(start, end, mins, maxs, 1, 0)`), filter `(this, group 0)`, mask `0x2000000`
    (`CONTENTS_MONSTER`). **True** when the trace was blocked (`fraction < 1` or `allsolid` or
    `startsolid`), the entity hit has an NPC pointer (`+0x94`), and `IRelationType(hit)` (slot
    404, `+0x650`) `!= 1` (`D_HT`). Else false — the player's body ends the trace and answers
    false (no NPC pointer).
    Port it over **`IElysiumEmbodiment::TraceRetail`** (`Public/ElysiumWorldServices.h` ~:703-759:
    a box, a retail mask, an ignore list; it returns the character bodies met, nearest first,
    when the mask carries `MONSTER`) with the MONSTER-alone recipe that already exists
    (`ElysiumRetailMaskRecipe.cpp` ~:33-38, used by the idle gate `0x102b5de0`) — you call both,
    you edit neither. Units: state the Source-unit → cm conversion at the line. **Rename it**
    (it is not a reach test): e.g. `ScheduleNonHatedNpcInTheWay`, the declaration in
    `ElysiumNpcSchedule.inl`, the three call sites (`ElysiumNpcHuman.cpp` ~:480,
    `ElysiumNpcMingXiao.cpp` ~:366, `ElysiumNpcSchedule.cpp` ~:388) — the name only, at those
    three lines; a fourth caller found by Grep outside your files is reported with its line.
    With no world or no embodiment it answers false, named as the fault path.
    Arm test `Elysium.Arm.NpcKernelSchedule.AllyInTheWay` on the trace double (the hull, the
    mask, self ignored; a non-hated NPC nearest → true; a hated NPC, the player's body, nothing →
    false). The record `melee_ally_in_the_way` is not yours to write (A0 writes it red; the
    integrator turns it).
15. **Before you trust `0x51`** (J14.7): the in-reach arm of the melee band needs owner slot 331
    = `CBaseCombatCharacter::ChooseMeleeAttackSequence 0x10347180` to answer true with `out >=
    0`. **Settled (`../v4/packets-S5.md` item 4): the body is walked, the port's slot is a
    stub answering false, and lane V11-3 ports it in this wave** (`brief-V11-3-slot-331.md`).
    Nothing to read and nothing to report for it; you edit nothing for it. V11-3 will report
    one line for your `ElysiumNpcMingXiao.cpp` (`ChooseMeleeAttackSequenceSeam` ~:924): the
    integrator applies it, not you.

## Not yours

The quartet's logic beyond the lines above (already retail), the human selector
(`SelectScheduleMeleeCombatHuman`, beyond the one renamed call), `GatherAttackConditions` and the
melee band `0x103ea7e0` (**V5a-1 ports it**, J14.4; it is read whole — `../v4/packets-S2.md` item
4, constants in `../v4/packets-S4.md` item b — reads only sequence data the bake carries, and is
no longer a judge item: out of reach beyond `max(1.2 × reach, 256)` the word is `9`, between
reach and that `0x60`), slots 307 / 308
(`ElysiumCombatCharacterSlots.cpp`), slot 331 (V11-3), the melee contact's hit test, filters
and damage (`ElysiumWeaponClasses.*`, the swing query: V11-2), the swing start and where the
sweep runs (`ElysiumNpcStartTask.cpp`, `ElysiumEntityWorldInteraction.cpp`: V4c
C1), `ElysiumNpc.h`, the shape map, every file of B1 and B2 (`Visual/*`,
`Public/ElysiumWorldServices.h`, the map actor's embodiment files, `Tests/ElysiumTestServices.h`,
`ElysiumNpcMotor10.*`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`,
`ElysiumNpcThink.cpp`), the other species' selectors that call the gate
(`CNPC_VChangBros`, `CNPC_VTzimisceRunner`: report a call site that needs the new name), squads
and followers (R4), any record under `Arena/`.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line you port. A divergence is recorded in your report, not
adopted. Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops
— never retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read /
Glob, never shell `grep` / `cat` / `sed`. `research where <addr>` before searching `docs/`. Do not
commit. Report ≤300 words: what you ported (addresses), where the listing corrected the doc,
the gate's new name and its call sites, whether item 11 was ported or stays a seam and why, what
slot 331's doc says for `0x51`, tests added and deleted, cross-lane lines, what stays unrecovered.
