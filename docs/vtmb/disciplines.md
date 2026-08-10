# Disciplines — vampire powers

This document owns VtMB's supernatural player-power behavior: the thirteen compiled Discipline
slots, player selection and cast authority, blood payment, targeted effects, renewable active
states, timed teardown, and the gameplay contribution of each level. Character-sheet storage and
generic trait-effect arithmetic remain in `docs/vtmb/game_runtime.md`; combat damage remains in
`docs/vtmb/combat-and-damage.md`; the input vocabulary remains in `docs/vtmb/controls.md`.

In VtMB terminology these are **Disciplines**, not skills. Skills such as Firearms, Melee,
Security and Computer live in the Abilities container and feed feats/checks. Disciplines are a
separate sheet container whose values select supernatural powers and whose active values modify
the sheet, AI, rendering, movement, combat or a target-effect interpreter.

## Evidence boundary

The native findings below come from static analysis of the hash-pinned `vampire.dll` and
`client.dll` recorded by `research/cases/disciplines/`. The authored catalog comes from the current
**patch-first** local `vdata/system/stats.txt`, `traiteffects000.txt` and
`disciplinetgt_000.txt`…`004.txt`. Those files contain explicit Wesp/Unofficial Patch changes, so a
number or description from them is faithful to the user's installed corpus, not proof that an
unpatched 1.2 file contained the same value.

Labels used here:

- **[DLL]** — recovered from native code in the pinned executable;
- **[data]** — read from the current patch-first authored corpus;
- **[help]** — user-facing intent text in `stats.txt`; useful, but weaker than an executing path;
- **[inference]** — the join is strong but one native caller or retail observation remains open;
- **[open]** — not yet established.

No live retail cast was captured for this pass. Timings, fields and branches are statically
recovered; exact presentation and edge-order acceptance still require controlled retail capture.

## The system in one page

The player has two parallel thirteen-slot arrays **[DLL]**:

| Index | Learned Discipline | Active slot |
|---:|---|---|
| 0 | Animalism | `Active_Animalism` |
| 1 | Auspex | `Active_Auspex` |
| 2 | Blood Healing | `Active_Blood_Healing` |
| 3 | Celerity | `Active_Celerity` |
| 4 | Corpus Vampirus / Bloodbuff | `Active_Corpus_Vampirus` |
| 5 | Dementation | `Active_Dementation` |
| 6 | Dominate | `Active_Dominate` |
| 7 | Fortitude | `Active_Fortitude` |
| 8 | Obfuscate | `Active_Obfuscate` |
| 9 | Potence | `Active_Potence` |
| 10 | Presence | `Active_Presence` |
| 11 | Protean | `Active_Protean` |
| 12 | Thaumaturgy | `Active_Thaumaturgy` |

A learned value of `-1` means unavailable; `0`+ means owned/rated. An active value of `0` means
off. `stats.txt` authors four later Numina entries (`Shield_of_Faith`, `Divine_Vision`,
`Holy_Light`, `Mind_Shield`), but the compiled player/save arrays stop after Thaumaturgy. They are
not four hidden slots in the ordinary vampire Discipline array.

The engine then splits the thirteen powers into two execution families **[DLL + data]**:

| Family | Disciplines | Authority |
|---|---|---|
| Targeted/non-instant | Animalism, Dementation, Dominate, Thaumaturgy | Select a `DisciplineTgt` record, resolve targets and apply its hit graph |
| Native instant/passive state | Auspex, Blood Healing, Celerity, Bloodbuff, Fortitude, Obfuscate, Potence, Presence, Protean | Change an `Active_Disciplines` value, run its authored actions and schedule expiry |
| Hybrid use of target data | Presence | Native 16-second active state; level-specific `DisciplineTgt` records describe the surrounding AoE effect **[inference: exact pulse caller open]** |

This is **not a combo system**. Each player request resolves independently. A targeted record may
use `Trigger_Casting` to invoke helper records — Blood Strike return, Blood Theft/Bloodrip, Blood
Boil inner/outer blasts, or the generic `Use_Spell` cast — but those are one power's internal
effect graph. There is no recovered player input buffer, ordered Discipline combo, or
Discipline-to-Discipline chain bonus.

## Who receives which Disciplines

Blood Healing and Bloodbuff are common vampire powers. The seven player clans each author a trio
of clan Disciplines **[data]**:

| Clan | Discipline trio |
|---|---|
| Brujah | Celerity, Potence, Presence |
| Gangrel | Animalism, Fortitude, Protean |
| Malkavian | Auspex, Dementation, Obfuscate |
| Nosferatu | Animalism, Obfuscate, Potence |
| Toreador | Auspex, Celerity, Presence |
| Tremere | Auspex, Dominate, Thaumaturgy |
| Ventrue | Dominate, Fortitude, Presence |

Clan ownership also affects dialogue and XP/chargen policy, but it does not create a second cast
engine. Once a rank exists, the common Discipline authority consumes it.

## Selection and cast authority

### Client selection

The quickbar entry type `0` is a Discipline **[DLL]**. The client walks the compiled Discipline
order above, filters to learned/available entries, converts the visible quickbar ordinal back to
the actual slot index, and sends:

```text
vdiscipline_int <compiled-index>
```

`vhotkey_int` is the separate quickbar-selection command. The exact upper-tier selection handoff
and the community-reported one-frame `vhotkey` deferral remain open; they are not prerequisites for
the server authority below.

### Server selection

`vdiscipline_int` stores the selected compiled index on the player **[DLL]**. A newly selected
index resets the remembered tier to zero; reselecting the same index preserves the remembered
tier. It rejects an index whose current learned value is below one. Obfuscate also crosses a
native eligibility predicate before use.

With the selection valid, the server calls the shared authority with `(player, discipline,
tier)`. `vdiscipline_last` performs only that last step with the remembered pair. Thus
`vdiscipline_last` does not own a second cast implementation.

### Authoritative split

The shared authority reads the Discipline's compiled `stats.txt` flags **[DLL]**:

1. `Is_Instant == 0` enters the targeted `DisciplineTgt` manager.
2. `Is_Instant == 1` enters the native active-stat/event path.
3. An already-active renewable is renewed or toggled through its existing timed event rather than
   creating an unrelated effect owner.
4. Failed blood, rank, predependency or target checks issue player feedback and do not commit the
   cast.

The misleading name `Is_Instant` should not be turned into a remake design rule. Auspex,
Celerity, Fortitude, Obfuscate, Potence, Presence and Protean all set it while producing timed
states. In this binary it is principally the native-path discriminator.

## Native active-state path

Every native power has an `Active_*` stat. Its authored block can supply:

- increment predependencies;
- an initial duration and renewal extension per level;
- blood-pool mutations;
- trait-effect groups;
- particles and screen effects;
- a named native function such as `BloodHealFunc`.

The server derives duration from the active-stat table, applies the Discipline-duration modifier,
adds a timed event to the shared event queue, and records begin/end times in per-Discipline player
arrays **[DLL]**. The event applies and later removes the active-stat actions. Renewable use works
against that owned event rather than stacking anonymous copies.

All thirteen `Active_*` values have explicit datamap fields. The base array begins at player
`+0x1310`; the current array begins at `+0x1354`, not `+0x1344`. Native save state also includes
per-Discipline timers, a cast counter, Obfuscate rule state and Protean transform state.

`vdiscipline_endall` and the entity input `ClearActiveDisciplines` converge on one teardown
wrapper **[DLL]**. It removes the character's owned Discipline timed events — invoking their normal
removal callbacks — and clears every currently tracked targeted effect. It is not a blind visual
reset.

### Native/self powers

| Discipline | Cost and duration in the active block | What it contributes | Recovered resolution |
|---|---|---|---|
| Auspex | 1 blood; 20/24/28/32/36 s | Aura sight, dark vision; Wits `+1/+1/+2/+2/+3`, Perception `+0/+1/+1/+2/+3`; authored aura radii `200/324/578/872/1200` | Generic active stat applies the level trait effect; native perception/render consumers remain to be closed |
| Blood Healing | Blood must be nonzero; 5 s nonrenewable active window | Reconstitutes damage; `VampHeal_Type` receives `Duration 8%` | Authored `BloodHealFunc` owns the transaction **[open: exact blood/health steps]** |
| Celerity | 1 blood; 14 s at every level | Preternatural speed/world slowdown; `Fx_Motion_Trail +rank` | Active stat and timer are closed; native time/movement consumer remains open |
| Bloodbuff (`Corpus_Vampirus`) | 3 blood; 14 s | `+2` Strength, Dexterity and Stamina, with each maximum raised to 10 | Generic trait-effect layer; normal player activation uses active value 1 |
| Fortitude | 1 blood; 25 s | `+rank` automatic soak successes; Fortitude FX | Generic trait effect feeds the common soak resolver; help excludes fire/heat, whose exact native mask remains to be rejoined here |
| Obfuscate | 1 blood; 18/20/22/24/26 s | Invisibility/stealth-kill rules; translucency `10/30/50/70/90` | Native visibility and break-rule state plus trait FX; exact break matrix is partly open |
| Potence | 1 blood; 25 s | Supernatural brawl/melee force; authored Strength `+rank` | Melee damage additionally floors remaining `DamageInflicted` up to active Potence rank before final scalar commit; that extra damage is not defended |
| Presence | 1 blood; 16 s | Nearby enemy penalties, attack-rate reduction and level-dependent mesmerize chance | Native active/reaction effects plus level-specific AoE target records **[inference: pulse cadence/caller open]** |
| Protean | 1 blood; 25 s | Heat vision, claws, physical buffs and war form | Level trait effect swaps equipment/stats/model; saved transform handle/time support the native form transition **[open: full transition lifecycle]** |

The common active preconditions require positive blood and a living character. Obfuscate also
requires `ObfuscateCanInc == 1` and disallows Protean war form; Protean disallows active
Obfuscate. These are authored increment gates, not UI-only warnings.

### Protean's cumulative level payload

Each exact-level trait group repeats lower-level benefits where needed **[data]**:

| Level | Payload |
|---:|---|
| 1 — Gleam of Red Eyes | Wits `+1`; heat-vision intent |
| 2 — Feral Claws | Wits `+1`; exclude ordinary equipment form; remove fists and grant `item_w_claws` |
| 3 — Will of the Wolf | Level 2 payload plus Stamina `+2` |
| 4 — Hunter of Night | Level 3 payload plus Strength `+4` |
| 5 — War Form | Level 4 payload plus `Close_Combat_Brawl +8` and `Fx_Model_Wolf +1` |

The data contains Toreador-specific alternate equipment-form names even though Toreador does not
normally own Protean. That is data accommodation, not evidence of a standard Toreador power.

### Obfuscate's authored level policy

The help text and active data agree on the broad progression **[data + help]**:

| Level | Allowed behavior and break benefit |
|---:|---|
| 1 — Hide | Motionless only; movement/action reveals and starts the timer |
| 2 — Limited Invisibility | Crouched movement; touching actors/containers or interaction reveals |
| 3 — Hidden Killer | Crouched movement; a breaking melee/unarmed attack receives a 50% bonus |
| 4 — Advanced Invisibility | Free movement; interaction/touch reveals; breaking attack deals double damage |
| 5 — Unseen Force | Free movement and world interaction; touching actors reveals; use-key stealth kills may preserve invisibility; breaking attack deals triple damage |

Only the duration gates, active slot, translucency effects and separate native fields are closed in
this pass. The precise line-by-line native consumer for movement, bump, interaction, stealth kill
and break-damage multiplication remains **[open]**.

## Targeted `DisciplineTgt` path

### Transaction

For Animalism, Dementation, Dominate and Thaumaturgy, the authority finds the record matching the
compiled Discipline index and selected tier **[DLL]**. It then:

1. reads the record's base `BloodCost` and applies `BloodCost` trait modifiers;
2. verifies the BloodPool and the learned Discipline;
3. builds the legal target set from the record's AoE definition;
4. aborts with feedback if no legal target remains;
5. deducts the adjusted blood cost once;
6. starts source-side effects and increments the player's Discipline cast counter;
7. applies the chosen hit mapping to every resolved target;
8. records the recovery/presentation state owned by the target-effect manager **[inference: exact
   cooldown ordering and restoration remain open]**.

The target query is data-driven. `AoE` supplies range, source (`Self` or `Target`), shape
(`Target`, `Radius`, `Cone`) and ordered filters. Filters include critter/human/supernatural,
boss/no-boss, self/friends, invulnerability, player/primary-target, character template and
`DisciplineStrata`. Ordered `Affects_Table` entries choose a `HitTable`; chance mappings may branch,
and a default mapping catches the remainder.

### Hit execution

A `HitInfo` may combine all of the following **[DLL + data]**:

- health and blood changes, including percentages and ranges;
- trait-effect groups and duration;
- NPC flags, AI schedules and expressions;
- a player compact animation, NPC gesture and gesture duration;
- flinch and knockback;
- target/source particles and sounds;
- a projectile with speed, model/particle and interruption payload;
- nested `Trigger_Casting` into another Discipline record.

Projectile records defer target hit execution until impact. Non-projectile records execute the
resolved target payload immediately. Active target effects are tracked by bits on the affected
character, so replacement and global clear remove the actual effect owner rather than merely
hiding its particle.

`ShouldRemove_OnTakeDamage`, `ShouldRemove_OnHearCombat` and `ShouldRemove_OnWasBumped` are
independent authored interruption flags. The current main powers use damage/bump interruption for
Nightwisp Ravens, Bloodsucker Communion, Hysteria, Trance, Brain Wipe (damage only), Sleep (bump
only) and Blood Theft.

### Animalism

| Level | Cost / recovery | Target shape | Gameplay result in the current corpus |
|---:|---|---|---|
| 1 — Nightwisp Ravens | 1 / 5.0 s | target, 600 | Incapacitates with ravens; damage or bump disperses it; covert record |
| 2 — Burrowing Beetle | 1 / 0.3 s | target, 600 | Projectile/direct body damage; overt and AI-audible |
| 3 — Spectral Wolf | 2 / 4.5 s | target, 600 | Spectral animal attack; overt and AI-audible |
| 4 — Bloodsucker Communion | 3 / 9.0 s | target, 600 | Bats drain target blood and launch a zero-cost return helper; damage or bump interrupts |
| 5 — Pestilence | 3 / 12.0 s | self-origin cone, 900 | Area flesh damage through strata-specific hit mappings |

Animalism records use `SupernaturalLvl 2` and extensive strata/template exceptions. A remake must
not reduce them to five unconditional damage spells.

### Dementation

All five user casts are covert in the current records.

| Level | Cost / recovery | Target shape | Gameplay result in the current corpus |
|---:|---|---|---|
| 1 — Hysteria | 1 / 6.0 s | target, 600 | Chance-selected laughter/crying incapacitation; damage or bump can end it |
| 2 — Mass Hallucination | 2 / 4.0 s | self radius | Area debuff; trait data applies `-2` Perception, Strength, Wits and Hacking, matching the authored combat/defense intent |
| 3 — Vision of Death | 2 / 4.5 s | target, 600 | Nightmare/heart-attack outcome with weaker supernatural mappings |
| 4 — Berserk Insanity | 3 / 10.0 s | target, 600 | Berserk AI schedule, hostile/random attack behavior and eventual fatal/lesser strata result |
| 5 — Voice of Bedlam | 4 / 12.0 s | target-origin radius, 900 | Randomly dispatches lower Dementation outcomes across the resolved set |

### Dominate

All five user casts are covert in the current records.

| Level | Cost / recovery | Target shape | Gameplay result in the current corpus |
|---:|---|---|---|
| 1 — Trance | 1 / 5.0 s | target, 600 | Temporary incapacitation; damage or bump wakes the target |
| 2 — Brain Wipe | 2 / 5.5 s | self radius | Nearby enemies lose/forget the player; damage removes the effect |
| 3 — Sleep | 2 / 4.5 s | target, 600 | Long incapacitating sleep; bump removes it |
| 4 — Possession | 3 / 10.0 s | target, 600 | Victim fights the player's enemies, then receives fatal/lesser strata outcome |
| 5 — Mass Suicide | 4 / 11.0 s | target-origin radius, 900 | Fatal episode across the target set, with supernatural/boss exceptions |

Possession applies the same `Fx_No_Resist_Feeding` trait channel used by Dementation Berserk while
its AI schedule is active.

### Thaumaturgy

All five main records are overt and AI-audible.

| Level | Cost / recovery | Target shape | Gameplay result in the current corpus |
|---:|---|---|---|
| 1 — Blood Strike | 1 / 0.2 s | projectile target, 700 | Projectile damage; a surviving target can launch a zero-cost return that brings stolen blood back |
| 2 — Blood Purge | 2 / 6.0 s | self radius | Damages/afflicts nearby enemies and drives vomiting; these are the only effective Discipline `Player_Anim` rows |
| 3 — Blood Shield | 3 / 12.0 s | self | Applies the Bloodshield trait and HealthBuffer; damage is absorbed until exhaustion clears the buffer and ends the effect |
| 4 — Blood Theft | 3 / 11.0 s | target, 600 | Drains blood through Bloodrip/return helpers and kills the ordinary target; damage or bump interrupts |
| 5 — Blood Boil | 4 / 4.5 s | target, 1100 | Fatal target boil followed by zero-cost inner/outer radius explosion helpers |

The helper records have zero blood cost and zero recovery. They are effect-graph nodes, not powers
the player purchases or casts as combo follow-ups.

### Presence's AoE records

Presence activates through the native renewable path for one blood and sixteen seconds. Its
separate target file describes self-centered radii `150/250/350/500/700`, level penalties and
mesmerize/gesture mappings. Level 1–2 records are covert; level 3–5 records are overt. The active
trait groups apply Strength/Wits/Perception `-rank`; level 4 also applies Stamina `-2`, and level 5
Stamina `-4`.

Two authored details must not be silently merged:

- `stats.txt` also contains `Presence_Effect_Radius` values `512/640/768/896/1024`;
- the Presence `DisciplineTgt` records carry BloodCost `1/1/2/3/3`, while the executing
  `Active_Presence` action deducts one blood at every level.

The native activation path and help text support the one-blood transaction. The exact consumer of
the second radius table and the exact Presence pulse caller remain **[open]**.

## Animation, AI reaction and presentation

There is no universal “play one cast animation, then apply the spell at montage end” rule.
Targeted records own a small effect graph **[DLL + data]**:

1. source instant/hit effects start particles, sounds or a nested `Use_Spell` helper;
2. `Use_Spell`/`Use_Spell_Alt` records select clan/form-sensitive cast gesture layers;
3. projectiles, when present, delay the target payload until impact;
4. target `HitInfo` can assign an AI schedule, gesture, flinch, knockback or compact player action;
5. duration and interruption belong to the timed effect, not to the visual animation length.

The patch-first corpus contains only two effective `Player_Anim` fields, both `PLAYER_VOMIT` in
Blood Purge. They enter the ordinary player action resolver, whose protected vomit state advances
`ACT_VOMIT_INTO → ACT_VOMIT_IDLE → ACT_VOMIT_GETOUT → ACT_IDLE`. NPC/target reactions more often
use authored `AI_Schedule` or `Gesture_Anim` values — Presence dazed, blood-theft pain, flinch and
cast layers — rather than the player compact-action table.

Discipline hit data can request flinch or knockback, and health damage can enter the common damage
reaction path. That is **not** the melee block/opposed-record system. No Discipline block action,
parry window or generic Discipline stagger meter was recovered.

## Overt use, AI sound and the Masquerade

The target-record parser stores `Overt` and `TriggerAISound` independently **[DLL]**. The authored
files use them coherently — Dementation and Dominate are covert; most Animalism and all main
Thaumaturgy effects are overt; some overt casts also emit an AI sound. Help text says overt powers
risk a Masquerade violation in Safe Areas.

This pass does **not** yet close the native predicate that combines overt use, zone state,
witnesses and Masquerade mutation. Therefore:

- `Overt == 1` is a confirmed authored classification;
- AI sound generation is a separate confirmed switch;
- “every overt use immediately subtracts one Masquerade point” is **not** established and must not
  be implemented from the help text alone.

## Faithful remake contract

A faithful implementation needs these separations:

1. learned Discipline rank and active Discipline value are different sheet state;
2. selection, target acquisition, cast acceptance and effect execution are observable stages;
3. blood is committed only by the authoritative accepted transaction;
4. native renewable effects are owned timed events with normal renewal/removal callbacks;
5. targeted powers are interpreted from ordered filters/mappings, not hard-coded by display name;
6. projectiles, immediate hits and nested helper casts retain distinct timing;
7. AI schedules, trait effects, damage, blood transfer, animations and presentation are independent
   HitInfo channels;
8. interruption reasons remain explicit;
9. `ClearActiveDisciplines` removes owned effects and events, including their gameplay modifiers;
10. no player-facing Discipline combo layer is added unless later retail evidence proves one.

## Open verification work

- close Blood Healing's exact blood/health transaction and pulse timing;
- recover Celerity's native time/movement consumer and level curve;
- recover the complete Obfuscate visibility/break/damage-bonus matrix;
- recover Protean's transform start/finish, equipment and teardown lifecycle;
- identify Presence's native pulse caller and reconcile its two radius tables;
- close recovery/cooldown save/restore and interruption ordering for targeted effects;
- close overt + witness + zone → Masquerade policy;
- confirm upper-tier UI selection and `vhotkey` frame deferral;
- run controlled retail casts for all thirteen powers, including failure, renewal, interruption,
  save/load and `ClearActiveDisciplines` cases.

## Static anchors

| Function | Role |
|---|---|
| `vampire.dll 0x100D8490` | `vdiscipline_int` selection/dispatch |
| `vampire.dll 0x100D9750` | `vdiscipline_last` dispatch |
| `vampire.dll 0x100DB250` | `vdiscipline_endall` |
| `vampire.dll 0x100D8830` | shared Discipline-use authority |
| `vampire.dll 0x100D9220` | native duration/event scheduling |
| `vampire.dll 0x101E2DA0` | targeted record, blood and learned-rank gate |
| `vampire.dll 0x101E2F50` | target-set commit and one-time blood payment |
| `vampire.dll 0x101E06A0` | `DisciplineTgt` record parser |
| `vampire.dll 0x101DDFB0` | `HitInfo` parser |
| `vampire.dll 0x101DE660` | `HitInfo` executor |
| `vampire.dll 0x101E33C0` | source + target cast execution |
| `vampire.dll 0x100CF420` | owned Discipline timed-event removal |
| `vampire.dll 0x101E3C80` | active targeted-effect removal |
| `vampire.dll 0x1033D8A0` | `ClearActiveDisciplines` entity input |
| `client.dll 0x100A8DE0` | quickbar Discipline command dispatch |
| `client.dll 0x100A94D0` | visible-ordinal → compiled Discipline index mapping |

The reproducible case is `research/cases/disciplines/README.md`; generated decompilation and data
surveys remain under `ELYSIUM_WORK_ROOT`.
