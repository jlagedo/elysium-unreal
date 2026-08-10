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
- `input_action_survey`, `weapon_activity_survey` and the cap12 player-animation inventory join
  the hash-pinned command bits, melee/ranged class vtables and activity tables, and model sequence
  descriptors; their generated ledgers remain below `ELYSIUM_WORK_ROOT`.
- `research/cases/core-mechanics/` preserves the address set and open joins.
- Valve's public Source SDK supplies only the inherited frame of reference:
  [`CTakeDamageInfo`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/takedamageinfo.h)
  carries the inflictor, attacker, weapon, scalar damage and damage type, and
  [`CBaseCombatCharacter`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/basecombatcharacter.cpp)
  consumes it. VtMB's `CVDmg_t`, feat rolls and health counters are fork-specific findings,
  not behavior inferred from the SDK.

These are offline binary/data findings. The shared, ranged and melee paths are closed far
enough to implement statically; several special filters and a live timing/formula capture remain
explicitly open.

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

## Firearms and ranged combat

### Input and firing modes

`CWeaponRanged` owns the ordinary firearm controller. The sampled Glock, M37, Uzi and crossbow
classes use the same post-frame, primary/secondary, shot, reload and mode-dispatch vtable slots;
their class tables primarily supply activity translation. The sampled Steyr also shares those
attack slots but overrides post-frame to update its two authored scope-sway oscillators before
entering the common weapon frame.

The common frame consumes the primary, secondary and reload button states separately. Primary
and secondary wrappers select mode 0 or 1, then `CWeaponRanged::ModeDispatch` (`0x102383B0`)
branches on the active mode's authored `Type`:

| Authored behavior | Native result |
|---|---|
| ordinary attack modes | enter attack start from input; enter the actual shot callback from a matching animation event |
| zoom loop | cycle the weapon's scope range/state and notify the owning player |
| `Toggle_Primary_Mode` | swap primary modes 0/1, play the corresponding mode-change activity and refresh weapon timers |
| consumable/throw-style mode | play its activity, consume ammunition and remove an exhausted item |

This is a **fire-mode state machine, not a firearm combo system**. A mode whose loaded
`allow_autofire` bit is clear loses held attack intent after the press edge; setting the bit lets
the common frame invoke primary attack repeatedly while held and the next-attack timer permits.
The patch-first Uzi and Steyr attack modes demonstrate the latter, while an ordinary Glock mode
does not declare it. The Anaconda and Uzi demonstrate `Primary`/`PrimaryMode2` records plus a
secondary `Toggle_Primary_Mode`; the Anaconda's faster, less accurate second record is its fan
mode. `BurstMin` and `BurstMax` are loaded and the loader forces `BurstMin <= BurstMax`, but no
player-side combo/burst queue was found in this shared input-to-shot path. Their exact player/NPC
consumer boundary remains open.

### Animation owns the shot moment

An accepted attack does not trace directly from the input function. `CWeaponRanged::Attack`
(`0x10238580`) first rejects or reloads an empty magazine, selects the current firing activity,
notifies the owner and sends compact player action `5` (`PLAYER_ATTACK1`). The ordinary player
selector realizes `ACT_RANGE_ATTACK1_LAYER`, after which the weapon activity table translates it
to a specific activity such as `ACT_RANGE_ATTACK_LAYER_GLOCK`,
`ACT_RANGE_ATTACK_LAYER_M37`, `ACT_RANGE_ATTACK_LAYER_STEYR`,
`ACT_RANGE_ATTACK_LAYER_SUBMACHINEGUN` or `ACT_RANGE_ATTACK_LAYER_CROSSBOW`. The current model
then supplies the exact weighted sequence.

The attack start also advances the weapon's next-shot schedule by authored `Attack_Rate` (scaled
by the owner's attack-speed value) and counts every shot interval due by the current frame. The
actual ballistic/projectile consumer is `CWeaponRanged::Shot` (`0x102387B0`), reached when
`CWeaponRanged::HandleAnimEvent` (`0x10238160`) receives one of its ranged-attack event ids and
re-enters mode dispatch in event mode. Thus the faithful order is:

```text
attack / attack2 intent
    -> authored mode dispatch and next-attack eligibility
    -> PLAYER_ATTACK1 / ACT_RANGE_ATTACK1_LAYER
    -> weapon activity translation and weighted sequence
    -> sequence attack event
    -> CWeaponRanged::Shot
    -> rays or projectile, impact grouping and damage
```

The hash-closed male and female `move_and_ranged.mdl` banks independently contain aim, ordinary
attack, attack-layer, dry-fire, dry-fire-layer, reload and reload-layer sequences for the sampled
weapon families, plus generic pistol, two-handed and submachine-gun fallbacks. Representative
male layer clips are short one-shots: Glock 15 frames at 30 fps, M37 14, Steyr 7, crossbow 16 and
submachine gun 4. Their sequence descriptors contain two or three events. Those event counts are
not all damage commits; only the native event-id dispatch above identifies a shot event.

### Shot count, accuracy and kick

The shot body keeps two authored counts distinct:

- `Ammo_Cost` is the number of loaded rounds spent per scheduled shot. Due shots are clamped to
  what the active magazine can pay.
- `Ammo_Fired` is the ray/pellet count placed in the fire packet for each scheduled shot. The
  M37's patch-first record therefore spends one shell while emitting eight buckshot rays.

The same packet carries a copy of the active `CVDmg_t`, the ammo type, muzzle/aim vectors, mode
range data and its spread cone. The body queries the attacker's Presence bonus and Shaky Hands
penalty and joins the authored `WeaponRanges`/`GrossPointBlank` tables. `SpreadAngle`, `Accuracy`,
crosshair fields and those modifiers are all live data surfaces, but the final cone/crosshair
formula is not yet closed tightly enough to reproduce from this static pass alone.

After a valid shot, `CWeaponRanged::Kick` (`0x102397B0`) reads the attacker's raw attack feat,
clamps it to compiled bounds, quadratically interpolates between the active mode's kick bounds,
draws two random signed offsets and applies them through the player view-angle callback. The
Steyr's separate post-frame scope sway is additive state, not the shot kick itself.

Crossbow modes use this same controller and event-driven shot entry. Their item records set
`fires_projectile`, projectile velocity/model and stake-damage policy, so the downstream fire
packet takes the projectile/stored-impact route rather than making the crossbow a separate player
attack state machine.

### Reload and dry fire

Empty fire and reload are distinct actions. `CWeaponRanged::FireOnEmpty` (`0x10238230`) plays the
mode-specific dry-fire activity/event, advances both attack timers and notifies the player. The
ranged reload slot (`0x10239570`) chooses a reload activity and calls the common reload request.
That request starts only when the owner has reserve ammunition (or the explicit infinite-ammo
policy), reload is permitted, and at least one compatible magazine has missing capacity.

Reload start selects the weapon/viewmodel activity, sends compact player action `14`
(`PLAYER_RELOAD`), and sets the weapon and owner end times from the selected sequence duration
divided by the active playback-rate scalar. `ReloadTime` is loaded from the magazine record, but
this player reload-start body does not use that field as its completion clock.

At completion, ordinary reload fills each compatible magazine by
`min(missing capacity, reserve)` and removes the same amount from reserve on the authoritative
path. With `reload_single`, the frame handler instead adds one round, removes one reserve round,
and re-enters reload until interrupted, full or out of reserve. The patch-first M37 declares
`reload_single 1`. Fire intent while a single-round reload is live sets an interruption latch;
the reload frame finishes the transaction before normal firing resumes.

### Damage and reaction boundary

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

There is no firearm analogue of the melee block/opposed-reaction bands in this chain. A ranged
weapon reports capability `0x2000`, which does not satisfy the player's melee-block capability
mask `0x18000`; `+wpn_secondaryatk` can still forward ordinary `attack2`, but a gun does not enter
`ACT_PREBLOCK`. The ranged per-victim body calls a defender-specific ranged-response virtual after
calculating its result; the base-player implementation is empty. The separate generic character
`DamageFlinch` routine can choose `ACT_HIT_HEAD` or `ACT_HIT_TORSO` and orient `hit_yaw`, but this
pass did not close every firearm caller or a firearm-specific stagger threshold. A remake must not
invent a melee-style firearm stagger meter from these findings.

## Melee attack, combo, block and damage

### Weapon and input surface

Retail distinguishes the always-carried `item_w_unarmed` from the weapon that actually punches.
`CWeaponUnarmed` is not the melee implementation and has no ordinary fist-attack activity table;
`item_w_fists` is `CWeaponMelee_Fists`, inherits `CWeaponMelee`, and carries the full attack,
combo, dodge and block translation surface. Armed melee classes such as baton, knife, baseball
bat, katana and sledgehammer inherit the same request path with their own activity translations.

`CWeaponMelee::ItemPostFrame` (`0x103EAEC0`) polls the held primary-attack bit and the weapon's
next-attack time, then enters `PrimaryAttack` (`0x103EACA0`). That body rejects a live grapple,
may start the paired sneak-attack route for a valid target, otherwise requests
`ACT_MELEE_ATTACK`, substitutes `ACT_MELEE_AIR_ATTACK` while airborne, and may fall back to
`ACT_KICK` when the weapon capability allows it. The melee `SecondaryAttack` body at
`0x103EAE00` requests `ACT_MELEE_ATTACK_HEAVY`. The class capability result is `0x40018000`;
its `0x18000` portion is also the capability gate used by the player block resolver.

The command overlap is deliberate and must not be simplified to "attack2 means block":
`+attack2` owns the ordinary weapon-secondary route, while `+wpn_secondaryatk` asserts a second,
dedicated held block bit and then forwards to `+attack2`. The complete button and compact-action
route is in `docs/vtmb/controls.md`.

### The combo is automatic, not directional

`CWeaponMelee::RequestActivity` (`0x103E9E00`) has one live combo substitution. When the requested
activity is exactly `ACT_MELEE_ATTACK`, it asks virtual `+0x5D4` which base **Ability** controls
the chance: ordinary melee returns slot 6 (`Melee`), while fists override it with slot 1
(`Brawl`). It reads the saved base value through `CVStatList_t::GetBase`; temporary/effect-adjusted
current values do not enter this test. A random integer in `[0, 99]` is compared against this
rank table recovered from `vampire.dll`:

| Base ability rank | 0 | 1 | 2 | 3 | 4 | 5 |
|---:|---:|---:|---:|---:|---:|---:|
| `ACT_MELEE_ATTACK_2COMBO` chance | 0% | 10% | 25% | 45% | 70% | 100% |

On success the function recursively requests `ACT_MELEE_ATTACK_2COMBO`; otherwise it continues
with the ordinary activity. There is no movement-direction read in this branch and no recovered
three-direction input state machine. Labels such as `med`, `low`, `far` and `jump` are model
sequence variants, not commands.

The weapon table then translates the generic activity to a weapon activity such as
`ACT_MELEE_ATTACK_FISTS`, `ACT_MELEE_ATTACK_2COMBO_KNIFE` or
`ACT_MELEE_ATTACK_HEAVY_BASEBALLBAT`. The ordinary player apply order remains:

```text
logical activity
  -> weapon ActivityOverride
  -> player NPC_TranslateActivity
  -> SetActivity
  -> SelectWeightedSequence by model actweight
```

The hash-closed player-model inventory contains separate ordinary, `2COMBO`, heavy, air,
preblock, block, heavy-block and left/right blocked-reaction sequences for fists and the sampled
melee weapons. Multiple sequences can answer one activity and their `actweight` controls the
choice; weight-zero variants are not selected by the ordinary weighted resolver. The dedicated
`2COMBO` activity and clip family are therefore real, but the name alone does not establish how
many damage commits a clip produces. A live attack capture is still required to count impact
windows and interruption timing.

### Opposed record and reaction margin

Before impact, `CBaseCombatCharacter::CalcAndStore...` appends a 16-byte record to the defender's
result array at `+0xA88`, whose active count is at `+0xA94`, and stores:

| Word | Stored value |
|---:|---|
| 0 | attacker/entity handle |
| 1 | active weapon's total lethality |
| 2 | defender `Defensive_Maneuvers` net successes, plus any bounded defense bonus |
| 3 | defender soak successes selected from the weapon's active `CVDmg_t` |

The record is keyed by attacker. `CBaseCombatCharacter::GetMeleeDiceRolls` searches the array and
returns the matching record; `GetNumAttackSuccesses` returns its word 1. The signed reaction
margin is `lethality - defense - soak`. Custom NPC `RunTask` handlers and the player block path
use the five-way classifier at `0x103498B0`.

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

### Block and stagger reactions

The block is a real player action, but it is a transaction rather than a single boolean. The
server input classifier requires the dedicated `+wpn_secondaryatk` bit, ground contact and an
active weapon with capability `0x18000`; it returns compact player action 13, whose ordinary
animation route requests `ACT_PREBLOCK`. At impact, `WasMeleeBlocked` (`0x10345AB0`) additionally
requires a block-capable defender, a held melee weapon and a frontal/facing test. A player counts
as actively blocking while its ideal activity is `ACT_PREBLOCK` or `ACT_BLOCK`; non-player
defenders use the stored roll classifier.

On a blocked contact, the defender callback at `0x10160BC0` can add bonus soak rolls from
attribute slot 2 (`Dexterity`) when the incoming margin is positive, then classifies the updated
record. Class 3, the **defender block stagger** band, plays `ACT_BLOCK_HEAVY`; the other blocked
classes play `ACT_BLOCK`. The attacker callback at `0x10160D00` plays the blocked-reaction
activity stored at `+0x2E0` in the attacker's current sequence descriptor, falling back to
`ACT_BLOCKED_REACTION_RIGHT`. This is where authored left/right blocked reactions enter; they are
not chosen from a movement direction at input time.

If the updated record is not damaging, the impact exits without damage. If positive damage
remains, the damage path continues even though block reactions played. Thus **blocked** does not
mean **zero damage**. A stronger unblocked result takes the separate normal-hit or knockback
callbacks; the player knockback body selects a reaction sequence and applies impulse/timing.
There is no recovered standalone player `STAGGER` command or compact action: the concrete melee
stagger is the heavy-block reaction band, with hit/knockback as a separate outcome.

### Melee damage commit

`CWeaponMelee::MeleeImpact` (`0x102579F0`) is the downstream consumer that was previously open.
It copies the active mode's `CVDmg_t`, resolves the stored attack record and block reactions,
marks the descriptor's direct-damage route, and carries it through the common descriptor/soak
path. The remaining `DamageInflicted` is floored up to the active Potence rank when Potence is
higher. Retail's own diagnostic string and the body agree on the final scalar:

```text
Total = DamageInflicted * (BaseDamage + DamageModifier) * Multiplier
```

`BaseDamage` is descriptor word 1, `DamageModifier` is the descriptor's evaluated modifier
reference, and `Multiplier` comes from the `CTakeDamageInfo`/trace envelope. The result is
committed through the ordinary impact and health route. This closes the melee result-to-damage
join; naming every special multiplier/filter and validating the exact live attack windows remain
open.

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

- Close the exact spread/cone and crosshair formula, including the confirmed authored range data,
  Presence modifier and Shaky Hands penalty.
- Find the player/NPC consumers of `BurstMin`/`BurstMax` and the complete caller chain from firearm
  damage into generic `DamageFlinch`.
- Capture ordinary, `2COMBO`, heavy, blocked, heavy-block and knockback attacks to validate
  impact-window counts, interruption and timing against the static activity/damage chain.
- Identify the complete `SkillRequirement` consumer and its relation, if any, to the attacker
  adjustment.
- Decompose the ranged multiplier into volley share, hitgroup and other trace modifiers.
- Join descriptor word 15 and item `DmgModifier` to the exact post-soak numerical operation.
- Name the remaining special immunity, relationship and secondary-effect predicates in
  `CVDmg_t::Apply`.
- Resolve `DMG_FIST` fallback/alias behavior.
- Name the alive-path prefilter and confirm every scalar-to-integer rounding boundary.
- Capture semi-auto, held-auto, mode-toggle, dry-fire, bulk-reload and single-round-reload retail
  sequences, plus one ranged and one melee hit with printed roll diagnostics, to validate the
  offline formulas and attack-state timing end to end.
