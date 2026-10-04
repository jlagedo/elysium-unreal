# Brief V5a-1 — `GatherAttackConditions 0x1026dd10`, whole (coder; no build)

Read `README.md` here (§1 first block, §2 P1–P3, §6, §7), `CLAUDE.md`, `spec.md` § Standing rules,
`../v4/README.md` § "Rules for every agent of V4" (they apply to you), the record
`Arena/scenarios/combat/range_bands.json` (`about`, `known_red`). The listing is the authority:
`vtmb_asm 1026dd10` from `0x1026de02` to `0x1026e10c` (one page), `vtmb_code 1026dd10`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

**Amended after settling packets S2 and S4 and the judge's second sitting, 2026-10-04** (J14.4,
`stories/v1/triage.md` § "Judge's rulings, V4 — second sitting"; `../v4/packets-S2.md` item 4,
`../v4/packets-S4.md` item b): **the melee band `0x103ea7e0` is read whole, with its five
constants, and is yours to port** — item 4 below replaces "unread; do not invent its bands".

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
   early `return` that makes the two exclusive.
   **The melee weapon's band — settle which port body stands for it, and port it whole if none
   does** (J14.4). No line under `Private/Substrate` cites `0x103ea7e0`, `0x103eac30` or
   `0x103eac60`; today's band in this body (~:1098-1114) is "CHOSEN, NOT RECOVERED" (0x60 past
   `MeleeReachSourceUnits`, 0x51 when facing and ready). First say in your report which port
   body the weapon's slot 367 reaches (Grep `MeleeAttack1Conditions`, `MeleeReach`, the weapon's
   condition readers); then replace the stand-in with retail's body, **as read**:
   `0x103ea7e0(weapon; activity, target, dot, dist)`, callers `0x103eac30` (activity `0x4b`, the
   weapon's slot 367 — this arm) and `0x103eac60` (`0x4e`).
   - `ready` = `+0x730 < curtime && +0x734 < curtime && owner m_flNextAttack (+0x1564) <
     curtime`, then (the target is a combat character) the target's slot 327.
   - `dot > 0.7` (f64 `0x104492d0`) `&& target CC && ready` and owner slot 331 `(weapon, target,
     activity, &out)` true with `out >= 0` → **`0x51`**. Slot 331 is
     `CBaseCombatCharacter::ChooseMeleeAttackSequence 0x10347180` (3,160 bytes, not walked by the
     packets): call the port's existing body for it (`research where 0x10347180`); if the port
     has none, a seam named for the address answering what today's stand-in answers, reported —
     **unrecovered as a listing walk; a reader settles it before this lane starts** if the port
     has no body.
   - Then over `GetSequencesForActivity(owner, translated activity, …)`, each sequence counted
     when `(target CC || seqdesc+0x10 > 0) && seqdesc+0x2c4 > 0`: `lo = min(+0x2cc)` (seed
     100000.0), `hi = max(+0x2d0)` (seed −100000.0), `mean` = the average over the `+0x2bc`
     records at `+0x2c0` (24-byte stride) of `(rec[0] + rec[3]) × 0.5` (f32 `0x104454d0`). No
     record counted → answer 0. `mean` clamped into `[lo, hi]`.
   - Then, in this order: `dist > max(hi × 1.2, 256.0)` (f32 `0x1049ae90`, `0x1044ddb0`) →
     **`9`**; `dist > hi` → **`0x60`**; `dot < 0.7` → **`0x61`**; `dist < lo` → **`0x5f`**;
     target CC and `ready`: `dist < mean × 0.25` (f64 `0x10449260`) → `0x5f`, else `0x60`; else
     0.
   **Every input is the model's sequence descriptor and the bake carries all of them** — no
   pipeline change: `+0x10` → the clip's `weight`, `+0x2c4` → the `swings` count, `+0x2cc` →
   `LowReachCm`, `+0x2d0` → `ReachCm`, `+0x2bc/+0x2c0` → `Envelopes` (min corner + max corner)
   on `FElysiumNpcClip`. Units: the baked values are centimetres, `dist` and 256.0 are Source
   units — convert at the line and say which side you converted. If reading the wielder's
   sequences for an activity needs an accessor that lives outside your files (the kernel's
   sequence table: `ElysiumNpcAnim.cpp` is not yours), write its exact signature in your report
   and call it; the integrator adds it. The five constants are not in `kernel_tunables.tsv`
   (generated header, never hand-edited): write the five rows in your report for the
   integrator, and cite each cell's address at its line meanwhile.
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
   `.GatherAttackBothArms`, README §6), each assertion naming its address; and
   `.MeleeWeaponBand` (`0x103ea7e0`) on fixture clips: `9` beyond `max(1.2 × hi, 256)`, `0x60`
   between `hi` and that, `0x61` under dot 0.7, `0x5f` under `lo` and under `mean × 0.25`,
   `0x51` only with dot above 0.7, a combat-character target, `ready` and slot 331 answering; no
   counted sequence → 0. Delete an assertion that pins the stand-in's numbers; list it. In
   `ElysiumNpcCombatTests.cpp` rewrite or delete only assertions that pin the stacking or the
   exclusive split; list them.

## Not yours

`GatherEnemyConditions` and its call site (`ElysiumNpcBaseConditions2.cpp`, already retail), slot
560 / 553 bodies (`ElysiumNpcBaseConditions.cpp`), the weapon classes, `RefreshCombatConditions
0x102b2570`, slot 363 (A3), `StartTask`, `ElysiumNpcSchedule.cpp` (lane V5a-2). A line another
file needs goes in your report, exact, with its place.

Wave check ([V5a-1, V5a-2, A3], re-checked after the second sitting): the band is ported inside
`ElysiumNpcConditions.{h,cpp}`, already yours; the second sitting adds no file to this lane. None
of your files is V5a-2's (`ElysiumNpcStartTask.{cpp,inl}`, `ElysiumItemTable.{h,cpp}`,
`ElysiumNpcSchedule.cpp`, its two tests) or A3's (`ElysiumCombatCharacterSlots.cpp`,
`ElysiumCombatCharacterSlotBodies.cpp`, `ElysiumNpcSenses.{h,cpp}`, `ElysiumPlayerEntity.cpp`,
its test). `ElysiumNpcAnim.cpp`, `ElysiumWeaponClasses.*` and the tunables table are in no lane
of the wave: lines for them go to the integrator.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line. A divergence is recorded in your report, not adopted.
Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops — never
retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read / Glob,
never shell `grep` / `cat` / `sed`. Look an address up with `uv run elysium research where <addr>`
before searching `docs/`. No record under `Arena/` is yours. Do not commit. Report ≤300 words:
what you ported (addresses), tests added and deleted, cross-lane lines, what stays unrecovered.
