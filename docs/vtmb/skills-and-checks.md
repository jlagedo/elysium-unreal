# Skills, feats and runtime checks

VtMB does not have one universal "skill check" operation. It has a shared character-sheet
and feat-rating layer, then each consumer chooses whether to compare that integer directly,
roll it as a dice pool, seed automatic successes, or combine it with another character's
result. This document owns that **consumer policy** and the boundary between rating and roll.

The storage model, trait effects and exact `Feats::FeatValue` algorithm remain canonical in
`docs/vtmb/game_runtime.md` section 3. The d10 algorithm, result struct and tiering remain
canonical in `docs/recovered/dice-system.md`. Combat damage after a check is owned by
`docs/vtmb/combat-and-damage.md`.

## Evidence boundary

- Patch-first `vdata/system/stats.txt`, `feats.txt`, `DiceRolls.txt` and `rules.txt` establish
  the authored traits, derived feats, weighting tables and difficulties.
- Hash-pinned `vampire.dll` (SHA-256
  `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`) establishes the
  rating evaluator and the consumer-specific call paths.
- Dialogue and map-sidecar corpora show how those consumers are authored.
- `research/cases/core-mechanics/` preserves the addresses, questions and rerun commands.

No live retail run was needed for the findings below. They are offline binary/data findings;
timing, presentation and any branch still called out as open require a targeted runtime probe.

## The layers

| Layer | Value | Owner | Important rule |
|---|---|---|---|
| Base trait | saved integer before effects | character sheet | Writes change persistent progression or damage counters. |
| Current trait | base after effects and bounds | character sheet | Runtime consumers read this value. |
| Feat rating | integer derived from current traits | `Feats::FeatValue` | `CalcFeat` returns this integer; it does **not** roll. |
| Roll | successes, botches, net and tier | dice resolver | The caller supplies pool, difficulty, weighting and automatic successes. |
| Consumer verdict | gate, success/fail/botch, margin or damage | dialogue/entity/combat/feed code | Thresholds and opposed logic are consumer-owned. |

That separation is load-bearing. A dialogue line asking for `Persuasion 7` and a lock asking
for `Intrusion` may read the same feat system, but only the lock invokes the dice resolver.

## The 23 shipped feats

Ids are `feats.txt` file order. Each base is a `CVStatRef` read as the current, effect-modified
trait value. The complete evaluator, including attribute floors, per-feat modifiers and the
feat-level effect pass, is in `docs/vtmb/game_runtime.md`.

| Id | Internal name | Authored bases | Normal role |
|---:|---|---|---|
| 0 | `Intrusion` | Dexterity + Security | lockpicking |
| 1 | `Sneaking` | Dexterity + Stealth | stealth |
| 2 | `Hacking` | Wits + Computer | terminals and electronic locks |
| 3 | `Inspection` | Perception + Investigation | perception/search |
| 4 | `Research` | Academics + Intelligence | research |
| 5 | `Haggle` | Finance + Manipulation | economy |
| 6 | `Intimidate` | Intelligence + Intimidation | dialogue |
| 7 | `Persuasion` | Charisma + Academics | dialogue |
| 8 | `Seduction` | Appearance + Subterfuge | dialogue |
| 9 | `Close_Combat_Brawl` | Brawl + Strength | unarmed attacks and resisted feeding |
| 10 | `Close_Combat_Melee` | Melee + Strength | melee attacks |
| 11 | `Ranged_Combat` | Firearms + Perception | ranged attacks |
| 12 | `Defensive_Maneuvers` | Dodge + Wits | combat defense |
| 13 | `Soak_vs_Bashing` | Armor + Stamina + Soak pool | mortal bashing soak |
| 14 | `Soak_vs_Lethal` | Armor + Soak pool | mortal lethal soak |
| 15 | `Soak_vs_Lethal_Falling` | half Armor + Soak pool | mortal falling soak |
| 16 | `Soak_vs_Aggravated` | Soak pool | mortal aggravated soak |
| 17 | `Soak_vs_Bashing_Kindred` | Armor + Stamina + Soak pool | Kindred bashing soak |
| 18 | `Soak_vs_Lethal_Kindred` | Armor + Soak pool | Kindred lethal soak |
| 19 | `Soak_vs_Lethal_Falling_Kindred` | half Armor + Soak pool + Stamina | Kindred falling soak |
| 20 | `Soak_vs_Aggravated_Kindred` | Soak pool | Kindred aggravated soak |
| 21 | `Damage` | none | weapon/code supplied |
| 22 | `Frenzy_Feat` | none | frenzy/code supplied |

`Close_Combat_Brawl` and `Close_Combat_Melee` also name `Automatic_Str_Successes`; the eight
soak feats display `Automatic_Soak_Successes`. These values are not part of the rating sum.
Combat reads the current automatic-success traits separately: the common damage resolver reads
`Automatic_Str_Successes` when the descriptor's source trait is Strength, and reads
`Automatic_Soak_Successes` from the defender.

All shipped feats select the `Normal` weighting for PCs and NPCs. The indirection is still
real: a modified `DiceRolls.txt` may replace the uniform table without changing the feat.

## Consumer matrix

| Consumer | Input | Runtime operation | Verdict |
|---|---|---|---|
| `Character.CalcFeat` | feat name | evaluate rating only | returns integer; no RNG |
| dialogue simple dependency | feat or raw trait + authored threshold | rating/current-trait comparison | boolean; `M_`/`F_` is a sex gate |
| lockable entity, `skilltype=1` | `Intrusion`, authored difficulty | repeated dice roll | `>2` success, `0` botch, `1..2` fail |
| lockable entity, `skilltype=2` | `Hacking`, authored difficulty | repeated dice roll | same thresholds |
| resisted feeding | attacker Brawl rating; victim Hacking roll at difficulty 6 | hybrid opposed check | attacker rating must be strictly greater than defender net successes |
| ranged defense against a Kindred victim | victim `Defensive_Maneuvers`; PC/NPC defense difficulty | dice roll | net successes subtract from lethality, floored at zero |
| melee defense | victim `Defensive_Maneuvers`; PC/NPC defense difficulty | dice roll | net successes stored with the attack record |
| soak | damage-type/creature-specific soak feat; PC/NPC soak difficulty | dice roll + automatic soak | net successes reduce damage successes, never below zero |

### Dialogue is a threshold, not a roll

`CDialogDependency::TestSimple` reads the feat rating or current raw trait and compares it to
the authored integer. `Persuasion 7` therefore means `CalcFeat("Persuasion") >= 7`; no die is
rolled and no botch is possible. `M_` and `F_` prefixes first reject the wrong player sex, then
evaluate the underlying check name.

### Lock and hacking props perform repeated rolls

`CBaseVampireSkillEntity` maps `skilltype=1` to feat id 0 (`Intrusion`) and `skilltype=2` to
feat id 2 (`Hacking`). Other values do not select a roll. Each attempt records its roll and
fires one of three outputs:

```text
roll > 2  -> OnSkillSuccess
roll == 0 -> OnSkillBotch
otherwise -> OnSkillFail
```

The same stored value is the lock state: `<3` is locked; `Lock` writes 1 and `Unlock` writes
3. Attempt cadence is `(K1 - rating*K2) / player_scale`, so rating influences both the roll
pool and the presentation pace. Full entity fields and outputs are in
`docs/vtmb/entity_io.md`.

### Feeding is deliberately asymmetric

`CBasePlayer`'s feed-attempt body first evaluates target eligibility. Several target activity
states and non-resisting targets succeed without a roll. On the resistance branch it compares:

```text
attacker Close_Combat_Brawl rating
    > max(victim Hacking roll successes - botches, 0), difficulty 6
```

The use of the victim's `Hacking` feat is what the pinned binary does; it must not be renamed
to a more intuitive trait without evidence. A separate target predicate can still authorize a
stealth-kill/feed result after the opposed check fails. The exact activity list, target
predicate identities and transition side effects remain open.

### Combat rolls defense and soak separately

`rules.txt` supplies different targets for the player and NPCs:

| Roll | Player difficulty | NPC difficulty |
|---|---:|---:|
| Defense | 3 | 7 |
| Soak | 3 | 7 |

Defense is attack-path-specific. The generic `CVDmg_t::EvadeCheck` callback installed at
startup returns zero, while ranged and melee code explicitly roll `Defensive_Maneuvers`.
Soak is selected later from damage family and mortal/Kindred state. See
`docs/vtmb/combat-and-damage.md` for ordering and formulas.

## Runtime contract for a faithful implementation

A check API must keep these operations explicit rather than hiding them behind one function:

1. Resolve a current trait or a feat definition.
2. Evaluate the integer rating.
3. Select PC/NPC weighting and human difficulty.
4. Add consumer-owned automatic successes, if any.
5. Roll only when that consumer requires a roll.
6. Return the whole result (`successes`, `botches`, `net`, tier), not only a boolean.
7. Let the consumer apply its own threshold, opposed comparison, output and pacing policy.

This also means `CalcFeat` is not the dice resolver. Code that calls it and immediately treats
the integer as a random outcome silently changes dialogue, locks, feeding and combat in four
different ways.

## Open research gaps

- The `prop_hacking` terminal difficulty/skill-attempt route is not yet joined to its feat,
  retry and lockout policy; it remains part of RE39.
- The exact feed activity codes, the "can resist" predicate and the stealth override predicate
  need names and a live transition capture.
- The melee attack record is known, but the later consumer that turns its lethality, defense and
  soak fields into committed damage remains unresolved.
- `SkillRequirement` is loaded for weapon modes but its exact effect on attack lethality or use
  eligibility has not been located. `BaseLethality` is a distinct field and is confirmed in the
  total-lethality accessor.
- Combat-specific trait override objects appear in the binary but are not initialized by the
  shipped image; their external owner, if any, remains unidentified.
