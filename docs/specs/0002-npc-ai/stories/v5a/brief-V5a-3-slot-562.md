# Brief V5a-3 — slot 562 `WeaponLOSCondition 0x1026fbe0` wired, with the weapon's line of fire `0x1024f330` → `0x1024f3d0` (coder; no build)

Planner, 2026-10-04, after `../v4/packets-S6.md` item 5. Condition `0x63`
(`WEAPON_BLOCKED_BY_FRIEND`) has two producers — the weapon's `0x1024f3d0` and the innate slot 573
`0x1026fcf0` — both reached only through slot 562, asked only by `GatherAttackConditions
0x1026dd10` on a ranged `0x4f`. V5a-1 ported the gather with its friend timers (`+0x5b88`,
`+0x5b8c`, `0x2e`), but the gather still answers slot 562 with the occlusion-latch stand-in (P3),
so the timers run only in tests. By the owner's rule "testable first" (`spec.md`) the producer
moves ahead of V5b: this lane runs in **V4b's wave, [B1, B2, V5a-3]**, after V5a and V4a are
committed. It needs nothing from V4o or V4c (the standing shot already fires; O3 and C1 only
change how).

**What the port already has (checked 2026-10-04 — S6's "neither source" is too strong):**
`FElysiumNpcBase::WeaponLOSCondition` and `InnateWeaponLOSCondition`
(`ElysiumNpcBaseSenses10.cpp` ~:450-546) are walked and already carry the `0x42`, `0x64`, `0x63`
and `0x66` arms. Three things are missing, and they are the lane:

1. the gather never calls slot 562 (`ElysiumNpcConditions.cpp` ~:1274-1300 reads the latch);
2. slot 562's weapon arm is a seam answering `true` (~:457-460): weapon slot 364 `0x1024f330` and
   its body `0x1024f3d0` are unported;
3. owner slot 389 `Weapon_ShootPosition 0x103338c0`, the weapon arm's start point, is a generated
   stub answering the zero vector (`ElysiumCombatCharacterSlots.cpp` ~:1021-1028).

Read `../v4/packets-S6.md` item 5, `README.md` here §1 step 5 and §2 P3, `AGENTS.md`, `spec.md`
§ Standing rules, `../v4/README.md` § "Rules for every agent of V4". The listings are the
authority: `vtmb_asm 1024f3d0` (whole, 170 lines), `vtmb_code 1024f330`, `vtmb_code 1026fbe0`,
`vtmb_asm 1026dd10` from `0x1026ded9` to `0x1026df69`, `vtmb_code 103338c0`.
**Re-locate every site by Grep on the function name**; cited lines are hints.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.cpp` (`GatherAttackConditions`' `0x4f`
  block ~:1270-1301 only) and `ElysiumNpcConditions.h` (its STAND-IN comment ~:715-719 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSenses10.cpp`, `.inl` (`WeaponLOSCondition`'s
  weapon arm, the new weapon line-of-fire body, `InnateWeaponLosTrace` and its seam comment;
  nothing else in the file)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlots.cpp` (slot 389's stub body
  ~:1021-1028 and its table row, `Stub` → the ported value; these only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSenses10Tests.cpp`,
  `Tests/ElysiumNpcKernelConditionsTests.cpp` (the gather's assertions only),
  `Tests/ElysiumNpcCombatTests.cpp` (only the assertions that pin the latch stand-in, ~:600-612)

Called, never edited: `FElysiumNpcBase::KernelHullTrace` and `KernelTraceKeepsCharacter`
(`ElysiumNpcBaseMotor.cpp` ~:164 / ~:232 — **B2's file**), `IElysiumEmbodiment::TraceRetail`
(`Public/ElysiumWorldServices.h` ~:1452 — **B1's file**), the mask recipe
(`Map/ElysiumRetailMaskRecipe.cpp` ~:24 already carries `0x46004003`), `Tests/ElysiumTestServices.h`
(its `TraceRetail` double ~:1793 — B1's; a line your test needs there goes in your report, exact).

## The job, line by line

1. **The gather's two dispatch sites** (`0x1026dede..0x1026df45`). Replace the latch stand-in with
   the slot, in retail's order:
   - first test (`0x1026dee4..0x1026def2`): `WeaponLOSCondition(ownerPos, target, 1)` — `ownerPos`
     is the owner's slot 217 (`+0x364`, called with no argument: the `PUSH 1` / `PUSH ECX` before
     it are slot 562's own second and third arguments; the port names this word `Origin`, see
     `AimGun`), `target` the vector at `[ESP+0x28]`, filled earlier from the enemy's slot 197
     `BodyTarget` (`+0x314`). Settled (`../v4/packets-S7.md` item 2): `0x1026de18..0x1026de30`,
     **every pass, unconditionally**, after slot 513 (`0x1026de0c`) and before the capability test
     `0x1026de3a`: `Enemy->BodyTarget(Origin, true, false)` (`posSrc` = the owner's slot 217,
     `bNoisy = 1`, fourth argument 0 — it draws random numbers each gather). Add that one call at
     that position (the single line of the gather outside the `0x4f` block that is yours) and keep
     its answer for the first test. The **player's** slot 197 (`CBasePlayer 0x10174e60`) is
     unported — `FElysiumEntity::BodyTarget` is a stub answering the zero vector: do not fix it;
     put in your report, for the integrator, the override `Origin + (EyePosition() − Origin) ×
     RandomFloat(0.5, 1.0)` (one draw; not noisy and the third bool set → `EyePosition()`; neither
     → slot 192). The comment at ~:1276 calls slot 193 "BodyTarget": slot 193 is `EyePosition`;
   - false → slot 560 again (`0x1026df00`, already there), then the enemy's slot 193 (`+0x304`,
     `0x1026df0f`, one out-pointer argument) copied to `[ESP+0x28]`, and
     `WeaponLOSCondition(slot 217, that, 1)` (`0x1026df3d`);
   - either true → `0x4f` (`0x1026df52`); both false → nothing set (`0x1026df45 JZ`).
   Delete the two `Out.Set(WeaponSightOccluded)` lines the stand-in raised by hand: the slot's
   bodies raise `0x66` / `0x63` / `0x42` / `0x64` themselves on `Cognition.Conditions`. Check that
   `Out` in this body **is** that set (the timers below read `Out.Has(0x63)`, `0x1026dfd0`); if
   it is a copy, say so and stop. The eye's latch (`BaseMemory.EnemyOccludedCheck`) keeps its
   other readers; it leaves this body only. Rewrite the header's STAND-IN paragraph to say what is
   now ported and what is still seamed (item 5).
2. **Weapon slot 364 `0x1024f330`** `(weapon; ownerPos, target, bSet)`: the owner is the weapon's
   `m_hOwner`'s NPC pointer (`+0x94`); `shootPos = owner slot 389 (+0x614)(ownerPos)`; then weapon
   vtable `+0x470` = `0x1024f3d0(owner, ignore = owner, &shootPos, target, bSet)`. The port stands
   no weapon vtable (one body fills the slot for the weapon classes, S6 item 5): write both as one
   private body beside `WeaponLOSCondition` — e.g. `WeaponLineOfFire(ShootPosCm, TargetCm,
   Ignore, bSetConditions)` — and call it from slot 562's weapon arm (`0x1026fc76`) in place of
   `bAnswer = true`. Slot 562's `0x10000000` cone (`0x10266b10`, 0.92 → `0x64`) still runs after
   it and still overrides a yes: unchanged.
3. **`0x1024f3d0`, in the listing's order**, each line citing its address:
   - one ray `shootPos → target` (`0x10015929`), filter `(ignore, group 0)` (`0x1000bd7f`), mask
     `0x46004003` (`0x1024f424`); the debug overlay under the cvar at `0x10738960`
     (`0x1024f42d..0x1024f45a`) is not ported, named;
   - `fraction == 1.0` (f64 `0x10449280`) → **true** (`0x1024f46e`);
   - `hit == owner slot 167 GetEnemy()` (`+0x29c`, `0x1024f4a4`) → **true**;
   - the hit is a combat character (`hit+0x9c != 0`): owner slot 404 `IRelationType(hit)`
     (`+0x650`) `== 1` (`D_HT`) → **true** (`0x1024f4d4`, shot through); else `bSet` →
     `SetCondition(0x63)` (`0x1024f502`); **false**;
   - no combat character: `hit m_CollisionGroup (+0x368) == 4` **and** `fraction > 0.0` (f32
     `0x104454c4`; `AND 0x4100 / JNZ` skips on `<=`) → the hit point `start + (end − start) ×
     fraction` and **the same body again** `(owner, ignore = the hit entity, &hitPoint, target,
     bSet)` (`0x1024f58c..0x1024f59e`: the new filter passes the hit entity, **not** the owner);
     else `bSet` → `SetCondition(0x66)` (`0x1024f5c7`); **false**. Unlike slot 573
     (`0x1026fe73`), this body does **not** record `m_hEnemyOccluder`: do not add it.
   - A blocked trace with a null entity faults in retail (`0x1024f509` reads `[ECX+0x368]`): take
     the `0x66` arm and say so at the line. Bound the recursion (retail's is unbounded; name the
     guard).
   **The trace.** `InnateWeaponLosTrace` is `KernelHullTrace` with a zero box, whose filter drops
   the tester and keeps characters by `KernelTraceKeepsCharacter`; its ".inl" comment ("answers no
   hit") is stale — `KernelHullTrace` answers `TraceRetail` now: correct the comment. Use it for
   the first ray when `ignore` is the owner. The re-trace ignores another entity: call
   `TraceRetail` yourself with that `Ignore` and fold the characters with
   `KernelTraceKeepsCharacter`, or give `InnateWeaponLosTrace` an ignore argument and a fraction
   out — inside your two files; the body needs **the hit entity, the fraction, and the entity's
   collision group**. Units: `KernelHullTrace` takes Source units, the slot's vectors are cm —
   convert at the line and say which side. With no world or no embodiment the trace is clear
   (today's answer), named as the fault path.
4. **Owner slot 389 `Weapon_ShootPosition 0x103338c0`** `(out; const Vector& origin)`: the active
   weapon's attachment `"muzzleflash"` (`GetAttachment01 0x10007680`, name `0x105cba6c`, asked of
   the **weapon** entity, position only) when it has one; else, settled from the listing
   (`../v4/packets-S7.md` item 1), in this float order:
   `((origin + forward·m_HackedGunPos.y) + right·m_HackedGunPos.x) + up·m_HackedGunPos.z`
   (`+0x157c` × forward `0x103339b9`, `+0x1578` × right `0x10333999`, `+0x1580` × up `0x10333972`;
   `AngleVectors 0x10139610` of slot 221 `+0x374` = `GetAngles`, the **body's** `m_angRotation`,
   not the eye angles — call `RetailGetAnglesDegrees()`, `ElysiumNpcBaseMotor10.inl` ~:164;
   only `AngleVectorsForward` exists, so write right and up from `0x10139610`'s listing beside
   your body; `m_HackedGunPos` is `HackedGunPosUnits`, `ElysiumNpcSpawn.inl` ~:38, Source units —
   convert). There is no eye-position fallback. The attachment: the port's two accessors
   (`IElysiumEmbodiment::GetBodyAttachment(Owner, FName, FTransform&)`,
   `FElysiumEntity::GetBodyAttachmentPoint(FName, FVector&)`) reach only placed bodies registered
   as use anchors, not a held wield model: **leave the attachment arm a seam answering "no
   attachment", named for `GetAttachment01("muzzleflash")` on the active weapon**, so every body
   takes the hacked-gun arm (retail's own arm for all but the 8 `muzzleflash` models; the
   thirty-eight has none) — and name in your report that the seam would be answered by
   `GetBodyAttachment(<weapon handle>, "muzzleflash", …)` once a held weapon registers. Delete the
   stub's `FireCombatCharacterSlot` line. **Check the other callers** of slot 389 (Grep
   `Weapon_ShootPosition(`: `AimGun` ~:400, `ElysiumNpcEntityChain2.cpp` ~:100) — they move from
   the zero vector to a real point; list each and what it now receives; fix nothing outside your
   files.
5. **Slot 573 `0x1026fcf0`**: compare the landed body against S6 item 5's line 2 and the listing
   (start `ownerPos + m_vecViewOffset`; hit character and `IRelationType != D_HT` → `0x63`; no
   character → `0x66` and `0x10270aa0(hit)`). It has no collision-group-4 re-trace — confirm from
   the listing and say so. Report a difference; change only what the listing contradicts.
6. **What stays a seam, named at its line**: the weapon's slot 360 capability word (the gather's
   existing `WeaponCapability` reader, V5a-1's — untouched); the muzzle attachment if item 4 found
   no accessor; `WEAPON_THROUGH_WALL 0x3c` (no producer in these three bodies — say where the
   header's comment names it).
7. **Arm tests**, each assertion naming its address:
   - `Elysium.Arm.NpcKernelSenses10.WeaponLineOfFire` (`0x1024f330`, `0x1024f3d0`) on the trace
     double: clear → true, no condition; the enemy hit → true; a hated character hit → true, no
     condition; a non-hated character hit → false and `0x63` with `bSet`, false and nothing
     without; a world hit → false and `0x66` with `bSet`, **`m_hEnemyOccluder` unwritten**; a
     group-4 entity at `fraction > 0` → the second ray starts at the hit point and ignores that
     entity (then each outcome behind it); a group-4 hit at `fraction == 0` → `0x66`; the ray
     starts at slot 389's point, not at `ownerPos`.
   - `.ShootPosition` (`0x103338c0`): the hacked-gun arm's three components on a turned body.
   - `.WeaponLOS` (`0x1026fbe0`): an armed body reaches the weapon body (no longer `true`
     unasked); unarmed with `0x20000` → slot 573; neither → `0x42`; the `0x10000000` cone
     overrides a weapon yes with `0x64`.
   - `Elysium.Arm.NpcKernelConditions.GatherAttackWeaponLos` (`0x1026dede..0x1026df52`): on a
     `0x4f` answer, slot 562 is asked with slot 197's point, then — after slot 560 — with slot
     193's; first true → one ask; both false → no `0x4f`; a friend on the line → `0x63` raised
     **by the gather's own path** (no test hook), `+0x5b88 = curtime + 1.5` (`0x1026e006`),
     `+0x5b8c = curtime + 2.5` (`0x1026dfe7`), `0x4f` cleared by the tail (`0x1026e087`); a
     non-`0x4f` answer never asks slot 562.
   - `GatherAttackFriendTimers` keeps its `RaiseInRangedArm` hook (it pins the timers alone);
     rewrite or delete only the assertions that pin the latch as slot 562
     (`ElysiumNpcCombatTests.cpp` ~:608-612 "the occlusion stand-in replaces 0x4f only";
     `ElysiumNpcKernelConditionsTests.cpp` ~:926) and list them.

## The arena record that proves it — `combat/ranged_friend_in_line_of_fire`

Not yours to write: the V4b integrator writes it (`../v4/brief-B-integrator.md` § "V5a-3 shares
this wave"), red until this lane lands. Stated here so your port and its test agree with it.

- **Staging** (arena host, seed 1; built on `ranged_open_fire`'s cast): the player at
  `cover_seat` facing the shooter; `arena_shooter`, `npc_VHumanCombatant` at `far_ne` facing the
  player, `item_w_thirtyeight`, `player_reaction "D_HT 5"`, `hint_groups "2"` (no cover hint, as
  `ranged_open_fire`); `arena_friend`, a second `npc_VHumanCombatant`, **unarmed**, neutral to the
  player, standing on the segment shooter → player at its midpoint, both of the shooter's lines
  (to the player's slot 197 point and slot 193 point) crossing its body: the lines start at the
  shooter's origin + 55 units up and end between half and full view height above the player's
  origin (`0x10174e60`) and at the player's eye. **No relation input is staged** (settled,
  `../v4/packets-S7.md` items 3-4): two `npc_VHumanCombatant`s with no relationship row are `D_NU`
  (4) to each other — `CBaseCombatCharacter::IRelationType 0x10333340`'s default, the friend's
  `Classify 0x103871a0` (4) matching no class row, `player_reaction` being an entity row for the
  player only — and anything but `D_HT` blocks. There is no team key. The record's `about` cites
  those two addresses. The record also needs the player's slot 197 `0x10174e60` (item 1): with
  the stub's zero vector the first test aims at the world origin.
- **Script**: `kill arena_friend` at `t: 0.8`.
- **Expect**, in order: `blocked` — shooter `cond+ WEAPON_BLOCKED_BY_FRIEND (0x63)` `by: 0.7`
  (`0x1024f502` through `0x1026def2`); `unblocked` — its `cond-`; `fires` — `task_range_attack1`;
  `shot_event` — `animevent ^3031`; `fired` — `taskdone task_range_attack1`.
- **Never**: shooter `animevent 3031` `until: 1.4` and `damage` on the player `until: 1.4` (the
  first raise cannot precede 0.0 and `+0x5b88` holds `0x63` and clears `0x4f` for 1.5 s after
  every raise, `0x1026e006` / `0x1026e068..0x1026e087`); `damage` on `arena_friend`, whole run (no
  shot leaves while the friend stands in it); shooter `cond+ EXTENDED_BLOCKED_BY_FRIEND (0x2e)`,
  whole run (the block lasts under 1.0 s, so `+0x5b88` lapses before `+0x5b8c = first + 2.5` and
  `0x1026e02c` resets it to `FLT_MAX`); shooter `death`.
- **Red before the lane**: `blocked` unmet and a 3031 inside 1.4 s — the port shoots through the
  friend. A timing that fails after the lane is read against the listing, never loosened.

## Not yours

`GatherAttackConditions` outside its `0x4f` block — the timers and the tail are landed and retail
(V5a-1); the weapon classes and the shot (`ElysiumWeaponClasses.*`: O3, C1, V11-2 — `Shot
0x102387b0` has **no** line-of-fire gate, S2; do not add one there); slot 360; the schedules that
read `0x63` / `0x2e` (`ElysiumNpcHuman.cpp`, `ElysiumNpcCombat10_2.cpp`, `ElysiumNpcAsianVampire.cpp`:
already retail's readers); Ming Xiao's `m_bBlockedByFriend`; the melee friend tests (`0x102a11d0`,
slot 331's `0x3a`: V11); `IElysiumEmbodiment` and every file of B1 and B2; any record under
`Arena/`; `kernel_verdicts.tsv` (the rows for `0x1024f330`, `0x1024f3d0`, `0x103338c0` go in your
report for the integrator).

Wave check ([B1, B2, V5a-3], listed 2026-10-04): your eight files are in neither list — B1:
`Visual/ElysiumNpcBody.{h,cpp}`, `Visual/ElysiumNpcMoveScript.{h,cpp}`,
`Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h`, `Map/ElysiumMapActorEmbodiment.cpp`,
`Tests/ElysiumTestServices.h`, `Tests/ElysiumNpcMoveScriptTests.cpp`; B2:
`ElysiumNpcMotor10.{cpp,inl}`, `ElysiumNpcBaseFacing.cpp`, `ElysiumNpcBaseMotor.cpp`,
`ElysiumNpcThink.cpp`, `Tests/ElysiumNpcKernelFacingTests.cpp`,
`Tests/ElysiumNpcKernelMotorTests.cpp`. **By ownership of functions**: you call
`KernelHullTrace` / `KernelTraceKeepsCharacter` (B2's file) and `TraceRetail` (B1's header) and
edit neither. If `packets-R1b.md` names one of your files for B2's slow turn (B2 item 4), the
file stays yours and B2 reports its line.

## Rules

Coders never build, never launch the editor, never run the arena or a suite. Retail first: follow
the listing, cite the address at every line you port. A divergence is recorded in your report, not
adopted. Query budget: 10 s warns (log to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops
— never retried as-is or widened. Never read a file over ~200 KB whole. Text through Grep / Read /
Glob, never shell `grep` / `cat` / `sed`. `uv run elysium research where <addr>` before searching
`docs/`. Do not commit. Report ≤300 words: what you ported (addresses); the gather's two target
points as the listing gives them; how the re-trace ignores the hit entity; slot 389's basis order
and whether the muzzle attachment is a seam (with the accessor's signature); slot 389's other
callers and what they now receive; slot 573 against the listing; tests added, rewritten and
deleted; cross-lane lines; what stays unrecovered.
