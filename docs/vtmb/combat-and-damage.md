# Combat and damage

VtMB keeps Source's attacker/inflictor/weapon damage envelope, but routes RPG damage through a
Troika-specific `CVDmg_t` descriptor. Attack code establishes lethality and attack-specific
defense; `CVDmg_t::Apply` performs the common damage-roll/soak/filter stage; the combat
character then applies blood shield, health counters and aggravated tracking.

This document owns that damage pipeline. Feat construction and the consumer-specific check
policies are in `docs/vtmb/skills-and-checks.md`; the die algorithm is in
`docs/recovered/dice-system.md`; command and action layers are in
`docs/vtmb/gameplay-verbs.md`; animation selection is in
`docs/vtmb/animation_and_movers.md`.

## Evidence boundary

- Patch-first `vdata/items/*.txt`, `vdata/system/feats.txt`, `rules.txt` and
  `npctemplate*.txt` establish authored damage, weapon and character-template inputs.
- Hash-pinned `vampire.dll` (SHA-256
  `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`) establishes the
  parser, callback registration, attack paths, soak resolver and health commit.
- `research/cases/core-mechanics/` preserves the address set and open joins.
- Valve's public Source SDK supplies only the inherited frame of reference:
  [`CTakeDamageInfo`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/takedamageinfo.h)
  carries the inflictor, attacker, weapon, scalar damage and damage type, and
  [`CBaseCombatCharacter`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/basecombatcharacter.cpp)
  consumes it. VtMB's `CVDmg_t`, feat rolls and health counters are fork-specific findings,
  not behavior inferred from the SDK.

These are offline binary/data findings. The shared damage and ranged paths are closed far
enough to implement; the exact melee commit and several special filters remain explicitly open.

## Authored weapon inputs

Each weapon mode loads two adjacent integer fields:

| Key | Confirmed runtime role |
|---|---|
| `BaseLethality` | read by the weapon's base-lethality accessor and included in total lethality |
| `SkillRequirement` | loaded beside it; exact runtime consumer remains open |

The mode's `Dmg` string is parsed into `CVDmg_t`. Its grammar is:

```text
[optional source trait] <base damage integer> <damage family>
    [source DMG flags] [attack feat/reference]
```

Representative patch-first values:

| Item | `Dmg` |
|---|---|
| fists | `2 Bashing Close_Combat_Brawl DMG_FIST` |
| baton | `2 Bashing Close_Combat_Melee DMG_CLUB` |
| katana | `3 Lethal Close_Combat_Melee DMG_SLASH` |
| Glock 17c | `2 Lethal Ranged_Combat DMG_BULLET` |
| Colt Anaconda | `3 Lethal Ranged_Combat DMG_BULLET` |
| holy light | `Strength 2 Lethal Close_Combat_Melee DMG_FAITH` |

The optional leading trait is stored as a `CVStatRef` and applied by `SetSrc`. The trailing
feat/reference identifies the attack feat used to adjust lethality and supply associated
automatic-success metadata.

## `CVDmg_t`: the 17-word damage descriptor

The structure is `0x44` bytes (`17 * 4`). Names below are semantic working names, not debug
symbols from the retail build.

| Word | Offset | Meaning | Confidence |
|---:|---:|---|---|
| 0 | `0x00` | damage family: `-1` none, `0` bashing, `1` lethal, `2` aggravated | confirmed |
| 1 | `0x04` | authored/base damage after optional source-trait setup | confirmed |
| 2 | `0x08` | applied/final damage; negative means no damage | confirmed |
| 3 | `0x0c` | extra roll input or direct damage input, depending on resolver flags | confirmed shape; producer semantics incomplete |
| 4 | `0x10` | Source `DMG_*` bit mask | confirmed |
| 5..8 | `0x14..0x20` | optional source `CVStatRef` | confirmed |
| 9..12 | `0x24..0x30` | attack feat/reference | confirmed |
| 13 | `0x34` | source entity handle | confirmed |
| 14 | `0x38` | forced soak value; negative selects the normal soak resolver | confirmed |
| 15 | `0x3c` | accumulated damage-filter/multiplier field | partial; final consumer join open |
| 16 | `0x40` | resolver flags; bit `0x8` selects direct damage input instead of a damage roll | confirmed |

`GetDmg` returns word 2 when it is positive, word 1 when word 2 is zero, and zero when word 2
is negative. Startup installs the game-specific `CVDmg_t::Apply` callback at `0x1022F640`.
The installed generic `EvadeCheck` callback returns zero; actual defense is in ranged/melee
attack code rather than this descriptor.

### Damage-family flags

The parser maps these strings to Source damage bits:

| Token | Value | Token | Value |
|---|---:|---|---:|
| `DMG_BULLET` | `0x00000002` | `DMG_SLASH` | `0x00000004` |
| `DMG_BURN` | `0x00000008` | `DMG_BLAST` | `0x00000040` |
| `DMG_CLUB` | `0x00000080` | `DMG_BUCKSHOT` | `0x04000000` |
| `DMG_SUPERCLAWBITE` | `0x08000000` | `DMG_CLAWBITE` | `0x10000000` |
| `DMG_SUNLIGHT` | `0x40000000` | `DMG_FAITH` | `0x80000000` |

`DMG_FIST` is authored but is not present in the recovered string-to-bit table. Its fallback
or alias behavior remains open.

## The common `CVDmg_t::Apply` path

The installed apply callback receives the descriptor, attacker/source handle and victim. Its
confirmed order is:

1. Reject a null/no-family descriptor.
2. For a Kindred victim, convert lethal firearm damage to bashing unless the attacker's active
   weapon template has `Disallow_FirearmsToBashing`.
3. Unless resolver flag `0x8` is set, roll a damage pool at difficulty 6. The pool starts with
   word 1 and adds `max(word3 - 1, 0)`. If the descriptor's source trait is Strength, add the
   attacker's current `Automatic_Str_Successes` after the roll.
4. With resolver flag `0x8`, use word 3 as the damage-success input instead of rolling. This flag
   bypasses the **damage roll**, not the later soak test.
5. Preserve the retail non-botch floor: when the nonnegative net is zero but the roll-result
   discriminator is nonzero, promote the damage-success count to one.
6. Read the defender's `Automatic_Soak_Successes`.
7. Unless the Source damage mask intersects `0xC8000008`, obtain forced soak or roll the selected
   soak feat. Add automatic soak and cap total soak to the damage-success count.
8. Compute `remaining = max(damage_successes - soak, 0)`.
9. Apply template-specific damage filtering and later special-immunity/relationship policy.
10. Store the final applied result in descriptor word 2.

Step 9 is not fully named. The body demonstrably reads
`DamageFilterBashing`, `DamageFilterLethal`, `DamageFilterAggravated` and the flame filter from
the victim's NPC template and accumulates them into descriptor word 15. It also performs
damage-flag immunities and an attacker/victim relationship percentage. The exact point where
word 15 becomes committed health damage is still missing, so the filters must not yet be
implemented as a guessed direct multiply.

## Soak selection

The common soak resolver chooses an id from damage family, mortal/Kindred state and the
falling flag:

| Damage | Mortal | Kindred |
|---|---|---|
| Bashing | `Soak_vs_Bashing` (13) | `Soak_vs_Bashing_Kindred` (17) |
| Lethal | `Soak_vs_Lethal` (14) | `Soak_vs_Lethal_Kindred` (18) |
| Lethal + resolver flag `0x20` | `Soak_vs_Lethal_Falling` (15) | `Soak_vs_Lethal_Falling_Kindred` (19) |
| Aggravated | `Soak_vs_Aggravated` (16) | `Soak_vs_Aggravated_Kindred` (20) |

The raw selector adds one for flag `0x20`; the table above records the observed intended
lethal/falling use, not a claim that every family-plus-flag combination is meaningful. The
selected feat is rolled at player difficulty 3 or NPC difficulty 7 and returns
`max(successes - botches, 0)`. Automatic soak is added by the apply callback, outside this
resolver.

## Ranged damage

The per-victim ranged body (`0x10268330`) is reached by a volley path that groups traces by
victim and passes the victim's hit share, plus a separate stored-projectile/impact path. It:

1. Copies the active weapon mode's `CVDmg_t` and applies the optional source trait.
2. Computes `total_lethality = max(BaseLethality + attacker_adjustment, 0)`. The adjustment
   comes from applying the descriptor's attack feat/reference to the attacker; a developer
   override can replace it.
3. Rounds total lethality and clamps the ranged path to at least one.
4. Only for a Kindred victim, rolls `Defensive_Maneuvers` at the PC/NPC defense difficulty and
   subtracts net successes, floored at zero.
5. Marks the descriptor's direct-damage flag and carries the shot through trace/hit processing.
6. Computes the direct attack value described by the retail debug string as:

```text
remaining lethality * BaseDamage * Multiplier
```

`BaseDamage` is the leading integer in `Dmg`. The multiplier is accumulated by the trace path;
the volley hit share enters `CTakeDamageInfo` before `TraceAttack`, and hitgroup/impact policy
may modify it further. Its exact decomposition is an open join. The resulting direct value then
enters the common descriptor/soak/health route.

## Melee damage

Melee is recovered through the defense/soak staging point, not yet through final damage commit.
`CBaseCombatCharacter::CalcAndStore...` appends a 16-byte record to the defender's result array
at `+0xA88`, whose active count is at `+0xA94`, and stores:

| Word | Stored value |
|---:|---|
| 0 | attacker/entity handle |
| 1 | active weapon's total lethality |
| 2 | defender `Defensive_Maneuvers` net successes, plus any bounded defense bonus |
| 3 | defender soak successes selected from the weapon's active `CVDmg_t` |

The record is keyed by attacker. `CBaseCombatCharacter::GetMeleeDiceRolls` searches the array and
returns the matching record; `GetNumAttackSuccesses` returns its word 1. Custom NPC `RunTask`
handlers use the record and the five-way classifier at `0x103498B0` to decide attack-task state,
so this result has reaction/AI consumers as well as a prospective damage consumer.

`MeleeRollA` derives defender reaction booleans and calls the defender's melee-reaction virtual.
`rules.txt` supplies the reaction margins:

| Reaction boundary | Margin `<=` |
|---|---:|
| attacker blocked major | -3 |
| attacker blocked | -1 |
| defender dodge attack | -3 |
| defender dodge | -1 |
| defender block | 1 |
| defender block stagger | 4 |
| defender hit/knockback | greater than 4 |

These are reaction thresholds, not a completed damage formula. The record lookup and reaction
consumers are now identified; the downstream path that turns the stored lethality, defense and
soak values into `CVDmg_t` and committed health damage remains the highest-priority combat gap.

## Health commit

`CBaseCombatCharacter::OnTakeDamage` dispatches by life state. The alive path first runs a
virtual prefilter, then gets the actual positive damage either from the custom descriptor apply
callback or from the scalar fallback. It commits in this order:

1. `HealthBuffer` (attribute slot 25) absorbs damage first. Partial absorption reduces it;
   exhausting it clears the counter and ends `Thaumaturgy_Bloodshield`.
2. If damage remains and the character is marked unkillable, the **Health damage counter** is
   capped at the literal value 75. With the default `Max_Health=100`, this leaves 25 health; it
   is not a general one-hit-point floor or a percentage calculation.
3. Remaining damage is added to `Health` (attribute slot 15), which is damage taken rather than
   health remaining.
4. For a Kindred victim, attacks whose Source mask intersects `0xC8000008` add the same amount to
   `Health_Aggravated_Dmg` (slot 16).
5. `HealthToPercent` projects `(Max_Health - Health) / Max_Health` back into Source
   `m_iHealth`, then downstream death/frenzy/reaction logic observes the result.

The exact identity and mutation rights of the alive-path prefilter, and some float-to-integer
rounding points around scalar damage, remain open.

## Faithful implementation seams

The baseline requires distinct types and stages:

- an authored weapon-mode record (`BaseLethality`, `SkillRequirement`, `Dmg`);
- a parsed `CVDmg_t`-equivalent value object;
- attack-specific ranged/melee hit and defense policy;
- one shared dice/soak/filter resolver;
- a typed health commit that knows bashing/lethal/aggravated, blood shield and unkillable;
- an audit result that exposes every intermediate value for retail comparison.

A scalar `TakeDamage(float)` cannot express this contract. Project implementation status and
the known scalar-path divergence are tracked by roadmap 13.3 and RE40 rather than in this VtMB
fact document.

## Open research gaps

- Locate the melee record's downstream **damage** consumer and close the exact
  attack-margin/damage formula; record lookup and reaction/AI consumers are already identified.
- Identify the complete `SkillRequirement` consumer and its relation, if any, to the attacker
  adjustment.
- Decompose the ranged multiplier into volley share, hitgroup and other trace modifiers.
- Join descriptor word 15 and item `DmgModifier` to the exact post-soak numerical operation.
- Name the remaining special immunity, relationship and secondary-effect predicates in
  `CVDmg_t::Apply`.
- Resolve `DMG_FIST` fallback/alias behavior.
- Name the alive-path prefilter and confirm every scalar-to-integer rounding boundary.
- Capture one retail ranged and one melee hit with printed roll diagnostics to validate the
  offline formulas and attack-state timing end to end.
