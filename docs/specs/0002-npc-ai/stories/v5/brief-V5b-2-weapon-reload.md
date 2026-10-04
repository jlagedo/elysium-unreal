# Brief V5b-2 — the weapon side: slots 280 / 322 / 323, the count per set, the Presence seam's text (coder; no build)

Read `README.md` here (§1.2 "The count-down", §1.3, §1.5, §2 P3, P5, P7, §3, §6), `AGENTS.md`,
`spec.md` § "Standing rules", `../v4/packets-S4.md` item a (who fills an NPC's clip; J12),
`docs/vtmb/combat-and-damage.md` § "Reload and dry fire" (`uv run elysium research section
0x102552c0`). **Re-locate every site by Grep on the function name**; cited lines are hints.
Retail was read for you: `0x10288780` (`0x102890f3..0x102891b9`), `0x10255050` (its decompilation
is damaged at three tail jumps — each is `JMP [vtable + 0x50c]` or `+0x514`; the non-single arm is
whole), `0x102552c0`, `0x10253ab0`, `0x10268900` (`0x10268919`). If a body you meet contradicts
this brief, stop and report it.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h`, `ElysiumWeaponClasses.cpp`
  (three new bodies; the per-set loop of the shot; `PresenceDoublesAttackRate`'s comment)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunTask.cpp` (`WeaponFinishReload` ~:259 only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunTask.inl` (its declaration ~:113-117)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`,
  `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelRunTaskTests.cpp`

## The job

Declare, with exactly these names (lane 1 calls the first; README §3):

```cpp
bool CanReloadMagazine(int32 MagazineIndex) const;   // slot 280, 0x10253ab0
void FinishReload();                                 // slot 322, 0x10255050
void FinishReloadBulk();                             // slot 323, 0x102552c0
bool bInReload = false;                              // +0x898 m_bInReload
```

The port's weapon carries **one** magazine (`FElysiumItem::MagazineCount`, `AmmoType`; the mode
record's `MagazineSize`, `bReloadSingle`). Magazine index 1 is a named seam: slot 277 (`+0x454`,
"uses a clip") answers false for it, stated at the line with the retail word
(`m_iMagazineCurAmts[1] +0x750`).

1. **Slot 280 `CanReloadMagazine(i)`**, four arms in order: no ammo type for `i`
   (`m_iAmmoTypes[i] < 0`, `0x10253b40`; the port: an empty `AmmoType`) → **true**; uses a clip
   and `clip[i] > 0` → true; an owner whose reserve of that type is `> 0`
   (`GetAmmoCount 0x103346c0`; the port's `Inventory.Reserve(AmmoType)` on the owner) → true;
   else false.
2. **Slot 323 `FinishReloadBulk`**. No owner → nothing at all. `bReloadSingle` clear: for each
   magazine that uses a clip: `Add = min(Size − clip, owner's reserve)` — the listing's
   `if (Size − clip < reserve) Add = Size − clip; else Add = reserve`; `clip += Add`; the reserve
   is debited **only for a player owner** and only under retail's cvar `DAT_1088aef4`
   (`IsCommand()` or its int `< 1`): for an NPC nothing is removed. If the port's player reload
   (`CommitQueuedReload`) already owns the player's debit, do not route the player here — this
   body is reached only from slot 322; name the cvar as a seam answering "debit" if no such cvar
   stands in the tunables. `bReloadSingle` set: `0x10254cd0(this, 0xc3)` — a weapon activity send;
   a named seam answering nothing unless the port has the call. Then, both arms: `bInReload =
   false`; `m_bIsJammed`, `m_bInterruptReload` cleared where the port has them, named where not.
3. **Slot 322 `FinishReload`**. `bReloadSingle` clear: an owner that is a combat character,
   `bInReload`, and the owner's `m_flNextAttack (+0x1564) <= curtime` (`<=`: the listing's
   `(a < b) != (a == b)`) → `FinishReloadBulk()`, then **both** next-attack stamps (`+0x730`,
   `+0x734`: `NextPrimaryAttackTime`, `NextSecondaryAttackTime`) `= curtime`. `bReloadSingle` set:
   every arm needs a **player** owner; for an NPC return with nothing changed (`bInReload` stays
   true — retail's own). The player's single-round arms are the player story's: name them at the
   line (`0x102550b4`, `0x102550d2`, `0x102551cb..0x102551dc`), port nothing of them.
   `m_flNextAttack` on an NPC has no writer in the port (named at its declaration): it reads 0,
   which is retail's "`<= curtime`".
4. **`FElysiumNpcBase::WeaponFinishReload`** (`0x1028918d`, `0x1028919d`): the entity is the active
   weapon; set `bInReload = true`, then call `FinishReload()`. An entity that is not an
   `FElysiumWeapon` keeps the count and does nothing. `WeaponFinishReloadCalls` stays as the
   call's witness. The arm around it (`AutoMovement`, the turn, slot 251, the two condition
   clears, `TaskComplete`) is retail and not yours.
5. **The fake-reload count, per bullet set** (`FireBullets 0x10268900`, slot 185: `0x10268919 DEC
   [m_pBaseNPCTroika + 0x65f0]`, its first statement, once per call; `Shot 0x102387b0` step 9
   calls it once per set). The port's shot stands a per-set loop for slot 185 (`TraceShotImpact`,
   in the queued-attack commit; Grep `One trace per set`). In **that loop**, per set, when the
   attacker is an NPC (`AsNpc()`): `--FakeReloadCount`. Every path that fires an NPC's sets must
   pass it once and only once — the event shot (`ShotFromAnimEvent` → the commit) and the
   move-and-shoot layer's shot (V4o) reach the same commit; verify by reading and say so. The
   player decrements nothing (`+0x98` null). Do not call `FElysiumEntity::FireBullets`
   (`ElysiumEntitySlotBodies.cpp`, not your file): it would trace a second time.
6. **`PresenceDoublesAttackRate`'s comment** (`0x1033d940` / `0x101e3f50`): replace "nothing writes
   a Presence bit … the bit values are the run-time table's" with README §1.5 — the bit is set
   only by `AddDiscFlag 0x1033cfb0` from the discipline manager's status apply `0x101e3560`
   (callers `0x101e2f50`, `0x101e3380`, `0x101e33c0`, `0x101f8620`), i.e. while a Presence effect
   cast on the owner is applied; owner spec 0006. It keeps answering false. Same sentence in the
   `.h` (~:711-714).
7. **Tests** (README §6): `Elysium.Arm.Weapon.ReloadFinish`, `.CanReloadMagazine`,
   `.FakeReloadCountPerSet` (use the weapon tests' actual prefix);
   `Elysium.Arm.NpcKernelRunTask19.ReloadFinish`. Delete any assertion that pins
   `WeaponFinishReload` doing nothing; list what you deleted. `ranged_sustained_fire`'s rule
   stays true and is not yours to prove: an NPC's shot neither spends nor refuses on the clip (O3).

## Not yours

The pre-pass and the template's range (lane 1). `ActiveWeaponCapabilityWord` (lane 3).
`ShotFromAnimEvent`'s gates, the stamp and the clip rule (O3, landed), the swing and the contact
(V11-2, C1), the player's `BeginReload` / `CommitQueuedReload`, `ElysiumEntitySlotBodies.cpp`,
`ElysiumItemTable.*`, `ElysiumItemClasses.*` (if a word you need has its home there, write the
exact declaration in your report). The records.

## Rules

Coders never build, launch the editor or run a suite. Retail first: the listing decides, and a
divergence you find is **recorded in your report, not adopted**. A shadowed local or member (C4458 /
C4459) is a compile error here. Query budget: 10 s warns, 60 s stops (never retried as-is, never
widened); never read a file over ~200 KB whole (`ElysiumWeaponClasses.cpp` and
`ElysiumWeaponTests.cpp` are near it: Grep the function, then Read the range); text through Grep /
Read / Glob, not shell. Only your files; a line needed elsewhere goes in the report, exact. No
commit. Report ≤300 words: the bodies ported (addresses), the seams left with their retail word,
which paths reach the per-set count, tests added and deleted, cross-lane lines.
