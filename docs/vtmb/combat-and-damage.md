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
- `research/cases/core-mechanics/` preserves the weapon/resolver address set;
  `research/cases/npc-combat-lifecycle/` preserves the enemy, NPC response, health and death joins.
- Valve's public Source SDK supplies only the inherited frame of reference:
  [`CTakeDamageInfo`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/takedamageinfo.h)
  carries the inflictor, attacker, weapon, scalar damage and damage type, and
  [`CBaseCombatCharacter`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/basecombatcharacter.cpp)
  consumes it. VtMB's `CVDmg_t`, feat rolls and health counters are fork-specific findings,
  not behavior inferred from the SDK.

These are offline binary/data findings. The shared, ranged, melee, NPC-response and death paths
are closed far enough to implement statically; several special filters and live
timing/presentation captures remain explicitly open.

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

## World-area weapon admission

Elysium policy joins before the attack and damage transactions below. When world
`m_nAreaType` changes to 2, the server equips every connected player with `item_w_unarmed` and
clears all active Discipline effects. `CBaseCombatCharacter::Weapon_CanSwitchTo` composes the
shared player-action blocker and, while Elysium is active, refuses every candidate whose classname
is not `item_w_unarmed`. The same blocker reaches the player weapon-action decision, so state 2 is
not only a holster animation or client HUD convention.

Static evidence closes forced unarmed state, non-unarmed switch refusal and the shared pre-action
predicate. The exact primary-versus-secondary refusal call order and player feedback remain live
acceptance; no Elysium condition belongs inside `CVDmg_t::Apply` or the health commit.

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

### Animation schedules the shot moment; weapon logic commits it

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
actual ballistic/projectile consumer is `CWeaponRanged::Shot` (`0x102387B0`).
`CWeaponRanged::HandleAnimEvent` (`0x10238160`) accepts ranged event ids 3030–3044 and re-enters
`ModeDispatch` `0x102383B0` in event mode; that arm calls the weapon's shot virtual. The server
sequence event therefore chooses the authored instant but does not itself spend ammunition or
apply damage. `Shot` is the authoritative boundary: it clamps due shots to the loaded magazine,
spends `Ammo_Cost` and constructs the fire packet. Client `C_BaseViewModel::FireEvent`
`0x100AB530` runs independently on the presentation copy, offering the event to the client weapon
before its attachment-effect fallback; it has no ammunition, ray or damage path. Thus the faithful
order is:

```text
attack / attack2 intent
    -> authored mode dispatch and next-attack eligibility
    -> PLAYER_ATTACK1 / ACT_RANGE_ATTACK1_LAYER
    -> weapon activity translation and weighted sequence
    -> server sequence attack event -> event-mode dispatch
    -> CWeaponRanged::Shot (ammunition/fire-packet commit)
    -> rays or projectile, impact grouping and damage

networked client sequence -> client viewmodel event -> attachment effects only
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

The corresponding viewmodel reload events are visual carriers only. The server order is reload
request/activity selection → common reload frame → `0x102552C0` reserve-to-magazine commit; for
`reload_single`, the frame returns to the per-round reload state after each commit. A magazine or
shell event on either client viewmodel cannot advance that transaction.

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
`ACT_MELEE_ATTACK_HEAVY_BASEBALLBAT`. The ordinary player apply order is:

```text
logical activity
  -> weapon ActivityOverride
  -> player NPC_TranslateActivity
  -> enumerate matching model sequences
  -> select by sequence state mask and activity weighting
  -> commit activity, sequence, cycle and playback rate
```

The hash-closed player-model inventory contains separate ordinary, `2COMBO`, heavy, air,
preblock, block, heavy-block and left/right blocked-reaction sequences for fists and the sampled
melee weapons. Multiple sequences can answer one activity and their `actweight` controls the
candidate order. The player selector at `0x10160F90` then reads the custom sequence word at
`+0x2D4` and compares it with player state `+0x2088 & 0x79A`; it prefers an exact state-mask
match, then two partial-match classes, then a zero-mask fallback. The target argument is not read
by this player selector. Labels such as `med`, `low`, `far` and `jump` therefore are not direct
input commands or distance tests, but the concrete clip can still depend on the current player
state mask.

### Target acquisition, sequence commit and recovery

`CWeaponMelee::RequestActivity` performs target acquisition before it commits the selected
sequence. It reads the custom reach float at `+0x2D0` from every sequence returned for the
translated activity and uses the maximum as the query distance. `CBaseCombatCharacter::FindEntityFOV`
(`0x10341C30`) then:

1. traces straight forward with mask `0x46004003` to that maximum reach;
2. accepts a valid obstruction hit first;
3. otherwise scans the reach volume and chooses the visible candidate with the highest forward
   dot product inside a 30-degree half-angle (a 60-degree full cone).

The melee predicate at `0x103EA4D0` rejects self, a missing entity and any entity whose
`m_lifeState` is not `LIFE_ALIVE`. The shared query also rejects non-targetable and
`ScriptHidden` entities. This is aim assistance and opponent reservation, not a damage verdict:
an ordinary swing may still animate when no candidate is found.

For a player attacker, an accepted target is stored in the replicated
`m_nMeleeOpponentIndex`/paired target-handle state and the target receives an incoming-melee
notice. `m_bNeverMeleeOpponent` clears the melee-opponent index and suppresses that notification.
The NPC notice path accepts the warning in its eligible states when the attacker is within 150
Source units or satisfies its visibility route, remembers the attacker for five seconds and lets
the concrete combatant schedule its response. No health or damage is changed by this reservation.

After a concrete sequence is selected, the player path stores the requested and translated
activities, sets the sequence, resets cycle to zero and resets sequence information. Its playback
rate is the character's base attack-rate scalar multiplied by:

```text
0.70 + 0.03 * evaluated attack-feat rank
```

The next player attack, primary weapon attack and secondary weapon attack are all held until at
least:

```text
now + selected sequence duration / playback rate
```

The writes are maximum operations, so a pre-existing later deadline is not shortened. NPC use can
also clamp the duration to a weapon-provided minimum. Melee recovery is therefore selected-clip
timing, not the ranged mode's authored `Attack_Rate` and not one global fist or weapon cooldown.

### Contact, opposed-roll and impact boundary

The ordinary player fist and katana attack sequences in the hash-closed female and male shared
banks have `event_count == 0`. Their normal damage commit is consequently **not** a firearm-style
server animation event and must not be made an authoritative Unreal montage notify merely because
that is convenient. VtMB does have special creature/event attacks that construct a trace from a
staged opponent, but that is a separate path and is not evidence for ordinary fists or weapons.

The downstream ordinary boundary is a real trace. `MeleeRollAndSendNoticeCallback`
(`0x10346830`) calculates and stores the opposed result for a contacted combat character;
`CWeapon`'s shared traced-impact virtual at `0x102579F0` reads the entity from that trace, consumes
the defender-side record, resolves block/reactions and commits damage. Static recovery has not yet
identified the normal swing caller that owns the contact sweep/window between those two entries.
The dedicated `2COMBO` clip family is real, but its exact number of contact windows, sweep shape,
refire/interruption behavior and miss timing still require that caller join or a live capture.

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

The shared weapon traced-impact body at `0x102579F0` is the downstream melee consumer that was
previously open. It occupies the same base-weapon virtual in melee and ranged weapon vtables, so
`CWeaponMelee::MeleeImpact` is a useful semantic label for the melee branch, not a recovered class
or symbol name.
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

After the alive virtual returns, `CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`) reads the RPG
stats directly. `Health < Max_Health` survives; `Health >= Max_Health` calls the character's
`Event_Killed` virtual. Source `m_iHealth` is a projection for engine consumers, not the authority
that selects death. A target with `takedamage == 0` is rejected before the transaction, and a
team/friendly-damage gate rejects disallowed non-self damage. Life state selects distinct alive,
dying and dead virtuals; the dying consumer accepts without repeating the alive transaction.

The successful alive path also reports damage severity back to an eligible attacker's combat
feedback path: positive damage through 1, over 1 through 5, and over 5 form three nonlethal tiers;
lethal damage uses the death tier. This feedback is downstream of the target's authoritative
commit.

The exact identity and mutation rights of the alive-path prefilter, and some float-to-integer
rounding points around scalar damage, remain open.

## NPC damage response and stagger boundaries

`CAI_BaseNPC::OnTakeDamageAlive` (`0x10265ed0`) consumes the committed result in this order:

1. Call the shared combat-character alive commit.
2. Fire `OnDamaged` on success; when projected Source health is no greater than half its Source
   max, also offer `OnHalfHealth`.
3. Record attack position/attacker and update enemy memory.
4. Ask the class light/heavy predicates to set `LIGHT_DAMAGE` (`0x4c`) and `HEAVY_DAMAGE`
   (`0x4d`).
5. Accumulate damage for one second; a sum over 15 percent of Source max health sets
   `REPEATED_DAMAGE` (`0x4e`), otherwise an expired window is reset.
6. Emit the NPC damage sound/event path.

The Troika NPC override at `0x102beda0` first saves the complete incoming damage packet at
`+0x660c`, then composes the base transaction. A surviving positive hit remembers the attacker for
five seconds and notifies the active schedule. A special NPC flag can force `Event_Killed`; its
authored semantic name is not yet proven and must remain an explicit flag rather than an invented
general rule.

Five reaction concepts are independent:

| Reaction | Authority |
|---|---|
| melee block stagger | the opposed margin's heavy-block band; selects `ACT_BLOCK_HEAVY` |
| melee hit/knockback | the stronger unblocked outcome; reaction sequence plus impulse/timing |
| light/heavy/repeated damage | AI conditions that can interrupt a schedule and select a flinch/cover response |
| generic damage flinch | `DamageFlinch` (`0x103229d0`), random head/torso activity plus directional `hit_yaw` |
| death | `Health >= Max_Health`, life-state transition and `Event_Killed` |

`DamageFlinch` derives hit yaw from actor yaw minus the incoming-vector angle, adds a random
`[-30,+30]` degrees, and requests the chosen layer/gesture with 0.1/0.3 fade values. The AI's
`SMALL_FLINCH` schedule is another owner: it remembers flinched state, stops movement and runs
`TASK_SMALL_FLINCH`; alert AI can instead take cover from the attack origin. A remake must not
invent one universal stagger meter or make every positive hit cancel the current action.

## NPC and player death transaction

The shared `CBaseCombatCharacter::Event_Killed` body (`0x1032b9b0`) sets life state 1 (dying),
cleans weapon, effect and ownership state, constructs the ragdoll-force envelope, and notifies the
killer and game rules. The NPC override at `0x10265ad0` is schedule-aware:

- an NPC already in the death schedule ignores a duplicate kill;
- a non-interruptible scripted sequence defers the kill packet, while an interruptible owner is
  cancelled;
- `OnDeath` fires once through the native guard at `+0x5bd4`;
- current and ideal NPC state become 7 (dead), strategy and squad claims are vacated, and death
  sound/solid-body policy leads to the death schedule.

The Troika NPC override (`0x102bf340`) additionally releases hints and feed/claim ownership,
notifies owner/maker systems, invokes Python `MarkAsDead('<targetname>')`, and updates its special
partner/owner memory. These are consequences of the one death commit; a maker child count must not
be decremented from a hit or flinch path.

The player damage wrapper (`0x10163020`) adds player-specific refusal/protection gates and tears
down conversation, use, grapple and special-control state before/around the shared commit. The
player death override (`0x10163af0`) stops active weapon/controllers, notifies game rules, selects
the death action/screen from `vdata/Signs/death.txt`, and composes the shared combat-character
cleanup. NPCs and the player share the authoritative damage counter and lethal comparison; their
outer AI, I/O and presentation lifecycles are deliberately different.

## Faithful implementation seams

The baseline requires distinct types and stages:

- an authored weapon-mode record (`BaseLethality`, `SkillRequirement`, `Dmg`);
- a parsed `CVDmg_t`-equivalent value object;
- an accepted-swing transaction carrying logical activity, concrete sequence, aimed opponent,
  playback rate and recovery deadline;
- a later trace/contact transaction that can miss, stage one opposed record and commit at most the
  contact windows authored by the move;
- attack-specific ranged/melee hit and defense policy;
- one shared dice/soak/filter resolver;
- a typed health commit that knows bashing/lethal/aggravated, blood shield and unkillable;
- an audit result that exposes every intermediate value for retail comparison.

A scalar `TakeDamage(float)` cannot express this contract. Project implementation status and
the known scalar-path divergence are tracked by roadmap 13.3 and RE40 rather than in this VtMB
fact document.

## Reverse-engineered mechanics (RE40)

- **Ranged Spread, Cone and Crosshair**:
  - `m_fCurrentRangedAccuracy` (`+0x1ddc`) dynamically interpolates between min/max accuracy bounds in `thunk_FUN_101600a0`.
  - Authored weapon mode `SpreadAngle` / `SpreadAngleMax` define the cone of dispersion.
  - HUD crosshair expansion directly mirrors `CrosshairMinSize` and `CrosshairWalkSizeMax`.
  - `Presence` modifier and `Shaky Hands` penalty log diagnostic messages via `DevMsg` in `WeaponRangedShot` (`0x102387b0`) but do not alter the physical spread cone.
- **Burst Fields and DamageFlinch Pipeline**:
  - `BurstMin` (`+0x3a4`) and `BurstMax` (`+0x3a8`) are parsed by `WeaponModeDataLoader` (`0x10259230`) from weapon script files but are unreferenced by runtime combat logic (dead fields).
  - Firearm damage enters `DamageFlinch` (`0x103229d0`) via: `RangedDamagePerVictim` (`0x10268330`) -> `DispatchTraceAttack` (`0x101cfef0`) -> `CBaseEntity::TraceAttack` (vtable slot 101) -> `DispatchTakeDamage` -> `CBaseCombatCharacter::OnTakeDamage_Alive` (`0x103302e0`) -> `CBaseCombatCharacter::DamageFlinch`.
- **Melee Swing Pipeline and Impact Dispatch**:
  - `CWeaponMelee::PrimaryAttack` (`0x103eaca0`) sets the attack animation activity. The resulting animation event triggers `EventDispatch` (`0x103ea510`), calling `thunk_FUN_10253b70` (the melee hit applicator / `Smack()`).
  - Contact enumeration is handled through `thunk_FUN_10170e80`.
  - The shared traced-impact virtual `WeaponDoImpactEffect` (vtable index 270 / `0x102579f0`) is bypassed by `CWeaponMelee`, which processes its trace impacts directly in its internal contact loop.
- **SkillRequirement**:
  - `SkillRequirement` is parsed to weapon mode field `+0x3d0` in `WeaponModeDataLoader` (`0x10259230`) and is never read or checked elsewhere in engine binaries (dead field).
- **Ranged and Melee Damage Post-Soak Operations**:
  - **Melee Damage Formula**: $\text{Final Damage} = \text{Lethality} \times (\text{BaseDamage} + \text{DmgModifier}) \times \text{Multiplier}$, where `DmgModifier` is the attacker's Feat rating (Brawl or Melee stat bonus) and Potence guarantees a minimum lethality floor.
  - **Ranged Damage Formula**: $\text{Final Damage} = \text{Lethality} \times \text{BaseDamage} \times \text{Multiplier}$, where `Multiplier` is $(\text{Volley\_Fraction} \times \text{Hitgroup\_Scale})$. Firearm Feat scales accuracy rather than flat damage.
- **Special Predicates and CVDmg_t::Apply**:
  - `CVDmg_t::Apply` (`0x101fb200`) invokes an installable game callback (`DAT_1074e7bc`) registered via `CVDmgSetApplyCallback` (`0x101fb180`).
  - Special damage modifiers and immunities are resolved in `CBaseCombatCharacter::ApplySpecialDamageModifier` (`0x1033db30`), matching weapon/damage IDs against the entity's 224-entry lookup table to trigger condition flags (`0x400`) and reactive audio.
- **DMG_FIST Alias**:
  - `DMG_FIST` is not an engine damage flag; unarmed melee attacks parse and alias directly to `DMG_CLUB` (`0x80` / Bashing damage) in `FUN_101fab10`.
- **Alive-Path Filtering and Rounding**:
  - `OnTakeDamage_Alive` (`0x103302e0`) guards against dead/invalid states (`0x168 != 0x1e/0x1f`) and non-positive incoming values (`damage <= 0.0`).
  - Final damage values truncate to integer via standard `__ftol()` (FISTP truncation toward zero) prior to deducting entity health.

