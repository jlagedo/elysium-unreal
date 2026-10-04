# Brief O3 — V4o: an NPC's 3031 is the shot (coder; no build)

Read `README.md` here (§1 "The shot", §2 P6, §3, §6, §7), `stories/v4/packets-R2.md` § 3 (a),
`docs/vtmb/combat-and-damage.md` § "Animation schedules the shot moment; weapon logic commits it"
and § "The NPC attack producers, start to commit" (`uv run elysium research section 0x10238160`).
After V4a's commit. Re-locate every site by Grep on the function name.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h`, `ElysiumWeaponClasses.cpp`
  (`CommitFromAnimEvent`, the new `ShotFromAnimEvent`, and whatever of `BeginRangedShot` you
  factor out to share — nothing else)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`

## The job

1. **Read retail first**: `CWeaponRanged::HandleAnimEvent 0x10238160` → `0x10238320` →
   `ModeDispatch(1) 0x102383b0` → slot 373 `Shot 0x102387b0`. Write down in your report what mode 1
   tests before the shot leaves (ammo, the next-attack clock, the operator) — only those gates are
   ported. No attack is staged before the event on this path.
2. **`FElysiumWeapon::ShotFromAnimEvent(const FElysiumAnimEvent&)`**: for a ranged operator body
   whose owner is an NPC and with **no transaction staged**, the commit event (3030..3044) stages
   the shot's transaction and queues its commit in the same call — the mode, the victim (the
   owner's enemy, as the AI cycle hands it today), the damage spine and the queue discipline
   (`QueueSelfInput`, delay 0.0) are `BeginRangedShot`'s, **without** `ResolveAndPlay` (the layer is
   already playing: the kernel pushed it), without the `ContactEventCycle` commit time and without
   the `HasLiveAnimEventDispatch` question. Factor the shared part out of `BeginRangedShot`; change
   nothing in what it does today.
3. **`CommitFromAnimEvent`**: `!Swing.bActive` on that operator → `ShotFromAnimEvent`. A staged
   transaction commits as today. The player's path is untouched.
4. **Tests** `Elysium.Arm.Weapon.ShotFromAnimEvent` (`0x10238160` → `0x10238320`): an NPC wielder,
   nothing staged, 3031 → one shot committed against its enemy; a second 3031 → a second; a 3031
   with a transaction staged commits that transaction once; a player wielder with nothing staged
   still commits nothing.

## Not yours

The `ContactEventCycle` estimate and the standing shot (`AttackIntent` → `BeginRangedShot`, the
`UpperBody` play for an NPC): V4c lane C1, which builds on your entry — say in your report exactly
what C1 can now delete. The dispatcher and the layers (V4a, O1), the overlay body (O2), melee,
`Weapon_FrameUpdate`, the species weapon bodies R2 did not read (their operator bodies keep
today's behaviour: key your arm on the operator body, as J6 rules, not on "is an NPC" alone).

## Rules

README § "Rules for every agent of V4o". Retail first; cite the address at every line. Never
build, launch or run. Only your files; a line another file needs goes in your report, exact, with
its place. A divergence is recorded, not adopted. Query budget: 10 s warns, 60 s stops. Never read
a file over ~200 KB whole (`ElysiumWeaponClasses.cpp` is large: Grep, then Read the section).
Grep / Read / Glob. Do not commit. Report ≤300 words.
