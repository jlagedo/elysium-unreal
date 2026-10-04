# Brief V5a-1 — `GatherAttackConditions 0x1026dd10`, whole (coder; no build)

Read `README.md` here (§1 first block, §2 P1–P3, §6, §7), `CLAUDE.md`, `spec.md` § Standing rules,
`../v4/README.md` § "Rules for every agent of V4" (they apply to you), the record
`Arena/scenarios/combat/range_bands.json` (`about`, `known_red`). The listing is the authority:
`vtmb_asm 1026dd10` from `0x1026de02` to `0x1026e10c` (one page), `vtmb_code 1026dd10`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.cpp` (`ElysiumNpcCond::GatherAttackConditions`, ~:1053-1151)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.h` (its declaration and comment, ~:669-696)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseClosure.cpp` (`FElysiumNpcBase::GatherAttackConditions`, ~:143-173)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelConditionsTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcCombatTests.cpp` (only assertions that drive `GatherAttackConditions`, ~:520-705)

## The job, in retail's order

1. **The top clear, `0x1026de02`.** The gather calls slot 560 `ClearAttackConditions()`
   (`FElysiumNpcBase::ClearAttackConditions`, already retail, virtual) before anything else is
   set. The free function takes a `const` NPC: either make the slot body
   (`ElysiumNpcBaseClosure.cpp`) call slot 560 and then the free function, or change the free
   function's signature — your choice, inside your files; say which in the report. The eleven
   cleared words are the slot's, never the band words 0x08 / 0x5f / 0x60 / 0x09.
2. **The ranged arm, `0x1026de3a..0x1026ded7`.** `caps & 0x2000` with an active weapon: weapon not
   ready (`0x10252410(weapon, 0)`: `curtime < NextPrimaryAttackTime`) → 0x2f; answer = the
   weapon's slot 365 (today's `Weapon->RangeAttack1Conditions`). Else `caps & 0x20000` (innate):
   `curtime < NextAttackTime (+0x1564)` → 0x2f; answer = slot 553 `RangeAttack1Conditions(dot,
   dist)`. The port keys on `ElysiumNpcCond::WeaponCapability`; keep that reader, but state at
   the line which retail bit each branch stands for (slot 513 `CapabilitiesGet`, `+0x804`).
3. **Answer 0x4f, `0x1026ded9..0x1026df69`.** First LOS test; on failure **slot 560 again**
   (`0x1026df00`) then the second test to the body target; either passing → 0x4f; both failing →
   nothing set. The port's occlusion-latch stand-in for slot 562 stays (named at the line): keep
   its outcome, add the second clear on the failing path. Any other answer, 0 included:
   `SetCondition(answer)`.
4. **The melee arm runs after the ranged arm, not instead of it** (`0x1026df6d`): `caps & 0x8000`
   with an active weapon → the weapon's slot 367; else `caps & 0x80000` → slot 555. Remove the
   early `return` that makes the two exclusive. The band numbers stay today's stand-in (retail's
   `CWeaponMelee 0x103eac30` → `0x103ea7e0` is unread: name both addresses at the line as
   **unrecovered**, do not invent its bands).
5. **The timers, `0x1026dfd0..0x1026e062`**, on `WeaponBlockedByFriendTimer (+0x5b88)` and
   `ExtendedBlockedByFriendTimer (+0x5b8c)`: with 0x63 standing — extended `== FLT_MAX` →
   `curtime + 2.5` (`_DAT_104629ec`, `ElysiumNpcTunables::TwoAndHalf`); `+0x5b88 = curtime + 1.5`
   (`_DAT_1044f02c`, `OneAndHalf`). Without 0x63 — `+0x5b88 <= curtime` → extended `= FLT_MAX`.
   Then `extended < curtime` → set 0x2e.
6. **The tail, `0x1026e062..0x1026e107`.** `curtime < +0x5b88`: set 0x63; clear 0x50, 0x4f, 0x52,
   0x51. Otherwise: if any of 0x50, 0x4f, 0x52, 0x51 stands, clear 0x08, 0x5f, 0x60, 0x09, 0x63.
   Cite each address at its line.
7. **The entry guards.** The port returns early on an inert enemy; retail's caller only passes a
   live `GetEnemy()`. Keep a null guard (say so at the line); check whether the inert return can
   skip the top clear on a path retail would clear, and report it.
8. **Tests** (`Elysium.Arm.NpcKernelConditions.GatherAttackClears`, `.GatherAttackFriendTimers`,
   `.GatherAttackBothArms`, README §6), each assertion naming its address. In
   `ElysiumNpcCombatTests.cpp` rewrite or delete only assertions that pin the stacking or the
   exclusive split; list them.

## Not yours

`GatherEnemyConditions` and its call site (`ElysiumNpcBaseConditions2.cpp`, already retail), slot
560 / 553 bodies (`ElysiumNpcBaseConditions.cpp`), the weapon classes, `RefreshCombatConditions
0x102b2570`, slot 363 (A3), `StartTask`, `ElysiumNpcSchedule.cpp` (lane V5a-2). A line another
file needs goes in your report, exact, with its place.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read / Glob,
never shell `grep` / `cat` / `sed`. Look an address up with `uv run elysium research where <addr>`
before searching `docs/`. No record under `Arena/` is yours. Do not commit. Report ≤300 words:
what you ported (addresses), tests added and deleted, cross-lane lines, what stays unrecovered.
