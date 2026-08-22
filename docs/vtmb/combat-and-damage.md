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
| `BaseLethality` | read by the weapon's base-lethality accessor (mode `+0x3f8`) and included in total lethality |
| `SkillRequirement` | written once to mode `+0x3d0` by `WeaponModeDataLoader` (`0x10259230`) and **read by nothing image-wide** — no gate, no penalty, no diagnostic. The item files' own comment describing it as a requirement to wield the weapon is wrong |

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

### The authored knockback inputs [data-verified]

Knockback is authored in five separate places, and they do not all have consumers. **The one that
actually selects a melee knockback is the sequence descriptor's own swing records** — see
"Knockback" below; the three item/rules keys catalogued here are a dead field, a ranged distance
pair and a player-only view-kick refractory. The fifth is the discipline `HitInfo` block, which
owns two mutually exclusive keys of its own — see "The discipline knockback path" below.

**`knockback_chance` is a melee key, on sixteen weapon definitions**, a probability in `0..1`:

| Value | Weapons |
|---:|---|
| 0.9 | sledgehammer |
| 0.6 | bush_hook |
| 0.4 | baseball_bat, severed_arm, torch, zombie_fists |
| 0.3 | fists, sabbatleader_attack |
| 0.1 | baton, tire_iron, claws, chang_claw, claws_ghoul, wolf_head, wolf_head-null |
| 0.05 | fireaxe |

Katana, knife and sheriff_sword author none, and neither does any ranged weapon. **This is retail
content, not a patch addition** — `item_w_sledgehammer.txt`, `item_w_fists.txt` and `rules.txt` all
carry their values inside `pack101.vpk`.

**`knockback_chance` is nonetheless a dead field.** The loader parses it into the weapon-data
record — **one write site, `0x1025a41f`, and the record's own copy pair** — and **no gameplay path
reads that storage** back: no probability draw, no knockback gate, no diagnostic. Nothing in the
recovered knockback chain consults a per-weapon chance: the launch
gate is the victim's hit-buildup counter and the swing record's own unconditional marker, and the
activity is chosen from the swing record's candidate list. A remake that rolls `knockback_chance`
invents a mechanic retail authored and then never wired.

**Ranged modes author a distance pair instead**, `MajorKnockbackDist` / `MinorKnockbackDist` —
retail's own major/minor two-tier vocabulary, not a probability. Retail (`pack101.vpk`) authors it
on four weapons: Ithaca M37 and the super shotgun `105 / 250`; the Colt Anaconda two minor
distances (`400`, `350`) across its two modes and no major; the Remington M700 a lone minor
`1500`. The flaming crossbow (`105 / 250`), frag grenade (`100 / 1000`), Desert Eagle (minor
`400`) and the M700 Bach variant (minor `1500`) are patch additions — their retail files carry no
`KnockbackDist` line. **What reads the pair is open** — no consumer of either distance is
recovered (`RE-K9`).

**`rules.txt` owns a refractory window that is not a knockback window**:
`Knockbacks { KnockbackPreventTime 5.0 }`, parsed to rules `+0x3DC`. Its only consumer is the
**player's ordinary hit reaction** (`0x10160a60`), where it gates the size of the view kick: a hit
landing more than `KnockbackPreventTime` after the last one takes the large kick, a hit inside the
window takes the small one, and either re-arms the timer. It never prevents a knockback, and it
never applies to an NPC. The name describes an intent the shipped code does not implement.

The fourth authoring — the one the melee runtime actually selects from — is the **per-swing
knockback table inside each sequence descriptor's swing records**, covered under "Knockback"
below. The fifth is the **discipline `HitInfo` block**, whose `AI_Schedule` and `Knockback` keys
reach two different mechanisms; both have consumers, and no authored hit table sets both.

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
| 15 | `0x3c` | accumulated damage-filter float; it is the descriptor's half of the final `Multiplier` | confirmed |
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

`DMG_FIST` is authored but has **no entry in `StrToDMGFlags`**, which therefore returns `0` for it.
It does **not** alias to `DMG_CLUB` or to anything else: a fist attack carries an empty Source
damage mask, which is what keeps it out of the `0xC8000008` aggravated/Kindred test as an ordinary
bashing blow. The damage *family* still comes from the `Dmg` string's `Bashing` token, not from
this bit.

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

Step 9 reads `DamageFilterBashing`, `DamageFilterLethal`, `DamageFilterAggravated` and the flame
filter from the victim's NPC template and accumulates them into descriptor word 15, and also
performs damage-flag immunities and an attacker/victim relationship percentage. **Word 15 is the
descriptor's half of the attack's final `Multiplier`** — the melee and ranged result formulas below
multiply the trace/volley scale by it, which is where the accumulator joins committed health
damage. Each individual filter's own arithmetic contribution into that accumulator is unnamed
**[open]**.

The difficulties in steps 3 and 7 are authored, not compiled: `rules.txt`'s `Damage_Info` block
states `Soak_Difficulty_PC 3` / `Soak_Difficulty_NPC 7` and `Defense_Difficulty_PC 3` /
`Defense_Difficulty_NPC 7`. **The player soaks and defends on 3+ where an NPC needs 7+**, which is
the single largest authored asymmetry in the whole combat system. Every roll is one d10 per feat
point, and net successes remove lethality dice.

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
selected feat is rolled at the authored `Soak_Difficulty_PC` 3 or `Soak_Difficulty_NPC` 7 and
returns `max(successes - botches, 0)`. Automatic soak is added by the apply callback, outside this
resolver.

**The soak feats' own pools are authored, and their asymmetry is deliberate.** `feats.txt` builds
the bashing soaks from Stamina plus `Soak_Pool` and the **lethal** soaks from `Soak_Pool` alone —
Troika's own `Base2 Stamina` line sits commented out on the lethal rows, so excluding Stamina from
a lethal soak is a decision the file records rather than an omission. The **aggravated** soaks take
`Soak_Pool` only as well, and because nothing feeds `Soak_Pool` against aggravated damage for an
ordinary character, **Fortitude's `Automatic_Soak_Successes` is the whole of an aggravated soak**.
A remake that adds Stamina to lethal or aggravated soak makes the player materially tougher than
retail.

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
range data and its spread cone. `SpreadAngle` is the **only** authored spread keyfield —
`SpreadAngleMax` does not exist anywhere in the corpus. The loader instead computes
`SpreadAngle * two engine constants` once and stores that same value into all three axes of the
per-mode spread vector (`+0x3ac/+0x3b0/+0x3b4`); the z component is never read downstream. The
body queries the attacker's Presence bonus and Shaky Hands penalty and joins the authored
`WeaponRanges`/`GrossPointBlank` tables, but Presence/Shaky Hands are **confirmed cosmetic**: both
are logged via `DevMsg` behind a developer cvar in `CWeaponRanged::Shot` and never read again
after that call.

`m_fCurrentRangedAccuracy` (`+0x1ddc`) is maintained only by the player's `PostThink`
(`thunk_FUN_101600a0`, the sole caller) — NPCs never run this path. It converges a target
crosshair size toward the authored `CrosshairMinSize`/`CrosshairWalkSize*`/`CrosshairRunSize*`
fields at a rate scaled **cubically** by `GetRawAttackValue` (the Firearms feat rank), plus a
widening term from mouse-look angular delta, clamped to authored bounds. The fired cone
(`FUN_10268170`) disc-rejection-samples a random point: the X axis always scales by the static
`SpreadAngle`; the Y axis substitutes `const * m_fCurrentRangedAccuracy` **only when the shooter
is `CBasePlayer`**. An NPC therefore always fires on the static authored `SpreadAngle` alone, on
both axes — only the player's cone is skill-warped, and only on one axis of it.

After a valid shot, `CWeaponRanged::Kick` (`0x102397B0`) reads the attacker's raw attack feat,
clamps it to compiled bounds, quadratically interpolates between the active mode's kick bounds,
draws two random signed offsets and applies them through the player view-angle callback. The
Steyr's separate post-frame scope sway is additive state, not the shot kick itself.

*Elysium divergence, owner-called.* Elysium drops the Firearms-feat-driven accuracy curve and the
randomized kick draw entirely, for both the player and NPCs. Firearms skill's only remaining
effect on ranged combat is lethality, via `attack_feat_adjustment` (see "Damage and reaction
boundary" below) — the same shape as Melee/Brawl, which already drives damage rather than to-hit.
In their place:

- **Spread** is a fixed cone per weapon mode: a `Base` half-angle that grows by a fixed `Growth`
  per consecutive shot while the trigger is held, capped at `Max`, and recovers to `Base` over
  `Recovery` seconds after the trigger releases. The bullet still lands at a random point inside
  that cone each shot — only the cone's size is deterministic, not the impact point.
- **Kick** follows a fixed, deterministic per-weapon pattern instead of the random draw: shot 1
  lands near the authored `KickPitchMin`/`KickYawMin`, and sustained automatic fire climbs to
  `KickPitchMax`/`KickYawMax` over a fixed shot count, the same every time a given weapon fires.

Authored `SpreadAngle`/`KickPitch*`/`KickYaw*`/`KickTime` remain the anchors; the values below
replace them for the fixed-cone system. The Mac-10's retail `SpreadAngle 15.0` is reduced — a
fixed cone with no bloom to mask it made the raw authored figure send fire wildly off target,
nearly 4x an Uzi's in the same weapon class:

| Weapon | Spread Base → Growth → Max (°) | Recovery | Kick ramps to Max over |
|---|---|---:|---:|
| Glock 17c | 1.0 → +0.6 → 4.0 | 0.35 s | 3 shots |
| .38 revolver | 1.2 → +0.8 → 3.5 | 0.3 s | 2 shots |
| Desert Eagle | 1.5 → +1.0 → 4.5 | 0.4 s | 2 shots |
| Colt Anaconda (aimed mode) | 1.0 → +0.7 → 3.0 | 0.3 s | 2 shots |
| Colt Anaconda (fan mode) | 2.5 → +1.5 → 6.0 | 0.5 s | 3 shots |
| Uzi | 2.0 → +0.9 → 7.0 | 0.5 s | 5 shots |
| Mac-10 | 2.5 → +1.1 → 8.5 | 0.6 s | 5 shots |
| Steyr AUG (mode 1) | 0.8 → +0.5 → 3.0 | 0.4 s | 5 shots |
| Ithaca M37 | 6.0 fixed, no growth | n/a — dominated by 1.5 s `KickTime` | 1 shot |
| Super shotgun | 5.0 / 7.0 (single/both barrels), no growth | n/a | 1 shot |
| Remington M700 | 0.2 fixed, no growth | instant | 1 shot |
| Crossbow | 0.3 fixed, no growth | instant | 1 shot |

Faithful behaviour — the stat-driven bloom curve and the randomized kick draw — stays recoverable
through git history and the RE record above; no A/B mechanism or cvar toggles between the two.

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
calculating its result; the base-player implementation is empty. Firearm damage does reach the
generic `DamageFlinch` — through the victim's `TraceAttack`, like every other damage source (see
"Damage flinch" below) — but there is no firearm-specific stagger threshold. A remake must not
invent a melee-style firearm stagger meter from these findings.

## Melee attack, combo, block and damage

### Weapon and input surface

Retail distinguishes the always-carried `item_w_unarmed` from the weapon that actually punches.
`CWeaponUnarmed` is not the melee implementation and has no ordinary fist-attack activity table;
`item_w_fists` is `CWeaponMelee_Fists`, inherits `CWeaponMelee`, and carries the full attack,
combo, dodge and block translation surface. Armed melee classes such as baton, knife, baseball
bat, katana and sledgehammer inherit the same request path with their own activity translations.

**Melee is press-edge end to end.** `CWeaponMelee::ItemPostFrame` (`0x103EAEC0`) reads
`m_afButtonPressed` (`+0x208C`), the player's `(last ^ current) & current` edge field — not the
held-button field — and so does `CWeaponMelee::ItemBusyFrame` (`0x10254250`). **One press is one
swing**, and holding the attack key never produces a second one; the semi-automatic
"held-with-an-edge-gate" behaviour belongs to the base `CBaseCombatWeapon` frame that firearms use,
not to melee.

`ItemPostFrame` polls that edge and the weapon's next-attack time, then enters `PrimaryAttack`
(`0x103EACA0`). That body rejects a live grapple — the **paired-action** triple at `owner+0x1538`
(peer handle), `+0x153c` (role) and `+0x1540` (mode), which is the whole of its body-ownership test:
it does not consult `m_hCine`, so a scripted beat does not refuse a press
(`docs/vtmb/animation_and_movers.md` → "Protected activities and player paired-action modes"). It
may start the paired sneak-attack route for a valid target, otherwise requests
`ACT_MELEE_ATTACK`, substitutes `ACT_MELEE_AIR_ATTACK` when the player's *ideal activity* names one
of five airborne states — the test is a switch on `+0xff0`, not the ground flag
(`docs/vtmb/animation_and_movers.md` → "The melee swing bypasses the compact-code dispatch
entirely") — and may fall back to `ACT_KICK` when the weapon capability allows it. The melee
`SecondaryAttack` body at `0x103EAE00` requests `ACT_MELEE_ATTACK_HEAVY`. The class capability
result is `0x40018000`; its `0x18000` portion is also the capability gate used by the player block
resolver.

**No air form of the heavy attack exists.** `SecondaryAttack` substitutes nothing, the compact
code `5` arm names no heavy row, and no recovered weapon ladder declares an airborne heavy base
[data-verified over the ladder corpus]. The grounded `ACT_MELEE_ATTACK_HEAVY` *is* the heavy answer
in the air; the absence is measured, not unexamined.

The command overlap is deliberate and must not be simplified to "attack2 means block":
`+attack2` owns the ordinary weapon-secondary route, while `+wpn_secondaryatk` asserts a second,
dedicated held block bit and then forwards to `+attack2`. The complete button and compact-action
route is in `docs/vtmb/controls.md`.

### `2COMBO` is an activity substitution, not the combo chain

`CWeaponMelee::RequestActivity` (`0x103E9E00`) has one live activity substitution. When the requested
activity is exactly `ACT_MELEE_ATTACK`, it asks virtual `+0x5D4` which base **Ability** controls
the chance: ordinary melee returns slot 6 (`Melee`), while fists override it with slot 1
(`Brawl`). It reads the saved base value through `CVStatList_t::GetBase`; temporary/effect-adjusted
current values do not enter this test. A random integer in `[0, 99]` is compared against this
rank table recovered from `vampire.dll`:

| Base ability rank | 0 | 1 | 2 | 3 | 4 | 5 |
|---:|---:|---:|---:|---:|---:|---:|
| `ACT_MELEE_ATTACK_2COMBO` chance | 0% | 10% | 25% | 45% | 70% | 100% |

On success the function recursively requests `ACT_MELEE_ATTACK_2COMBO`; otherwise it continues
with the ordinary activity. **This substitution is not the combo chain** — that is a separate,
press-driven hand-off documented below — and no movement direction is read in *this* branch.
Direction enters one step later, in sequence selection.

**A `2COMBO` clip terminates a chain.** No `ACT_MELEE_ATTACK_2COMBO` sequence in the corpus names
a chain successor, so an ability roll that promotes the swing to `2COMBO` also ends the player's
ability to extend it.

**An air attack spends no draw at all.** `PrimaryAttack` picks the air form *before* it calls
`RequestActivity`, and the substitution tests the requested activity for equality with
`ACT_MELEE_ATTACK` with the random draw inside that equality branch. `ACT_MELEE_AIR_ATTACK`
therefore never promotes to `ACT_MELEE_ATTACK_2COMBO`, and the ability rank has no bearing on an
airborne swing.

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
candidate order.

### The direction key selects which attack, at swing start

The player selector at `0x10160F90` reads each candidate sequence's authored button mask at
`+0x2D4` and matches it against `m_nButtons & 0x79A` — the movement half of the current button
field, sampled once when the swing starts. The five authored mask values and their meanings:

| Mask | Meaning |
|---:|---|
| `0` | neutral — no direction key held |
| `0x008` | `IN_FORWARD` |
| `0x010` | `IN_BACK` |
| `0x200` | `IN_MOVELEFT` |
| `0x400` | `IN_MOVERIGHT` |

The ranking is: an **exact** match wins outright; failing that a forward/back partial match;
failing that a strafe partial; and failing all three the **zero-mask fallback**, the attack a
neutral press selects, which any state falls back to. `-1` — the mask on 13,898 of the install's
14,012 descriptors — is not a rank at all: it makes a sequence **no candidate for direction-keyed
selection**, reachable only through the ordinary `actweight` path. `0` is a stated mask rather than
an absence, which is why `-1` is the only marker tested. The target argument is not read by this
selector. Per candidate `i`:

```c
mask = seqdesc->+0x2D4;  hit = buttons & mask;
if (mask == 0xFFFFFFFF) continue;                                       // not a candidate
if (mask == 0) { bestZero = i; if ((buttons & 0x79A) == 0) return i; }  // neutral, exact
else if (hit) { if (hit == (buttons & 0x79A)) return i;                 // exact
                if (hit & 0x18) best18 = i; else if (hit & 0x600) best600 = i; }
return best18 >= 0 ? best18 : best600 >= 0 ? best600 : bestZero;
```

**The authored mask is the runtime's own button enum, and nothing remaps it.** The selector ANDs
the player's live `m_nButtons` (`+0x2088`) straight against the file's `+0x2D4`, and the numbering
is stock Source, confirmed three ways inside the same binary: `ItemPostFrame` tests
`m_afButtonPressed & 1` for `IN_ATTACK`; `CHL2_Player::PreThink`'s ladder push tests `& 8` for up
and `& 0x10` for down, i.e. `IN_FORWARD` and `IN_BACK`; and the same block gates the strafe pair on
`& 0x600`. A consumer reads the authored value as a usercmd bit field directly.

**So the attack a swing plays *is* directional**, but the direction is a sequence-selection key
rather than a command: there is no three-direction input state machine, and labels such as `med`,
`low`, `far` and `jump` remain authored clip variants rather than distance tests.

### The combo chain is a press-edge hand-off inside the busy frame

`CBasePlayer::ItemPostFrame` (`0x10174CE0`) routes to the weapon's `ItemBusyFrame` instead of its
ordinary frame whenever `curtime < m_flNextAttack` **or** the busy predicate at `0x10161200` holds.
That predicate is `CBasePlayer` virtual `+0x670`, it is keyed on the **ideal** activity (`+0xff0`),
and it is the single answer three systems ask: this routing test, the player's animation router
(`docs/vtmb/animation_and_movers.md` → "Protected activities and player paired-action modes"), and
the movement substitution that makes a swing lunge (`docs/vtmb/source_movement.md` → "The melee
lock drives the move from the clip"). That third consumer reads it through virtual `+0x674`
(`0x10161430`), which is this predicate OR `m_IdealActivity == ACT_LAND_HARD` and nothing else.
`ACT_LAND_HARD` (`0x32`) matches none of the rows below, so the two virtuals part company only
during a hard landing — where the movement is driven from the animation while a jump is still
permitted.

| Ideal activity | Busy while |
|---|---|
| `ACT_BLOCKED_REACTION_LEFT` `0x1152`, `ACT_BLOCKED_REACTION_RIGHT` `0x1153`, `ACT_BLOCK` `0x52`, `ACT_BLOCK_HEAVY` `0x1156` | `curtime < m_flNextAttack` (`+0x1564`) |
| *every row below additionally requires `m_nSequence >= 0` and `m_flCycle < 1.0`* | |
| `ACT_MELEE_ATTACK` `0x4b` | `cycle < ` the sequence's own `+0x2F8` hold value |
| `ACT_MELEE_AIR_ATTACK` `0x4c`, `ACT_MELEE_ATTACK_2COMBO` `0x4d`, `ACT_MELEE_ATTACK_HEAVY` `0x4e`, `ACT_FEEDING_ENGAGE_FAILURE` `0xfa4` | unconditionally — i.e. for the whole clip |
| flying knockback `0x8b`…`0x93` (`FUN_10344da0`) | unconditionally |
| the knockback family `0x75`…`0x93` or `0x9d0` (`FUN_10161380`) | `cycle <= 0.8` (`_DAT_1047049c`) |
| `ACT_VOMIT_INTO` / `ACT_VOMIT_IDLE` / `ACT_VOMIT_GETOUT` `0x1065`…`0x1067` | unconditionally |

**Only `ACT_MELEE_ATTACK` reads `+0x2F8` at all.** `0x10161200` compares the live cycle against it
with a plain `fld` / `fcomp` pair at `0x10161275` — busy while `cycle < w_hold`, **strictly** — and a
null sequence descriptor is the routine's only guard. There is no substitution, no clamp and no
default arm: the value the predicate tests is the value the file states. The other three player
melee activities never reach that comparison, taking the unconditional arm at `0x1016129b`
(`cmp edi,0x4e / jne / mov al,1 / ret`) instead.

`CWeaponMelee::ItemBusyFrame` (`0x10254250`) is where a chain step is taken. It requires all three
of:

1. a **press edge** on `m_afButtonPressed` — the same edge field the ordinary frame reads, which is
   why a held key never chains and why **an NPC never chains at all** (it has no button field to
   produce an edge);
2. the playing sequence to **name a successor** — `GetChainSequence` reads the descriptor-relative
   name index at `+0x2E8` and resolves it through `LookupSequence`, which is case-insensitive, so
   the successor is a **sequence label** rather than an activity. `+0x2EC` is the alternate
   successor and is the **flying-knockback wall branch**, not a second attack;
3. `IsInComboWindow` (`0x10160DC0`) to accept the current cycle: `+0x2F0 <= cycle <= +0x2F4`,
   **inclusive at both ends**.

When all three hold, the successor sequence is committed at **cycle 0** at the **same playback
rate** — the swing's stored rate at `+0x1488` is reused rather than recomputed — the next-attack
deadline is **not** pushed forward again, and `ForceMeleeReset` clears the previous swing's state.
A chain step is therefore free of recovery cost: the whole chain is paid for by the first swing's
deadline.

**The windows are per-sequence, and one of them is not what it looks like.** `0.5 / 0.9 / 0.91`
(open, close, hold) is the triple the **directional** attacks carry, and the `0.91` hold is stated
by 126 descriptors install-wide — those attacks plus the `-1`-mask successors that continue them,
which open at `0.55` instead. A descriptor stating no window at all states `0.0 / 1.0 / 1.0`
(`docs/vtmb/mdl_v2531.md`), which admits a chain press at any cycle. Authored values differ —
`katana_running_attack` states `0.25 / 1.0 / 0.9`, whose *hold* sits
**below** its close, so the busy predicate releases the clip before its own combo window shuts.

**The hold does double duty.** `+0x2F8` is not only the combo hold: it is the cycle at which
`ACT_MELEE_ATTACK` stops being busy, so it simultaneously releases the busy-frame routing, the
animation reselection block and the movement substitution.

**`1.0` is a value the file states, not a fallback the runtime supplies** [data-verified]. Across
the install's **14,012** sequence descriptors — the loose `models/` tree and the Unofficial Patch
shadowing the VPKs, which is what the game runs — `+0x2F8` reads `1.00` on **13,863** (98.94%),
`0.91` on 126, `0.96` on 12, `0.00` on 7, `0.80` on 2 and `0.90` on 2, and `0.91` is what the
directional attacks carry rather than any default. That `13,863` is not `docs/vtmb/mdl_v2531.md`'s
`13,841`: the latter counts the whole unauthored triple `(0.00, 1.00, 1.00)`, while this counts
`+0x2F8` alone, which is the only one of the three the busy predicate reads — the 22 extra
descriptors state an open/close window and still hold to the end.

**Seven descriptors state a hold of `0.00`, and a strict `cycle < w_hold` makes them never busy.**
They are the single-`idle` scenery and prop models whose whole custom block is zeroed
(`docs/vtmb/mdl_v2531.md`), so no melee activity reaches them and the reading costs nothing in
play — but they are the one population a `1.0` fallback would answer wrongly, which is the sharpest
evidence that the field is read rather than defaulted.

On `baseball.mdl` the non-directional attacks — `attack_heavy_a`, `_heavy`, `_W3`, `_med`, `_low`,
`_far`, all four `*combo`, `dodge_attack` and the eight `stealth_*` — state `0.00 / 1.00 / 1.00`
outright; the directional ones state `0.50 / 0.90 / 0.91`, or `0.55 / 0.90 / 0.91` where the swing
is a `-1`-mask successor continuing one of them, and `baseballbat_attack_jump` states
`0.50 / 0.90 / 0.80`.

A sequence stating the full-clip hold therefore locks all three consumers for its whole clip, where
a directional swing gives control back before the clip ends. Because the predicate separately
requires `cycle < 1.0`, a stated `1.0` makes `cycle < w_hold` and that outer guard the same test, so
`ACT_MELEE_ATTACK` degrades cleanly to the behaviour of the unconditional arms. A heavy finisher is
held twice over — its activity is unconditionally busy, and its descriptor states no window either.

**Four authored chain links are broken, and are preserved as data.** Both sexes' `fists.mdl` chain
`Fists_attack_W2` to a `Fists_attack_W3` the bank never defines, and both sexes' `katana.mdl` chain
`katana_dodge_attack` to `tireiron_attack_med`, a label from a different weapon's bank that the
katana's own bank does not contain. All four are Troika authoring bugs — the chain step simply
fails to resolve — and none is repaired. Ten further links disagree with their target's case and
resolve fine, because the lookup is case-insensitive.

### Target acquisition, sequence commit and recovery

`CWeaponMelee::RequestActivity` performs target acquisition before it commits the selected
sequence. It reads the custom reach float at `+0x2D0` from every sequence returned for the
translated activity and uses the maximum as the query distance. `CBaseCombatCharacter::FindEntityFOV`
(`0x10341C30`) then:

1. traces straight forward with mask `0x46004003` to that maximum reach;
2. accepts a valid obstruction hit first;
3. otherwise scans the reach volume and chooses the visible candidate with the highest forward
   dot product inside a 30-degree half-angle (a 60-degree full cone).

**The authored reach corpus is large and far from any fixed distance** [data-verified]. `+0x2D0`
holds Source units, with `FLT_MAX` as studiomdl's unset marker (`docs/vtmb/mdl_v2531.md`), and
**31,481 exported clip rows across the cast state one**. The per-activity maxima:
`ACT_MELEE_ATTACK_FISTS` 578 cm, `ACT_MELEE_ATTACK_CLAWS` 682 cm, with the armed melee families
between them and `ACT_ANDREI_DIVE_OUT` at 1,015 cm. The reproduction queries the resolved clip's
own authored reach; `ElysiumWeapons::MeleeReachSourceUnits` (64) covers only the cases where no
answer exists — a headless run, a body with no vocabulary, or a row authoring no reach — and that
degraded path reports once per weapon.

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

### Contact is a per-frame swept walk over the clip's own swing records

The ordinary player fist and katana attack sequences in the hash-closed female and male shared
banks have `event_count == 0`. Their damage commit is consequently **not** a firearm-style server
animation event and must not be made an authoritative Unreal montage notify merely because that is
convenient. VtMB does have special creature/event attacks that construct a trace from a staged
opponent, but that is a separate path and is not evidence for ordinary fists or weapons.

**The swing caller is `CBaseCombatCharacter::MeleeSwingUpdate` (`0x10346CD0`, vt `+0x4EC`)**, and it
runs **every frame** from `UpdateCharacter` (`0x103246D0`) — reached for the player through
`CPlayerMove::RunPostThink` → `CBasePlayer::PostThink`, and for an NPC through
`CAI_BaseNPCTroika`'s own think. There is no start event and no stop event:

> **A swing is live exactly while the currently playing sequence declares at least one swing
> record.** The two writers of `m_bMeleeSwingIsLive` (`+0xAA1`) are the only ones in the image, and
> both derive it from that count. The records *are* the window.

Each frame the update sub-steps the elapsed time: `N = floor(dt * 100)` against a constant `100.0`
at `0x10450564`, producing `N` **contiguous** cycle intervals covering everything the clip advanced
through. `MeleeSwingStep` (`0x10343020`) then, for each record and each sub-interval, tests the
record's authored `[start, end]` window against that sub-interval, and where they overlap sweeps
the record's bone-local segment — transformed into world space by that bone's matrix through
`VectorTransform` — committing any hit through the weapon's traced-impact virtual `vt +0x438` =
`0x102579F0`.

**The `N < 1` branch is unreachable in retail's own environment, and reproducing it literally is a
defect.** That branch jumps straight into the epilogue stores, which overwrite the stored timestamp
(`+0xAA4`, one writer image-wide), position and angles and discard the short span. But the clock is
server `curtime`, which advances in fixed ticks, and the update's `dt <= 0` early exit is the one
path that does *not* write the timestamp — so a call either sees `dt == 0` and stores nothing, or
sees a whole tick and walks it. **The observable is tick-batched walking**, not span-dropping; a
literal port makes melee stop landing above 100 fps, a behaviour retail's clock cannot produce.
(The reproduction adds two guards of its own on top of that reconciliation —
`ElysiumSwing::MaxBatchSeconds` and `MaxBatchTravelCm` — which bound a hitched frame's sub-step
count and refuse to sweep through an engine discontinuity such as a teleport. Retail's server never
hands its update either case, so neither stands in for a recovered rule.)

**Hit-once is per record, with a spread.** Each record carries its own victim hit list. A landed hit
marks the victim in **every record whose window overlaps the hitting record's**, so a swing whose
records share one window lands once; a record's list clears when its window closes, which is what
lets a `2COMBO`'s two disjoint record groups land **twice** on the same victim. This is the
recovered answer to the `2COMBO` contact-count question.

**The opposed roll and the incoming-swing notice stage once, before any contact.** On the swing's
first live frame — ahead of every window test — `SendIncomingSwingNotice` (`0x10346AC0`) runs its
own `FindEntityFOV` at **60 units** and a half-cone dot of **0.7**, with
`MeleeRollAndSendNotice` (`0x10346830`) as the per-candidate predicate that calculates and stores
the opposed record. That query is **not** the acquisition query of the previous section: acquisition
uses the authored per-sequence reach and a 30-degree half-angle and reserves an opponent for aim
assistance, while this one selects whom the swing's opposed record is staged against. Collapsing
them makes the roll follow the aim reservation, which is not what the bytes do.

`ForceMeleeReset` (`0x10346760`) clears the per-swing state — **including the opposed record count
at `+0xA94`** — at every swing start and every chain step, so a record cannot outlive the swing
that made it.

Compiled constants of the walk:

| Constant | Value | Role |
|---|---:|---|
| sub-step rate | `100.0` Hz | `N = floor(dt * 100)` |
| segment subdivision | `1/6` unit | how finely the swept segment is subdivided |
| `melee_swish_sound_time_offset` | `-0.1` s | the whoosh **leads** the contact window |
| `melee_swing_completion_percent` | `0.8` | at this cycle the victim's hit-buildup counter is cleared |
| `IsMeleeSwingInRange` | reach `+ 100` | a **2D** (horizontal) distance test |

#### No shipped sequence authors a melee commit event

A corpus-wide census of all **4,445** models settles what the 3000 band actually carries:

| Id | Authored by |
|---:|---|
| 3001 | 4+ sequences; the only retail animation-event **melee commit** is `CNPC_VDog`'s bite on it |
| 3002 | viewmodels only |
| 3031 | **105** sequences on `move_and_ranged` (53 male, 50 female) — the generic third-person shot carrier, and the **only** member of the 3030–3044 band any clip authors |
| 3047 | **no shipped sequence at all** |
| 3200 | lockpick reference only |

`CWeaponMelee::Operator_HandleAnimEvent` (`0x103EA5B0`, vt `+0x5C8`) accepts 3001, 3030–3037,
3039–3044 and 3047 — 3038 falls through to the base body and 3003 is silently swallowed — and its
tail calls `PrimaryAttack` **only when operator `+0xA8 == 0`**, the `CBasePlayer` self-pointer, so
3047 is an **NPC swing trigger** rather than a damage commit. Nothing authors it. `CWeaponRanged`
(`0x10238160`) accepts the whole 3030–3044 band identically.

`WEAPON_MELEE_BEGIN_SWING` / `WEAPON_MELEE_END_SWING` (3200 / 3201) are **vestigial**: no consumer
in the image, and no authored clip. A remake must not build a melee window out of them.

### Opposed record and reaction margin

Before impact, `CBaseCombatCharacter::CalcAndStore...` appends a 16-byte record to a result array
at `+0xA88`, whose active count is at `+0xA94`, and stores:

| Word | Stored value |
|---:|---|
| 0 | the other party's entity handle |
| 1 | active weapon's total lethality |
| 2 | defender `Defensive_Maneuvers` net successes, plus any bounded defense bonus |
| 3 | defender soak successes selected from the weapon's active `CVDmg_t` |

**The array lives on the ATTACKER and is keyed by defender.** `ForceMeleeReset` (`0x10346760`) is
what settles the ownership: it zeroes the count at `+0xA94` at every swing start and every chain
step, which only makes sense for a per-swing array on the swinging character — a defender-side
array would be cleared by its own next swing rather than by the attacker's.

`CBaseCombatCharacter::GetMeleeDiceRolls` searches the array and returns the matching record;
`GetNumAttackSuccesses` returns its word 1. The signed reaction margin is
`lethality - defense - soak`. Custom NPC `RunTask` handlers and the player block path use the
five-way classifier at `0x103498B0`.

**The reproduction stores the record defender-side and scopes it by swing serial** — an explicit
owner call, recorded here beside the faithful behaviour. `FElysiumMeleeRoll::SwingSerial`
(`Source/ElysiumUE/Public/ElysiumPlayer.h`) stamps each staged record with the accepted-swing
serial the attacking weapon was on, and a reader naming a different serial is refused. That reaches
the same observable as `ForceMeleeReset` from the reader's end — the moment a new swing is
accepted, every record the previous one staged stops answering — without a clear pass that would
have to reach every body the attacker might touch.

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

**The frontal test is the open forward hemisphere** [VtMB decompiled]. The defender is
`WasMeleeBlocked`'s *argument*, not its `this` — the argument is what takes the block-capable
virtual `+0x510` and `IsHoldingMeleeWeapon`. The test dots the defender's forward vector,
`AngleVectors(defender->GetAbsAngles())` from virtual `+0x36c`, against the **normalized**
attacker-minus-defender delta built from virtual `+0x364`, and admits the block on
**`dot > 0` strictly**. The comparand at `0x104454C4` is **`0.0f`**, byte-identical in the patched
`vampire.dll` and in `vampire.dll.12`, so there is no cone angle: the half-angle is exactly 90° and
an attacker standing exactly abeam is **refused**. Two details complete it:

- the dot is **3D**, including the vertical term, but `GetAbsAngles` carries no pitch on a combat
  character, so `forward.z` is zero and the sign of the 3D dot equals the sign of the planar
  bearing;
- the delta is normalized in place by a `Vector` method that returns its length, and that length is
  compared against `0x1049E024` = **`1e-4f`** first. At or below that separation the facing test is
  **skipped entirely and the block is allowed** — coincident origins name no direction, and retail
  resolves that in the defender's favour.

On a blocked contact, the defender callback at `0x10160BC0` rolls bonus soak dice from attribute
slot 2 (`Dexterity`) through `AddBonusSoakRolls` (`0x10349710`), which adds them into the opposed
record's **word 3**, then classifies the updated record. **In the shipped call ordering those dice
do not change the triggering hit's damage** — the damage descriptor has already been resolved — so
what blocking buys is the *reaction classification* (`ACT_BLOCK` against `ACT_BLOCK_HEAVY`) and
nothing else. Blocking grants **no defense-roll bonus**: the only recovered modifier on the
defence roll is the anti-move-spam bonus, whose own diagnostic reads "player is using the same move
on this NPC" (`0x10623d18`).

Class 3, the **defender block stagger** band, plays `ACT_BLOCK_HEAVY`; the other blocked
classes play `ACT_BLOCK`. The attacker callback at `0x10160D00` plays the blocked-reaction
activity stored at `+0x2E0` in the attacker's current sequence descriptor, falling back to
`ACT_BLOCKED_REACTION_RIGHT`. `+0x2E0` is the **load-time enum slot**, `-1` on every descriptor on
disk; the DLL resolves it at model load from the descriptor's own name index at `+0x2E4`
(`docs/vtmb/mdl_v2531.md`). This is where authored left/right blocked reactions enter; they are
not chosen from a movement direction at input time.

If the updated record is not damaging, the impact exits without damage. If positive damage
remains, the damage path continues even though block reactions played. Thus **blocked** does not
mean **zero damage**. A stronger unblocked result takes the separate normal-hit or knockback
callbacks; the player knockback body selects a reaction sequence and applies impulse/timing.
There is no recovered standalone player `STAGGER` command or compact action: the concrete melee
stagger is the heavy-block reaction band, with hit/knockback as a separate outcome.

**The authored reaction vocabulary is exactly two literals** [data-verified]. Across the exported
clip tables **10,077 rows carry a `blocked_reaction`** — `ACT_BLOCKED_REACTION_LEFT` 5,892 and
`ACT_BLOCKED_REACTION_RIGHT` 4,185, and nothing else — and every one of them sits on a melee-attack
row (`ACT_MELEE_ATTACK*`, including the `2COMBO` and `MELEE_AIR_ATTACK` families). The stored value
is always the **base** spelling; the weapon ladder supplies the armed variant, so a descriptor
never stores a weapon-suffixed reaction.

**Where the reproduction departs from that record.** Each is an explicit owner call, taken with
the blocked/stagger family and recorded here beside the behaviour it departs from:

- **The frontal test's two boundaries.** `ElysiumReactions::IsFrontalContact` reproduces the
  recovered hemisphere as a planar `|bearing| <= 90°`, which agrees with retail everywhere except
  at the boundary itself: it **admits** an attacker standing exactly abeam where retail refuses it,
  and it **refuses** a horizontally coincident attacker where retail allows the block. The planar
  reading is exact for the rest of the fan because the defender's absolute angles carry no pitch.
- **The `Dexterity` bonus-soak re-roll at `0x10160BC0` is left to the damage/soak system.** This
  rung owns the pose, not the number — which costs nothing against retail, where those dice do not
  change the triggering hit either.
- **The flinch occupies the base channel where retail's `DamageFlinch` is a 3-slot gesture
  overlay.** A melee block or stagger reaction therefore *holds* the base channel for its held
  seconds, and the flinch yields to it without advancing the `Reaction` RNG stream
  (`docs/architecture/save-architecture.md` § 8). Both are consequences of the channel
  substitution rather than recovered rules. The envelope the substitution carries is the recovered
  one — the 0.1 s / 0.3 s fade pair of "Damage flinch" below, in seconds. Which contacts reach the
  flinch at all is not affected by the substitution: the flinch is requested from `TraceAttack`, not
  from `OnTakeDamage_Alive`, so a blocked **non-damaging** melee contact never reaches
  `DispatchTraceAttack` and therefore never flinches, while a fully-soaked damaging one does.
  What the substitution costs on the way out is named: retail's overlay never touches the base
  cycle, while a base-channel reaction hands the resumed gait the cycle it was left at
  ([animation_and_movers.md](animation_and_movers.md) → "The cycle is never stored, and only a gait
  inherits one").
- **A player blocking into the hit/knockback margin plays no defender pose**, while the attacker
  still plays its blocked reaction — the chosen reading of `WasMeleeBlocked` gating both callbacks
  off one predicate.

### Knockback

The hit/knockback band above is the only reaction that moves a body. Its whole chain is recovered.

#### The authored table lives in the swing record, not in an item file

Each 188-byte swing record (`docs/vtmb/mdl_v2531.md`, `docs/vtmb/animation_and_movers.md` A.3)
carries, in bytes `0x28`–`0xBA`, **four direction buckets of up to four candidate knockback
activity names each** — name indices at `0x78`, `0x88`, `0x98` and `0xA8`, record-relative, with the
sixteen resolved enum slots at `0x38` and the four per-bucket candidate counts at `0x28`. Retail
draws one candidate with `RandomInt`.

Two single bytes complete it:

- **`0xB8` is the direction bucket 0 answers.** The four directions cycle `BACK, LEFT, FORWARD,
  RIGHT`, and bucket `k` answers direction `(byte_b8 + k) mod 4`. This holds on **948 of 948**
  records that fill every bucket, so the buckets are a rotation rather than a fixed order and a
  consumer must read the byte.
- **`0xBA == 2` is the unconditional marker**, which admits the knockback past the victim's
  hit-buildup gate below.

#### Launch is a velocity assignment, in two stages

There is no impulse and no physics solve. The NPC reaction body (`0x102a01b0`) computes
`m_KnockbackVelocity` (`+0x6004`) **only** on the flying branch — `IsFlyingKnockbackActivity`, i.e.
`0x8a < act < 0x94` — and schedules `SCHED_TROIKA_FLYING_KNOCKBACK` (`0x14d`). On the next think,
`TASK_MELEE_FLYING_KNOCKBACK_INTO` calls `SetAbsVelocity(m_KnockbackVelocity)`, a straight
assignment.

Magnitude interpolates on `t = GetRawAttackValue * 0.1` (constants at `0x1049a1d8`–`0x1049a1e4`):

```text
horizontal = 220 + (400 - 220) * t
vertical   = 200 + (310 - 200) * t
```

`t` is `1.0` when there is no attacker, and is forced to `0.1` for a chain reaction. **Neither the
inputs nor the result are clamped.**

Direction is `normalize(victim - attacker)` with z zeroed. With no attacker it is the victim's own
negated facing — a body with nothing to be thrown away from goes straight backwards. In a chain
reaction it is the attacker's velocity instead.

#### Classification, yaw snap and the cell

The classifier cuts `AngleMod(victimYaw - awayYaw)` into four bands. They are **not symmetric**, and
the asymmetry is authored rather than rounding:

| Relative yaw (degrees) | Bucket | Direction | Width |
|---|---:|---|---:|
| `> 316` or `<= 45` | 2 | `FORWARD` | 89 |
| `<= 135` | 3 | `RIGHT` | 90 |
| `135 <` … `<= 225` | 0 | `BACK` | 90 |
| otherwise (`225 <` … `<= 316`) | 1 | `LEFT` | 91 |

Retail then **snaps the victim's yaw** to `AngleMod(awayYaw + offset)` with offsets
`{0: +180, 1: +270, 2: +0, 3: +90}` in Source yaw, so the authored clip's model-space direction
points along the travel. Two gates on that snap:

- the snap is **gated on victim `+0xA8 == 0`** — the `CBasePlayer` self-pointer — so **NPCs snap and
  players never do**;
- the flying path forces direction bucket 0 (a `+180` offset) regardless of the classification.

**The body-goes token convention is CONFIRMED**: an `..._BACK` cell plays when the body travels
backwards, i.e. for a blow to the face. When no candidate list is consulted the fallback activity is
`0x8B`, flying-into-forward, and `TranslateFlyingKnockback` downgrades the flying range
`0x8b..0x8e` to the grounded `0x87..0x8a` when victim `+0xA8 != 0`.

#### Who may be knocked back

**The player is never launched.** `CBasePlayer`'s reaction (`0x101606e0`) is an activity, a forced
switch to `item_w_unarmed` on capability `0x6000`, and a `ViewPunch` scaled by
`player_damage_kick_knockback_scalar` (`10.0`) times the reaction sequence's duration, plus
`m_fNoAttackTimer` (`knockback_no_attack_time`, `1.0`) and `m_fPreventKnockbackTime`. `ShouldKnockback`
refuses a player whose weapon capability intersects `0x6000` unless the attacker's own NPC template
authors `KnockbackRangedPlayer` (`+0xA0`).

`Disallow_Knockbacks` (template `+0x9E`) is authored `"1"` on **8** `npctemplate*.txt` files.

**The NPC gate is a counter, not a chance.** A knockback is admitted when
`m_iHitBuildupCount` (`+0x6064`) `<= npc_hit_buildup_amount` — a ConVar, default `"2"` — **or** the
swing record's `0xBA == 2` marker is set. The counter is incremented per qualifying hit
(`0x1029F800`) and reset at `melee_swing_completion_percent`. **Whether the count is per victim or
per attacker/victim pair is OPEN**; that is the observable behind the widely reported "about two
hits and then the NPC parries" feel.

#### How a flying chain ends

The `_INTO` / `_LAND` / `_WALL_LAND` clips end on sequence completion. The flight itself ends on
**land detection** — the body is grounded, or it is falling and a ground probe succeeds. Wall
contact diverts instead: the rebound velocity is the wall vector times `100.0`, a timer is armed at
`curtime + 0.01`, and the chain enters `..._WALL_FALL`.

#### The discipline knockback path

A discipline reaches knockback through its `HitInfo` block, and the two keys it may author are
different mechanisms, not two settings of one.

**`Knockback` enters the shared chain.** The key is parsed as an integer percent into `HitInfo`
`+0x4C` (`0x101DDFB0`); the apply body (`0x101DE660`) tests it for non-zero only and calls the
shared knockback entry `0x10344F80` with an away-vector it builds from the source and target
origins. **The authored percentage is not forwarded** — `0x10344F80` takes no magnitude argument,
so `"50%"` and `"100%"` are the same instruction. From there the hit runs the ordinary chain:
direction classification, `LookupActivity`, `TranslateFlyingKnockbackActivity` when victim `+0xA8
!= 0`, the yaw snap when `+0xA8 == 0`, then the reaction virtual `+0x500`.

**`AI_Schedule` bypasses the chain entirely.** The key is stored as a **string** at `HitInfo`
`+0x34`, and the apply body resolves it by name at hit time — no schedule id appears in the image
for these — then forces it with `SetSchedule(id, false)`. Two of the schedules it can name carry
`TASK_SET_KNOCKBACK_ACTIVITY`:

| Schedule | id | Named by |
|---|---:|---|
| `SCHED_TROIKA_D_BLOODSHOT_KNOCKBACK` | `0x144` | Thaumaturgy **Blood Strike**, `Hit_Human` and `Hit_Strata_2` |
| `SCHED_TROIKA_D_BURROWING_BEETLE` | `0x134` | Animalism **Burrowing Beetle**, `Hit_Human` (inherited by `Hit_Supernatural`, `Hit_Strata_1`, `Hit_Strata_2`) |

Both are the same program, and both pass the same activity:

```text
TASK_MAKE_OBLIVIOUS           TRUE
TASK_SET_NPC_FLAG             NPCFlag:D_IS_BUSY     (beetle: TASK_SET_FAIL_SCHEDULE SCHEDULE:Idle_Stand)
TASK_SET_PRESERVE_PATH        0
TASK_STOP_MOVING              0
TASK_SET_KNOCKBACK_ACTIVITY   ACTIVITY:ACT_KNOCKBACK_SMALLLOW
TASK_ADD_EVENT_EXPRESSION     EXPRESSION:KNOCKBACK
TASK_MELEE_KNOCKBACK          0
Interrupts                                          (empty — uninterruptible)
```

- **`TASK_SET_KNOCKBACK_ACTIVITY` is task `0xF1`**, and its whole body is `m_knockbackType =
  activity; TaskComplete()`. It exists because the discipline path never enters the melee reaction
  body `0x102A01B0`, which is what normally fills `m_knockbackType` from the swing-record candidate
  table; without it the task below would replay whatever the last melee knockback left behind.
- **`TASK_MELEE_KNOCKBACK` is task `0x92`**. Its start clears all three flinch/gesture layer slots
  (`m_Flinch_0..2` sequence `-1`, via the NPC virtual `+0x428`) and then *restarts* the ideal
  activity — so a knockback cancels an in-flight `DamageFlinch` gesture rather than layering over
  it. Its run phase is `AutoMovement()` each think, completing when the sequence finishes.
- **Nothing here launches a body.** `m_KnockbackVelocity` is computed only in `0x102A01B0`, there is
  no direction classification and no yaw snap, and `ACT_KNOCKBACK_SMALLLOW` (`0x75`) fails
  `IsFlyingKnockbackActivity` (`0x8A < act < 0x94`) in any case. `0x75`–`0x80` are the legacy
  `SMALLLOW` / `SMALLHIGH` / `BIG*` band that precedes the directional `SMALL_` / `NORMAL_` /
  `FLYING_` sets at `0x81`–`0x93`.

The authored data keeps the two mechanisms disjoint: every strata that names `AI_Schedule` leaves
`Knockback` unset, and every strata that sets `Knockback "50%"` has its `AI_Schedule` line
commented out (Blood Strike `Hit_Strata_3/4/5` and `Hit_Boss`, Burrowing Beetle `Hit_Strata_3/5`).
Retail (`pack101.vpk`, `vdata/system/disciplinetgt_000.txt` and `_004.txt`) and the patch author
these lines identically.

**Both knockback entries force the frame.** `0x102A01B0` and `0x101DE660` each call the NPC virtual
`+0x998` immediately before setting the schedule; it stamps `curtime` into `m_flNextThink`,
`m_flNextUpdateThink`, `m_flNextNormalThink`, `m_flNextMoveThink` and `m_flNextAIThink`, so the new
schedule starts on the current frame instead of the next AI tick.

#### `COND_KNOCKBACK` is a dead condition

`COND_KNOCKBACK` is condition `0x28` in the retail registry (`0x102C8CE0`), and **nothing sets it**.
`CAI_BaseNPC::SetCondition` (`0x10269A20`) is the only writer that introduces a condition id; across
every call site in `vampire.dll` its argument is a literal in `0x01`–`0x7E` or one of four computed
values, and `0x28` is not among them. The four computed sites are `GatherAttackConditions`
(`0x1026DD10`, feeding `0x2F` / `0x4F` / `0x51` / `0x63` from weapon results), the delayed-condition
queue drain (`0x102CC760`, whose two queues only ever receive `0x0B` and the `COND_HEAR_*` family)
and `CNPC_VMingXiao::GatherConditions` (`0x77`–`0x7E`). No task sets conditions either — the
330-entry task registry has no `TASK_SET_CONDITION`.

Its consumers are therefore unreachable: `CAI_BaseNPCTroika::SelectSchedule` (`0x102AE920`) tests it
in both `NPC_STATE_ALERT` and `NPC_STATE_COMBAT`, returning `SCHED_TROIKA_KNOCKBACK` (`0x14C`)
either way, and three schedules list it as an interrupt (`SCHED_TROIKA_MELEE_IDLE`,
`..._IDLE_STAND_STILL`, `..._IDLE_FRENZY`).

The reason is structural rather than an oversight: `0x102A01B0` delivers the knockback by forcing
the schedule directly, so the condition-to-selector round trip the registry entry was written for is
bypassed. This is the same shape as `knockback_chance` above — a retail authoring that the shipped
code never wired. A remake gains nothing by producing the condition, and reproducing the selector
branches that read it reproduces dead code.

`EXPRESSION:KNOCKBACK` in the schedule text is unrelated: it is a facial-expression token consumed
by `TASK_ADD_EVENT_EXPRESSION`.

#### The reproduction's grounded stand-ins

Each is recorded beside the retail fact it stands in for
(`Source/ElysiumUE/Private/Substrate/ElysiumReactions.h`):

- **One deterministic candidate per bucket.** `StandInKnockbackSize` / `StandInKnockbackHeight`
  select the `NORMAL`/`HIGH` cell of the classified direction, with no draw at all, pending the
  authored swing-record table above being consumed. The `SMALL` family and the two `LOW_BACK` cells
  stay in the vocabulary; nothing reaches them yet.
- **The hit-buildup gate is omitted, not guessed.** `IsKnockbackAllowed` takes the alive filter and
  `Disallow_Knockbacks` only, and the producer reports the omission once. A victim retail would have
  spared until its counter drained is knocked back here.
- **The second template predicate is omitted** for the same reason — it is unidentified, and a
  guessed predicate would refuse knockbacks the content asks for.
- **Only the grounded cells are produced.** The flying chain and the launch assignment are not
  reproduced, so nothing here moves a body.

### Melee damage commit

The shared weapon traced-impact body at `0x102579F0` is the downstream melee consumer. It
occupies the same base-weapon virtual in melee and ranged weapon vtables, so
`CWeaponMelee::MeleeImpact` is a useful semantic label for the melee branch, not a recovered class
or symbol name.
It copies the active mode's `CVDmg_t`, resolves the stored attack record and block reactions,
marks the descriptor's direct-damage route, and carries it through the common descriptor/soak
path. The remaining `DamageInflicted` is floored up to the active Potence rank when Potence is
higher. Retail's own diagnostic string and the body agree on the final scalar:

```text
Total = DamageInflicted * (BaseDamage + DamageModifier) * Multiplier
```

`BaseDamage` is descriptor word 1. **`DamageModifier` is the `Dmg` string's optional leading
trait** — the `CVStatRef` at words 5..8, evaluated on the attacker — and it is **`0` on every real
weapon in the game**: only `holy_light`, `wolf_head` and developer items author one. It is not the
attacker's Brawl/Melee feat rating; the attack feat enters lethality, not this term.
`Multiplier` is the trace/volley scale from the `CTakeDamageInfo` envelope **times descriptor word
15**, the accumulated damage-filter float.

Total lethality itself is:

```text
Total Lethality = max(BaseLethality + GetRawAttackValue, 0)
```

where `BaseLethality` is the weapon mode's authored value at `+0x3f8` and `GetRawAttackValue` is
the evaluated attack feat named by `CVDmg_t` word 9 — retail's own diagnostic calls that term
**"Feat Adjustment"**.

**SUSPECTED double-soak — recovered but not to be reproduced yet.** The melee margin subtracts the
opposed record's `soak`, and the *same* value is then carried into `CVDmg_t::Apply` as the forced
soak (word 14), so the defender's soak appears to be spent twice on one blow. The read is
consistent across both call sites, but a mis-decoded aliasing of one local would produce exactly
this reading, and the difference is large enough to change every melee outcome. **One live capture
of a melee hit with a known soak value settles it**; until then a remake reproduces a single soak
subtraction and records the discrepancy rather than doubling it.

Naming each individual damage filter's arithmetic remains open.

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

**The reproduction carries that memory as a derived, unsaved hostility row** — a mechanism note
beside the faithful lifetime, not a behavioural divergence. `ElysiumNpcEnemy::RememberAttacker`
(`Source/ElysiumUE/Private/Substrate/ElysiumNpcEnemy.h`) writes a `D_HT` relationship toward the
attacker at priority 5, expiring after `DamageMemorySeconds = 5.0` and re-stamped (not extended) by
every qualifying hit. Retail keeps a per-actor enemy-memory component that `BestEnemy` walks; this
runtime has no such component, so the relationship table is the eligibility surface a remembered
attacker has to reach. The lifetime is retail's; the store is ours, and a persistent authored row
at a higher priority still supersedes it.

Five reaction concepts are independent:

| Reaction | Authority |
|---|---|
| melee block stagger | the opposed margin's heavy-block band; selects `ACT_BLOCK_HEAVY` |
| melee hit/knockback | the stronger unblocked outcome; a swing-record-selected activity, a yaw snap and, on the flying branch, an assigned velocity |
| light/heavy/repeated damage | AI conditions that can interrupt a schedule and select a flinch/cover response |
| generic damage flinch | `DamageFlinch` (`0x103229d0`), requested from `TraceAttack`; a 0.1/0.3 s gesture triangle in its own 3-slot array |
| death | `Health >= Max_Health`, life-state transition and `Event_Killed` |

### Damage flinch

`CBaseCombatCharacter::DamageFlinch` (`0x103229d0`) is vtable slot 292 (`vt +0x490`) and **no leaf
class overrides it**, so every combat character flinches the same way.

**The rule.** The activity is `0x74 - (RandomInt(0,1) != 0)` — `ACT_HIT_TORSO` or `ACT_HIT_HEAD`, an
even coin. `hit_yaw` is `actorYaw - atan2(incoming) in degrees + RandomFloat(-30, +30)`, where the
`atan2` term is **skipped entirely unless `dir.x > 0 || dir.y > 0`**. It then calls
`AddFlinchGesture` through `vt +0x424` with `(activity, 0.1f, 0.3f, "hit_yaw", yaw)`.

**The two fades are seconds, and they are compiled constants.** They are not normalized fractions:
the SendProps quantize the pair over `0..4.0`, and the client computes the weight as
`w = (t - start) / fadeIn` on the way up and `(start + in + out - t) / fadeOut` on the way down —
**a plateau-free linear triangle**. So a flinch peaks at **0.1 s** and is gone at **0.4 s**, and it
is evaluated at cycle **literal 0**: the gesture is a static pose steered by `hit_yaw`, not a
playing clip.

**It lives in its own array.** The flinch slots are three entries of stride `0x1C` at `+0x7F4`, a
**separate structure** from `m_AnimOverlay` (four entries of stride `0x30` at `+0x734`); `+0x730` is
`m_bNoFlinch`. The array is newest-on-top by start time, evicts the oldest-expiring entry when
full, is **ended by the clock only**, and a slot is never cleared — an expired entry is simply
weighted to zero until something overwrites it.

`m_bNoFlinch` has exactly one writer in the image: `CNPC_VMingXiao`'s constructor.

**Where the flinch is requested from — and where it is not.** Both callers are in `TraceAttack`, at
the **top** of `CBasePlayer`'s (`0x10162c30`) and the **bottom** of `CAI_BaseNPC`'s (`0x10266780`),
and **both run before soak and before the health commit**. `OnTakeDamage_Alive` (`0x103302e0`)
never calls it. Two consequences follow directly:

- a **blocked and non-damaging** melee contact skips `DispatchTraceAttack` altogether, so it does
  not flinch;
- a hit that is **fully soaked** still flinches, because the flinch was already requested when the
  soak ran.

The AI's `SMALL_FLINCH` schedule is a separate owner: it remembers flinched state, stops movement
and runs `TASK_SMALL_FLINCH`; alert AI can instead take cover from the attack origin. A remake must
not invent one universal stagger meter or make every positive hit cancel the current action.

**The reproduction plays the flinch on the base channel** where retail overlays it — an explicit
owner call, recorded here beside the faithful behaviour, and detailed under "Block and stagger
reactions" above. The envelope it carries is the recovered 0.1 s / 0.3 s triangle
(`ElysiumReactions::FlinchBlendInSeconds` / `FlinchBlendOutSeconds`).

## NPC and player death transaction

The shared `CBaseCombatCharacter::Event_Killed` body (`0x1032b9b0`) sets life state 1 (dying),
cleans weapon, effect and ownership state, constructs the ragdoll-force envelope, and notifies the
killer and game rules. The NPC override at `0x10265ad0` is schedule-aware:

- an NPC whose current schedule is `GetScheduleOfType(0x3a)` — schedule type **`NPC_FREEZE`** —
  refuses the kill and returns. This guard is about being frozen, not about already dying;
- a **started** scripted sequence defers the kill packet, otherwise the owning sequence is
  cancelled; the deferral test and its resume are owned by
  [npc-ai-reverse-engineering.md](npc-ai-reverse-engineering.md);
- `OnDeath` fires once through the native guard at `+0x5bd4`;
- current and ideal NPC state become 7 (dead), strategy and squad claims are vacated, and the
  carcass-sound / corpse-fade fork runs; the death schedule itself is chosen later, by the
  dead-state selector.

The Troika NPC override (`0x102bf340`) additionally releases hints and feed/claim ownership,
notifies owner/maker systems, invokes Python `MarkAsDead('<targetname>')`, and updates its special
partner/owner memory. These are consequences of the one death commit; a maker child count must not
be decremented from a hit or flinch path.

### The two authored death-policy keys

`npctemplate*.txt` states `Disallow_Kindred_Death` on **9** templates and `Has_Burning_Death` on
**32** (out of 41 and 34 authorings respectively; the rest are explicit zeros). Both are parsed
into the character-template record by the same loader on server and client (`0x101d4520` /
client `0x1013ec60`), inherited by the template copy (`0x101d3c10`), and reached through
`GetCharTemplate()` into the template array:

| Key | Template offset |
|---|---|
| `Has_Burning_Death` | `+0x98` |
| `Disallow_Kindred_Death` | `+0x9d` |

Their **single consumer** is the predicate at `0x10207df0`, which is exactly

```
ShouldBurnOnDeath(ent) = template[0x98] || (IsKindred(ent) && !template[0x9d])
```

and its only caller is `CBaseCombatCharacter::CreateCorpse`. `Has_Burning_Death` short-circuits
true for anyone; `Disallow_Kindred_Death` only ever suppresses the Kindred half. A true result
gives the corpse a material override (vtable `+0x3d0` producing the value passed to `+0x3cc`),
puts it on the burn think at `curtime + 10.0`, and plays
`character/vampire burning death.wav` through a `CPASAttenuationFilter` at 0.8 attenuation. A
false result puts the corpse on the ordinary corpse think at the same `curtime + 10.0`.

The player damage wrapper (`0x10163020`) adds player-specific refusal/protection gates and tears
down conversation, use, grapple and special-control state before/around the shared commit. The
player death override (`0x10163af0`) stops active weapon/controllers, notifies game rules, selects
the death action/screen from `vdata/Signs/death.txt`, and composes the shared combat-character
cleanup. NPCs and the player share the authoritative damage counter and lethal comparison; their
outer AI, I/O and presentation lifecycles are deliberately different.

### The ragdoll-force envelope

`CBaseCombatCharacter::Event_Killed` builds one force vector and hands it to the corpse
constructor. In order:

1. Take `info`'s damage force. **If its length is `<= 0.0`, replace it** with
   `CalcDamageForceVector` (`0x1032b290`) — a zero force is synthesised, not carried.
2. Add the character's absolute velocity.
3. When the physics ConVar is on and `m_pPhysicsObject` exists, add the physics object's velocity
   and write the sum back with `SetLocalVelocity`.
4. **Clamp the magnitude to `50000.0`**, announced by
   `DevMsg(1, "Clamping ragdoll force from %.2f to %.2f.\n")`.
5. Call `CreateCorpse(force, info)` through vtable `+0x4b4`.

`CalcDamageForceVector` picks a magnitude and a direction independently.

- **Magnitude** is the attacker's active weapon's own force when it has one; otherwise
  `damage * 150.0`, where `damage` is `CVDmg_t::GetDmg` when the descriptor is present and the
  scalar damage field otherwise. A weapon whose capability bits intersect `0x18000` adds a
  stat-scaled random term drawn from two `CVStatList_t` entries.
- **Direction** is one of four cases: `DMG_BLAST` (`0x40`) takes inflictor→victim scaled by
  `1.5`; a self-inflicted kill takes the victim's own forward vector scaled by `-1.0`; an
  inflictor in `MOVETYPE_VPHYSICS` (7) takes that object's velocity, and its **mass replaces the
  magnitude**; everything else takes inflictor→victim through a ConVar-scaled height offset.
- With neither inflictor nor attacker the whole result is `vec3_origin`.

The result is then **multiplied component-wise by a per-axis scale vector carried in the damage
record at `+0x04 … +0x0c`**. This is why the scalar `TakeDamage` I/O input produces no impulse: it
zeroes those three floats immediately after construction, so the synthesised replacement force
collapses to zero as well.

### Corpse construction and the solid-body policy

A corpse is not made non-solid by one rule. Four stages apply in sequence.

**`BecomeDead` (`0x10265a40`)**, reached from the NPC `Event_Killed` when `GetFlags() & FL_NPC`
(`0x2000`) after `SetTouch(NULL)`: `m_iHealth = m_iMaxHealth / 2`, `m_iMaxHealth = 5`,
`m_takedamage = DAMAGE_YES` (2), `SetMoveType(MOVETYPE_TOSS, 0)`. **Solidity is untouched** — a
fresh corpse is still solid and still takes damage.

**The fade fork.** `ShouldFadeOnDeath()` (vtable `+0x8a0`) decides the next line:

- false → `CSoundEnt::InsertSound(SOUND_CARCASS 0x20, GetAbsOrigin(), volume 384, duration 30.0)`;
- true → `SUB_StartFadeOut` (`0x102695d0`): set render mode 2 with alpha 255 **only when the render
  mode was 0**, `AddSolidFlags(FSOLID_NOT_SOLID 0x4)`, zero local angular velocity, relink, and
  arm the fade think at `curtime + 0.0`.

**`CreateCorpse` (`0x1032c0e0`)** then builds the body:

- the **player** never ragdolls — the player branch spawns a static corpse and stops all 18 body
  fire particle emitters;
- an NPC carrying MiscFlag **`No_Ragdoll_Death` (bit 19, `0x80000`)** spawns a static corpse and
  takes a `curtime + 0.5` think. `TASK_SET_MISC_FLAG MiscFlag:No_Ragdoll_Death` in
  `SCHED_TROIKA_D_VISION_OF_DEATH` is the authored producer;
- otherwise `BecomeClientRagdoll(force, bone, 0)`, where `bone` is the hitbox index carried by
  `info`, or the bone looked up as `Bip01 Spine2` when that index is negative.

**`CBaseAnimating::BecomeClientRagdoll` (`0x10090180`)** returns **false** when the model carries no
ragdoll collide, and in that case only zeroes velocity — that false is what drives the death
schedule choice recorded in
[npc-ai-reverse-engineering.md](npc-ai-reverse-engineering.md). On success:

- **when the force bone is `-1` it first sets a weighted-random sequence of activity `0x21`
  (`ACT_DIERAGDOLL`) at cycle 0**, so the ragdoll starts from the first frame of a death animation;
  with a real hit bone the current pose is kept;
- `TriggerClientRagdoll` fires with the force and bone, `m_nRenderFX` becomes `0x17`
  (`kRenderFxRagdoll`), and — because both call sites pass `0` for the third argument —
  `FSOLID_NOT_SOLID` is added, move type becomes `MOVETYPE_NONE`, velocity is zeroed and the think
  is cleared.

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

### What the scalar `TakeDamage` I/O input actually builds

`CAI_BaseNPCTroika`'s datamap declares `TakeDamage` as `FIELD_INTEGER`, and its handler
(`0x102c29a0`) accepts the variant **only** when the field type matches — any other type silently
synthesizes **zero** damage. From the integer it builds a descriptor whose damage-type bits are
explicitly zeroed (`DMG_GENERIC`, no slashing/bashing/aggravated/fire), whose attacker and inflictor
are the I/O `pActivator`/`pCaller` rather than a weapon, and whose **force and damage position are
both the same zero vector**. It additionally zeroes the per-axis force scale at `+0x04 … +0x0c`
straight after construction. It then enters the ordinary pipeline unmodified — base
`CBaseEntity::TakeDamage`, the `OnTakeDamage` virtual, the life-state dispatch, and the same health
commit and `Event_Killed` a weapon kill reaches.

That zeroed scale is what makes the input harmless to the body. `Event_Killed` would otherwise
**replace** the zero force with a synthesised one, but the synthesis is multiplied by the same
per-axis scale, so the corpse killed by this input **collapses in place with no impulse** instead
of being knocked back. That is the authored result, not a defect: a map that wants a body thrown
has to move it itself.

### `invincible` is a total refusal, tested first

`CNPC_VVampire::OnTakeDamage` (`0x102bed30`) reads the authored `invincible` keyfield
(`m_bInvincible`, `CAI_BaseNPCTroika + 0x63d8`) as its **first** act and returns immediately when it
is set, before life state, the resolver, the soak or the health commit are reached. The only side
effect on that branch is a cosmetic timestamp/notify pair (`+0x5d98` / `+0x5e0c`). An invincible
character is therefore never damaged and never healed back — the transaction does not happen.

This is what lets one scripted wire serve a mixed cast: in `sp_tutorial_1` the Sheriff authors
`invincible 1` and no-sells the same `TakeDamage 100` that kills the Sabbat standing beside him,
who author `invincible 0`.

## Reverse-engineered mechanics (RE40)

- **Ranged Spread, Cone and Crosshair** [closed]:
  - `m_fCurrentRangedAccuracy` (`+0x1ddc`) is maintained only by the player's `PostThink` (`thunk_FUN_101600a0`, sole caller), converging toward `CrosshairMinSize`/`CrosshairWalkSize*`/`CrosshairRunSize*` at a rate scaled cubically by the Firearms feat rank (`GetRawAttackValue`). NPCs never run this path.
  - `SpreadAngle` is the **only** authored spread keyfield — `SpreadAngleMax` does not exist; the loader stores `SpreadAngle * two engine constants` into all three axes of the per-mode spread vector, and only x/y are ever read.
  - The fired cone (`FUN_10268170`) samples a random point in a unit disc: the X axis always scales by the static `SpreadAngle`; the Y axis substitutes `const * m_fCurrentRangedAccuracy` only when the shooter is `CBasePlayer`. NPCs fire on the static `SpreadAngle` alone, on both axes.
  - `Presence` modifier and `Shaky Hands` penalty log diagnostic messages via `DevMsg` in `WeaponRangedShot` (`0x102387b0`) but are never read again after that call — confirmed cosmetic, not a live input to the cone.
- **Burst Fields and DamageFlinch Pipeline**:
  - `BurstMin` (`+0x3a4`) and `BurstMax` (`+0x3a8`) are parsed by `WeaponModeDataLoader` (`0x10259230`) from weapon script files but are unreferenced by runtime combat logic (dead fields).
  - Firearm damage enters `DamageFlinch` (`0x103229d0`) via `RangedDamagePerVictim` (`0x10268330`) -> `CBaseEntity::DispatchTraceAttack` (`0x100a7d00`, identified by its scope string) -> the victim's `TraceAttack` virtual, which requests the flinch **at the top of `CBasePlayer::TraceAttack` (`0x10162c30`) or the bottom of `CAI_BaseNPC::TraceAttack` (`0x10266780`)**, before soak and before the health commit. `OnTakeDamage_Alive` (`0x103302e0`) does **not** call `DamageFlinch`, and `0x101cfef0` is the impact-effect/decal body — it reads the trace fields, maps surface characters and calls `+0x268`/`+0x26C` — not a trace-attack dispatcher. See "Damage flinch" above.
- **Melee Swing Pipeline and Impact Dispatch**:
  - `CWeaponMelee::PrimaryAttack` (`0x103eaca0`) sets the attack animation activity; contact is then owned by the per-frame swept walk `CBaseCombatCharacter::MeleeSwingUpdate` (`0x10346CD0`) / `MeleeSwingStep` (`0x10343020`) over the sequence's authored swing records. No animation event is involved.
  - `0x103ea510` is **`CWeaponMelee::Deploy`** (vt `+0x4EC`, called from `Weapon_Equip` / `Weapon_Switch`), not an event dispatcher.
  - **`CWeaponMelee` does not bypass the shared traced-impact virtual `0x102579f0`** — it inherits it, and the swing sweep calls it through `vt +0x438` to commit each contact.
- **SkillRequirement**:
  - `SkillRequirement` is parsed to weapon mode field `+0x3d0` in `WeaponModeDataLoader` (`0x10259230`) and is never read or checked elsewhere in engine binaries (dead field). The item files' own comment calling it a requirement to wield is wrong.
- **knockback_chance**:
  - Parsed into the weapon-data record — one write site (`0x1025a41f`), one copy pair, zero gameplay readers (dead field). The knockback gate is the victim's hit-buildup counter and the swing record's `0xBA == 2` marker; the activity comes from the swing record's candidate table.
- **Ranged and Melee Damage Post-Soak Operations**:
  - **Melee Damage Formula**: $\text{Final Damage} = \text{Lethality} \times (\text{BaseDamage} + \text{DmgModifier}) \times \text{Multiplier}$, where `DmgModifier` is the `Dmg` string's **optional leading trait** — `0` on every real weapon, authored only on `holy_light`, `wolf_head` and developer items — and Potence guarantees a minimum lethality floor. The attack feat enters `Lethality` as retail's "Feat Adjustment", not this term.
  - **Ranged Damage Formula**: $\text{Final Damage} = \text{Lethality} \times \text{BaseDamage} \times \text{Multiplier}$. Firearm Feat scales accuracy rather than flat damage.
  - **`Multiplier`, both paths**, is the trace/volley scale (volley fraction × hitgroup scale) **times `CVDmg_t` word 15**, the accumulated damage-filter float.
- **Special Predicates and CVDmg_t::Apply**:
  - `CVDmg_t::Apply` (`0x101fb200`) invokes an installable game callback (`DAT_1074e7bc`) registered via `CVDmgSetApplyCallback` (`0x101fb180`).
  - Special damage modifiers and immunities are resolved in `CBaseCombatCharacter::ApplySpecialDamageModifier` (`0x1033db30`), matching weapon/damage IDs against the entity's 224-entry lookup table to trigger condition flags (`0x400`) and reactive audio.
- **DMG_FIST**:
  - `DMG_FIST` is not an engine damage flag and **does not alias to `DMG_CLUB` or to anything else**. `StrToDMGFlags` carries no entry for it and returns `0`, so an unarmed attack's Source damage mask is empty; its bashing family comes from the `Dmg` string's `Bashing` token.
- **Alive-Path Filtering and Rounding**:
  - `OnTakeDamage_Alive` (`0x103302e0`) guards against dead/invalid states (`0x168 != 0x1e/0x1f`) and non-positive incoming values (`damage <= 0.0`).
  - Final damage values truncate to integer via standard `__ftol()` (FISTP truncation toward zero) prior to deducting entity health.

