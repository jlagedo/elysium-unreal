# Brief S6 — the weapon range words and slot 365, ported (spike fix)

Read `CLAUDE.md`. Retail is recovered in
`docs/specs/0018-world-ai-infrastructure/story8/findings-R3-weapon-range.md` — read it first and
confirm each address against the listing with the `vtmb-corpus` MCP (`vtmb_code`) before porting;
if the listing disagrees with the findings, the listing wins and you say so. You edit ONLY:
- the port's weapon record (`Source/ElysiumUE/Private/Substrate/ElysiumWeapon*.{h,cpp}`,
  `ElysiumWeaponClasses.cpp`, `ElysiumNpcLoadout*` — whichever holds the carried weapon's words)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcConditions.cpp` (the range-attack producer)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBaseHelpers.cpp` (`ActiveWeaponMaxRangeUnits`,
  `TaskTailWeaponMinRangeUnits` only — another agent just finished in this file; re-read it first)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers.cpp` (`TroikaShootAtHintDefaultRadiusUnits`
  stand-in → the weapon's max word, `0x102b6b50`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcHints.cpp` (`ValidateHintCoverRange`'s weapon-range
  term → the weapon's max word)
- tests under `Source/ElysiumUE/Private/Tests/` that pin the old behaviour (grep `Range`,
  `TOO_FAR`, `TOO_CLOSE`, `CAN_RANGE_ATTACK1`, `ActiveWeaponMaxRangeUnits`)
Do NOT build or run. Report ≤300 words: every arm, every test changed, anything unrecovered.

## Deliver
1. `m_fMinRange1 +0x8b8` / `m_fMaxRange1 +0x8c0` (and the mode-2 pair if cheap) on the port's
   weapon record, set by the class constructors' values (base 65 / 1024; ranged 150 / 1024,
   65 / 300; melee 50, others 108 / 500 — read each melee class's constructor for its value and
   map it to the port's weapon classes), the `weapon_maxrange1` keyvalue, and `Weapon_Equip`'s
   spawnflag-`0x100` arm (both max words 1e9). The `Range` item-text key stops feeding any
   range compare (leave it read, unused, with a comment naming the unrecovered meaning).
2. Slot 365 `0x1024f670` as ONE ordered first-match body on the weapon (or the NPC's weapon seam),
   called from the port's `GatherAttackConditions` site with retail's `(enemy, dot, d)` — `d`
   measured as retail does (`0x10270890`; read it) — raising exactly ONE condition; strict
   compares; NaN passes to `0x4f`; the attack-timer `+0x730` gate last.
3. The three readers (`0x102b6b50` radius, `0x10296c40` band, the StartTask radius sites the
   findings name) read the real word; delete the 1024 stand-in constants that stood for it.
4. Units: the words are Source units; the port compares in whatever unit the site already uses —
   convert once at the read (`ElysiumMove::U`).
