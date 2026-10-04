# Brief O3 — V4o: an NPC's 3031 is the shot (coder; no build)

Read `README.md` here (§1 "The shot", §2 P6, §3, §6, §7), `stories/v4/packets-R2.md` § 3 (a),
`docs/vtmb/combat-and-damage.md` § "Animation schedules the shot moment; weapon logic commits it"
and § "The NPC attack producers, start to commit" (`uv run elysium research section 0x10238160`).
After V4a's commit and V4b's. **Amended after settling packets S2 and S4 and the judge's second
sitting, 2026-10-04** (`stories/v4/packets-S2.md` items 1 and 2: `ModeDispatch` and `Shot` are
read, item 1 below is their answer written out; `stories/v4/packets-S4.md` item a and J12: **the
NPC's clip is never spent — item 3b**; record `ranged_sustained_fire`). Re-locate every site by Grep on the function name.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h`, `ElysiumWeaponClasses.cpp`
  (`CommitFromAnimEvent`, the new `ShotFromAnimEvent`, whatever of `BeginRangedShot` you
  factor out to share, and — item 3b, J12 — `CommitQueuedAttack`'s magazine block ~:2313-2323 —
  nothing else)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`

## The job

1. **Retail, as read** (S2 item 2; listings `0x102387b0`, `0x102383b0`) — port exactly these
   gates and no other. No attack is staged before the event on this path.
   `CWeaponRanged::HandleAnimEvent 0x10238160` (events 3030..3044) → `0x10238320`
   (`DAT_1088aee4 = 0`, `ModeDispatch(1)`). `ModeDispatch 0x102383b0`: weapon `+0x848 =
   weapon[+0x84c + slot×4]` (the current activate-mode tag); a slot other than 0 / 1 → return;
   the mode record `0x102517e0` (the one whose `+0x104` equals `+0x848`; no match → a static
   default record with type 0); then on the record's **type `+0x108`**: **1 or 2 → slot 373
   `Shot`**; 3 → the zoom step (player only) and the next-attack times `= curtime +
   GetFireRate`; 4 → the fire-mode toggle; 6 → the throw (`0x10239e70`; its "launch" `0x10239f30`
   creates nothing, `../v4/packets-S5.md` item 5 — not yours); any other → `+0x730 += GetFireRate`, `+0x734 =` the same,
   **nothing fired**. `Shot 0x102387b0`, for an NPC, in order:
   1. Zoomed (weapon data `+0x4fe84 > 0 && +0x914 > 0`): the mode record is re-read as tag 2.
   2. **Owner** (`0x10252240`) null → return. `AddMiscFlag(0x200000)` on the owner.
   3. **Not a player and the owner's NPC pointer (`+0x94`) null → return.** NPC: slot 333
      `(6, 1, 0, 0, 0, 0)` — the weapon activity (the player's is `(1, 1, …)`).
   4. Weapon data `+0x50170` → the owner's `m_fEffects |= 2`.
   5. The shoot position: owner slot 389; the direction: the NPC's slot 574 `(&out, &shootPos,
      1, 0)`.
   6. **The cooldown is a count, not a refusal**: `sets = 0`; `rate` = slot 332 `0x10254410`
      (the mode record's `Attack_Rate +0x260` through the owner's `0x1033d940`); `next =
      max(m_flNextPrimaryAttack (+0x730[slot]), curtime − frametime)`; `while (next <= curtime)
      { next += rate; ++sets; }` (with `m_iAtkMode +0x86c != 0` the rate is re-read each step).
      **An event that arrives while `m_flNextPrimaryAttack > curtime` fires zero bullets — and
      still plays the weapon activity (step 3) and inserts the sound (step 9).**
   7. **The clip caps, never refuses outright**: `n = sets × Ammo_Cost (+0x110)`; if `n > 0` and
      `clip = m_iMagazineCurAmts[ammo index +0x10c] (+0x74c) < n` → `sets = clip / Ammo_Cost`.
      An empty clip gives zero sets. **The NPC's clip is not decremented** (the subtraction is
      inside the player-only block `0x10238a1d`).
   8. **No line-of-fire gate**: the one trace (2 048 units, mask `0x46004003`) feeds a debug
      overlay and a discarded `GetFlags`. Do not add one.
   9. Writes: per set, owner slot 185 `FireBullets` (shots `Ammo_Fired +0x114`, the mode's
      damage, the ammo type, the tracer data); `CSoundEnt::InsertSound(1, owner origin,
      [0x1072bc40], 0.2)`; slot 339 `Kick` when the owner's slot 220 answered non-null;
      `+0x730[slot]` advanced to `next`.
   **Settled by the judge's second sitting** (`stories/v4/packets-S4.md` items a and f.1; J12 —
   `stories/v1/triage.md` § "Judge's rulings, V4 — second sitting", Grep, read only it):
   - **Who fills the NPC's clip.** `m_iMagazineCurAmts` is `CBaseCombatWeapon +0x74c`, two
     words. `CBaseCombatWeapon::Spawn 0x10250ce0`, per magazine: "uses a clip" (slot 277) false
     → −1; no weapon data → 0; else `clip = max(clip, Size)`. Then
     **`CBaseCombatCharacter::Inventory_Insert 0x10334e70`** (first call of `Weapon_Equip
     0x1032d380`), only when the owner has an NPC pointer and `0x102585c0(weapon)`: `n =
     max(Default_Size, 1)`; `Size == −1` → the owner is given `n` ammo (slot 374); else
     **`clip[0] = n`**. Nothing after that: `Shot` subtracts only inside the player block
     (`0x10238a15 TEST EAX,EAX / JZ` on the player pointer, then `0x10238a4b SUB`). **An NPC's
     `CWeaponRanged` clip is `max(Default_Size, 1)` from equip to death; firing never lowers it;
     `NO_PRIMARY_AMMO 0x40` cannot rise from firing.**
   - **`0x1033d940`** (the rate's scale): `0x101e3f50(&0x10739a4c, owner)` true → `rate × 2`;
     else `rate`. The test is a bit family on the owner's `+0xeb4` word (five levels of one
     status). **The status is Presence — discipline id 10, the five level bits in
     `m_iDisciplineFlags2`** (`../v4/packets-S5.md` item 5; the bit values are the run-time
     table's). Port it as **a seam named for `0x1033d940` / `0x101e3f50` — "a Presence level bit
     in `m_iDisciplineFlags2` doubles the attack rate"** — reading the port's Presence bits if
     `DisciplineFlags2` carries them (Grep; say which), else answering false, so an NPC's slot
     332 is `Attack_Rate` unscaled.
   - The mode record's key at `0x105402f8` is the string `Type`.
2. **`FElysiumWeapon::ShotFromAnimEvent(const FElysiumAnimEvent&)`**: for a ranged operator body
   whose owner is an NPC and with **no transaction staged**, the commit event (3030..3044) stages
   the shot's transaction and queues its commit in the same call — the mode, the victim (the
   owner's enemy, as the AI cycle hands it today), the damage spine and the queue discipline
   (`QueueSelfInput`, delay 0.0) are `BeginRangedShot`'s, **without** `ResolveAndPlay` (the layer is
   already playing: the kernel pushed it), without the `ContactEventCycle` commit time and without
   the `HasLiveAnimEventDispatch` question. Factor the shared part out of `BeginRangedShot`; change
   nothing in what it does today. **Item 1's gates in item 1's order**: the mode type must be 1
   or 2 (any other type fires nothing and only advances the two clocks); owner, then the NPC
   pointer; the weapon activity and the sound **even at zero sets**; the count from the cooldown
   (`sets` bullets-sets, each `Ammo_Fired` shots), capped by the clip, **no decrement for an
   NPC**, no line-of-fire test; `m_flNextPrimaryAttack` advanced by the loop.
3. **`CommitFromAnimEvent`**: `!Swing.bActive` on that operator → `ShotFromAnimEvent`. A staged
   transaction commits as today. The player's path is untouched.
3b. **The NPC's clip: no spend and no refusal — an old bug in landed work, fixed here** (J12).
   Today `FElysiumWeapon::CommitQueuedAttack` (`ElysiumWeaponClasses.cpp` ~:2313-2323) refuses a
   shot the magazine does not cover and **subtracts `Ammo_Cost` for every wielder**: a port
   gunman with the .38 (`Size 6`) fires six times, raises `NO_PRIMARY_AMMO`, reloads to no
   effect (`WeaponFinishReload` fills nothing) and loops, where retail's keeps firing. That is
   on step-2 paths (`ranged_open_fire`'s shooter, `cover_armed`, the tutorial's `thug_3`) and it
   changes which programs run. Fix, in the commit both paths share:
   - **for a non-player wielder** the sets are **capped** by the clip (`sets = clip / Ammo_Cost`
     when `clip < sets × Ammo_Cost`; item 1 step 7) and **the clip is not written**; no refusal
     beyond that cap. This holds for the staged (standing) shot as well as your unstaged entry —
     it is one commit;
   - **the player's branch is untouched**: it still refuses and still subtracts (retail's
     player block `0x10238a15`); the player's weapon tests must not move;
   - **equip**: an NPC's clip is `max(Default_Size, 1)` (`Inventory_Insert 0x10334e70`). The
     port's `FElysiumItem::Spawn` gives `Default_Size` (`ElysiumItemClasses.cpp` ~:111-113 — not
     your file): check by Grep that the value an NPC-held weapon ends with is `max(Default_Size,
     1)`; if the `max(…, 1)` or the NPC-only equip write is missing, write the exact line in
     your report for the integrator;
   - **the reload stays a seam, not yours**: `FElysiumNpcBase::WeaponFinishReload`
     (`ElysiumNpcBaseRunTask.cpp` ~:259-264) is unreachable from firing once the spend is gone.
     Its comment is to carry the body as read — `TASK_RELOAD` at activity finished:
     `m_bInReload = 1`; weapon slot 322 `0x10255050` (single-round weapons: player only; bulk,
     slot 323 `0x102552c0`: `clip += min(Size − clip, owner's reserve)`, reserve untouched, both
     next-attack words `= curtime`); owner V5b. That file is not yours (V4c C2 holds
     `StartFadeOut` in it; no V4o lane does): write the replacement comment in your report and
     the integrator applies it.
   Two other decrements exist in retail and are not on the two maps — leave them, named:
   `CWeaponRanged_FlameThrower::Attack 0x103e2f30` (`+0x74c −= 1`; **it has no NPC gate — an NPC's
   flamethrower spends its clip**, `../v4/packets-S5.md` item 5) and mode type 6 (the throw:
   `0x10239e70` spends one and drops the emptied weapon; its "launch" `0x10239f30` creates
   nothing).
4. **Tests** `Elysium.Arm.Weapon.ShotFromAnimEvent` (`0x10238160` → `0x10238320` →
   `0x102387b0`): an NPC wielder, nothing staged, 3031 → one shot committed against its enemy;
   **a second 3031 inside the cooldown (`m_flNextPrimaryAttack > curtime`) fires nothing** —
   zero bullets, the weapon activity and the sound still played, no clock moved back; a second
   3031 **once `m_flNextPrimaryAttack <= curtime`** → a second shot; a clip smaller than the
   cost caps the sets and an empty clip gives zero; the NPC's clip is the same after the shot; a
   mode of type other than 1 / 2 fires nothing and advances the clocks; no owner, or a
   non-player owner with no NPC pointer → nothing at all; a 3031 with a transaction staged
   commits that transaction once; a player wielder with nothing staged still commits nothing.
   **J12**: an NPC wielder fires `Size + 2` shots across cooldowns with its clip unchanged and
   is never refused; a player wielder still spends `Ammo_Cost` per shot and is refused on an
   empty magazine; the doubled-rate seam answers false (the rate is `Attack_Rate`). **Delete the
   weapon tests that pin an NPC wielder's spend or its empty-magazine refusal** — they pin a
   port mechanism — and list them.

## Not yours

The `ContactEventCycle` estimate and the standing shot (`AttackIntent` → `BeginRangedShot`, the
`UpperBody` play for an NPC): V4c lane C1, which builds on your entry — say in your report exactly
what C1 can now delete. The dispatcher and the layers (V4a, O1), the overlay body (O2), melee,
`Weapon_FrameUpdate`. **Key your arm on the operator body, as J6 rules, not on "is an NPC"
alone — there is one ranged body**: `0x10238160` serves `CWeaponRanged` and all 16 subclasses, the
species ones included (flamethrower, Ming Xiao's spit, the Tzimisce head, the crossbows); none has
its own operator body or its own `Shot` (S2 item 1). The base body (thrown, unarmed, discipline)
and the melee bodies never reach your entry. (An earlier text spoke of "species weapon bodies R2
did not read": there are none.)

Wave check (O1 / O2 / O3, re-checked after S3 and the second sitting): `ElysiumWeaponClasses.{h,cpp}`
and `ElysiumWeaponTests.cpp` are in neither O1's list nor O2's (which gained
`ElysiumNpcMotor.cpp`). J12 adds no file to you: the magazine block is in your
`ElysiumWeaponClasses.cpp`; `ElysiumItemClasses.cpp` and `ElysiumNpcBaseRunTask.cpp` are in no
V4o lane — lines for them go to the integrator.

Report, beside the rest: the tests you deleted for the NPC spend, the equip value you found, the
reload seam's replacement comment, and — for V4c's C1 — what it can now delete.

## Rules

README § "Rules for every agent of V4o". Retail first; cite the address at every line. Never
build, launch or run. Only your files; a line another file needs goes in your report, exact, with
its place. A divergence is recorded, not adopted. Query budget: 10 s warns, 60 s stops. Never read
a file over ~200 KB whole (`ElysiumWeaponClasses.cpp` is large: Grep, then Read the section).
Grep / Read / Glob. Do not commit. Report ≤300 words.
