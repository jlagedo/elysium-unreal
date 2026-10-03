# R3 — the weapon range words and the range-attack condition (read 2026-09-30, from the corpus)

Found by the green-room cover scenario: the port's ranged NPC is flagged both TOO_FAR and
TOO_CLOSE and never reaches `CAN_RANGE_ATTACK1`.

## `+0x8b8` / `+0x8c0` — `m_fMinRange1` / `m_fMaxRange1` on `CBaseCombatWeapon`

- The item text's `Range` key is NOT their source. `WeaponModeDataLoader 0x10259230` reads
  `"Range"` as an int (GetInt, default 0, `0x10259437`) into the weapon-mode record `+0x270`; no
  server reader of that word was found (`client.dll 0x10184bc0` reads it; not examined). What
  `Range` means in retail is unrecovered.
- The writers are the constructors: `CBaseCombatWeapon 0x10250ac0` min1 65 / max1 1024;
  `CWeaponRanged 0x10238070` min1 150 / max1 1024 / min2 65 / max2 300 (the `.38` factory
  `0x102371c0` uses it); `CWeaponMelee 0x103e9ac0` max 50 (other melee classes 108 or 500).
- `Weapon_Equip 0x1032d380` sets both max words to 1e9 when the NPC carries spawnflag `0x100`.
- The map keyvalue `weapon_maxrange1` can set the max word.
- Readers of `+0x8c0`: `0x10270b20`, `0x102b6b50` (the shoot-at search radius), `0x10296c40`
  (the attack-position validator), the StartTask radius sites, and slot 365 below.

## `CAN_RANGE_ATTACK1` — weapon slot 365 `0x1024f670`

Called from `GatherAttackConditions 0x1026dd10` at `0x1026de87` with `(enemy, dot, d)`. Answers
the FIRST match, in this order; every compare strict; a NaN `d` passes every edge and reaches
`0x4f`:
1. empty clip → `0x40`
2. `d < 100` → `0x08`
3. `d < +0x8b8` (150 for every firearm) → `0x5f` (TOO_CLOSE)
4. `d > +0x8c0` (1024) → `0x60` (TOO_FAR)
5. `dot < 0.5` → `0x61`
6. else `0x4f` (CAN_RANGE_ATTACK1) when the attack timer `+0x730` has run out, else nothing.

## The port's divergence

- `ElysiumNpcConditions.cpp:~1111` uses `Mode->Range × U` (the `.38`'s `Range 35` → 89 cm) as the
  far edge where retail uses the weapon's `+0x8c0` (1024 units); `:~1121` uses the 64-unit melee
  reach as the near edge where retail uses 100 then `+0x8b8` (150); the port never raises `0x08`
  on this path, raises several conditions at once where retail returns exactly one, and measures
  eye to eye rather than through `0x10270890`.
- `ActiveWeaponMaxRangeUnits` (`ElysiumNpcKernelBaseHelpers.cpp:~680`) and
  `TaskTailWeaponMinRangeUnits` always answer false; they can be filled from the words above.
- `ElysiumWeaponClasses.cpp:~1391` and `:~2254` make the same `Range` misreading on the firing
  path.
