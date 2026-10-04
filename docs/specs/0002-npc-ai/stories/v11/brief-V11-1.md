# Brief V11-1 — the attack coordinator's list and what stands behind it (coder; no build)

Read `README.md` here (all of it), `CLAUDE.md`, `spec.md` § Standing rules, `../v4/README.md`
§ "Rules for every agent of V4" (they apply to you), `docs/vtmb/npc-ai/social.md` § "The melee
coordinator" (:776-897: `uv run elysium research section 0x1025db70`), triage row N3 (Grep
`| N3 |` in `stories/v1/triage.md`; never the file whole), the records
`Arena/scenarios/combat/chase_melee.json` and `melee_swing.json`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumAttackCoordinator.h`, `.cpp` (new)
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorld.h`, `.cpp` (the three objects' owner and their reset, nothing else)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers.cpp`, `.inl`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcHuman.cpp` (`FUN_10385cf0` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTzimisceHeadClaw.cpp`, `ElysiumNpcTzimisceRunner.cpp` (the `0x1025ddd0` release sites only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask_2.cpp` (`TaskTailCoordinatorCircleSide` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSchedule.cpp` (`ResolveTaskDistance` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp` (`StartTaskChooseBestMeleeWeapon` only)
- `Source/ElysiumUE/Private/Tests/ElysiumAttackCoordinatorTests.cpp` (new), `Tests/ElysiumNpcKernelTroikaHelpersTests.cpp`, `Tests/ElysiumNpcKernelSpeciesTests.cpp`, `Tests/ElysiumNpcKernelScheduleTests.cpp`

## The job

1. **Read the listings first** (`vtmb_code`, one call each, ≈1 KB in all): `1025db50`,
   `1025db70`, `1025dca0`, `1025ddd0`, `1025de90`, `1025df40`, `1025d9d0`. Where a body disagrees
   with `social.md`'s table, the listing wins and the doc is corrected in your report (exact line).
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
11. **`StartTaskChooseBestMeleeWeapon`** (`0x10337230`): read `GetBestMeleeWeapon` (its callee,
    `vtmb_callees 10337230`) first. If the port's inventory can answer it, port both and the
    switch (this slot `+0x610` `(weapon, 0)`); if not, leave the seam, name both addresses and what
    is missing, and report.
12. **Check, do not edit** (Grep): does the port's `Event_Killed` / `UpdateOnRemove` path dispatch
    slot 601 (`social.md` :883-888)? Report the site or its absence, with the exact line to add.
13. **Tests** (README §6): `Elysium.Arm.AttackCoordinator.*` on the object alone, plus the two
    kernel tests; rewrite or delete the assertions that pin the missing object and list them.

## Not yours

The quartet's logic beyond the lines above (already retail), the human selector
(`SelectScheduleMeleeCombatHuman`), `GatherAttackConditions` and the melee band (V5a-1; the
judge), `ScheduleMeleeReachGate 0x102a11d0` (R3 / R4), slots 307 / 308
(`ElysiumCombatCharacterSlots.cpp`), the swing start and the contact sweep
(`ElysiumNpcStartTask.cpp`, `ElysiumWeaponClasses.*`, `ElysiumEntityWorldInteraction.cpp`: V4c
C1), `ElysiumNpc.h`, the shape map, every file of B1 and B2 (`Visual/*`, `ElysiumNpcMotor10.*`,
`ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`, `ElysiumNpcThink.cpp`), squads and
followers (R4), any record under `Arena/`.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line you port. A divergence is recorded in your report, not
adopted. Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops
— never retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read /
Glob, never shell `grep` / `cat` / `sed`. `research where <addr>` before searching `docs/`. Do not
commit. Report ≤300 words: what you ported (addresses), where the listing corrected the doc,
tests added and deleted, cross-lane lines, what stays unrecovered.
