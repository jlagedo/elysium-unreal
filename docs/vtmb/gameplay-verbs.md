# Gameplay verbs and runtime action gates

In VtMB, a "verb" can be a console command, a held user-command bit, an entity input, a Python
method, an AI task or a compact gameplay action. They are related, but they are not aliases for
one universal action enum. This document owns the map between those layers and the question
"where is this action checked at runtime?"

The exhaustive bindable command inventory is in `docs/vtmb/controls.md`; entity inputs and
outputs are in `docs/vtmb/entity_io.md`; Python methods and module calls are in
`docs/vtmb/script_api.md`; item ownership is in `docs/vtmb/inventory.md`; activity/sequence
selection is in `docs/vtmb/animation_and_movers.md`; checks and combat effects are in
`docs/vtmb/skills-and-checks.md` and `docs/vtmb/combat-and-damage.md`.

## Evidence boundary

The layer map joins the hash-pinned server/client command registrations, the recovered
player-command and gameplay-action chains, class datamaps, Python method tables, patch-first
bindings and the 22-map exported corpus. `research/cases/animation-pose/` owns the complete
action/activity extraction; `research/cases/core-mechanics/` owns the check and damage joins.

This is an ownership and routing model. It does not claim that every verb executes each stage
below, nor that command-string adjacency proves a call relationship.

## The seven verb layers

| Layer | Examples | What it represents | Runtime owner |
|---|---|---|---|
| Binding/alias | `MOUSE1 -> +attack`, `f -> vm_feed` | user configuration and command composition | engine/client cfg and alias system |
| Console command | `+attack`, `+use`, `vdiscipline_last`, `inven_drop` | named entry point and arguments | client, server or engine command registry |
| User command | attack/use/feed/reload bits, movement axes, view deltas | frame-stamped player intent; held and edge state | client builder, server player command |
| Gameplay predicate | active weapon, grounded, target eligible, zone legal, enough blood | whether intent may become an action | player, weapon, target and game-rule code |
| Gameplay action/activity | compact player code, Source `ACT_*`, AI task/schedule | realized simulation state and animation request | player/NPC/weapon resolver |
| Effect verb | hit, damage, feed, discipline effect, inventory transfer | authoritative world/sheet mutation | server combat, entity, inventory and rules systems |
| Script/entity verb | `Use`, `TakeDamage`, `GiveItem`, `OnTrigger`, Python calls | content-driven invocation and orchestration | datamap I/O queue and embedded Python bridge |

The same word can occur at multiple layers. `Use` is both player intent and an entity input;
`attack` is a command/bit but the realized action is selected later; a Python `TakeDamage`
call bypasses weapon intent and starts near the effect layer.

## A normalization model

For reconstruction and debugging, trace a verb through these questions:

1. **Entry:** which binding, console command, entity input, script method or AI task requested it?
2. **Intent:** is it held, pressed, released, deferred or parameterized?
3. **Authority:** which side may execute the mutation, and is the player/NPC in a mode that
   accepts it?
4. **Target:** which trace, handle, active item, selected slot or authored target resolves?
5. **Eligibility:** which owner checks capability, state, ground contact, zone policy, resource
   cost, relation or cooldown?
6. **Mechanic:** is the decision a direct predicate, a rating threshold, a dice roll or an
   opposed check?
7. **Realized action:** which compact action/activity/AI task becomes simulation state?
8. **Effect:** which item, sheet, health, inventory, entity-I/O or script mutation commits?
9. **Presentation:** which sequence, layer, camera, HUD and sound observe the committed state?

This order is a diagnostic contract, not a literal monolithic retail function. Some paths skip
stages and some re-enter the I/O or script layer after committing.

## Player-facing verb families

| Family | Primary entries | Confirmed runtime checks/routing | Result owner |
|---|---|---|---|
| Movement/look | `+forward`, `+back`, strafe, jump, duck, look modes | client builds axes/bits; player command runs before the think/event pass; mover and camera read realized intent | movement/camera, then locomotion activity |
| Primary attack | `+attack` | active weapon and weapon policy consume the bit; attack code resolves hit/defense/damage; exact global eligibility chain remains open | weapon/player combat, then damage and action resolver |
| Ordinary secondary | `+attack2` | owns the ordinary secondary-fire bit; does not by itself assert melee block | weapon-specific secondary policy |
| Composite secondary/block | `+wpn_secondaryatk` | asserts a dedicated bit and forwards into `+attack2`; block also requires ground contact and active-weapon capability `0x18000` | player compact code 13 / `ACT_PREBLOCK`, or weapon secondary |
| Reload | `+reload` | held command bit exists; active-firearm, clip/reserve and animation/state predicates still need an end-to-end join | active weapon/inventory ammo |
| World use | `+use` | look/use target, object capability, use filter and class-specific eligibility; then target `Use` | entity class and I/O queue |
| Feed | `+feed` or patch alias/script helper | target eligibility and resistance policy; resisted branch uses Brawl rating vs target Hacking roll | player/target feeding state, camera and sheet effects |
| Inventory select/equip/drop | `slotN`, cycle, holster, drop and inventory UI commands | selected category/item, ownership, droppable/permanent/stack/ammo policy; transfer is server-authoritative | combat-character inventory and item entities |
| Hotkey/discipline | `vhotkey`, `vdiscipline*` | selection is separate from cast; exact target, blood-cost, zone, cooldown and active-effect gates are not yet recovered end to end | discipline manager, sheet and effect queue |
| Dialogue | `+use` into a talkable or scripted start | target/conversation eligibility, dialogue dependency thresholds, sex gate and Python conditions | dialogue state, camera and script/I/O actions |

### Held bits are not actions

`+attack`, `+attack2`, `+wpn_secondaryatk`, `+reload`, `+use` and `+feed` have press/release
semantics. The user command records intent; it does not prove that the server accepted a hit,
block, reload, use or feeding session. Any replay/capture format must preserve both the raw bit
and the later realized state.

The clearest example is block: `+wpn_secondaryatk` asserts both the dedicated secondary-attack
bit and ordinary attack2. The server emits compact action 13 only when the dedicated bit,
ground contact and the active weapon's block capability agree. Animation then translates
`ACT_PREBLOCK` through the current form/weapon rules. Treating `attack2` as "block" loses both
the ordinary secondary-fire route and the eligibility check.

### `+use` resolves an entity-owned verb

The player use path traces/selects a usable entity, tests generic capability and
`PassesUseFilter`, then dispatches to the target's class-specific `Use`. Doors, buttons,
terminals, containers, NPCs and signs own different eligibility and outputs. The resulting
`OnUseBegin`, `OnUseEnd`, `OnLockedUse` or class output enters the ordinary queued I/O system;
it is not a direct UI callback.

### Feed is a transaction, not just an animation

The initial feed attempt has automatic-success branches, a target resistance branch and a
stealth override. The resisted branch is the asymmetric check documented in
`docs/vtmb/skills-and-checks.md`. Acceptance then has to coordinate player/target action state,
camera, blood/health transfer, interruption and completion outputs. Only the first eligibility
and opposed-check seam is closed; the full transaction state machine remains open.

### Selection and activation are different verbs

The hotkey bar may select a weapon, discipline or blood pack. `vhotkey #N` selects a slot and
`vdiscipline_last` casts the selected discipline; the selection is deferred by one frame.
Inventory category commands similarly select before equip/use. A faithful input layer therefore
cannot collapse selection, equip and activation into one button event.

## Content-driven verbs

Player input is only one producer. The server also exposes:

- entity inputs such as `Use`, `TakeDamage`, `InventoryRemove`, `Weapon_Equip`, `Lock`,
  `Unlock`, animation inputs and terminal commands;
- entity outputs that enqueue later inputs with target, delay, parameter and fire count;
- Python `Character`, `Entity` and module methods such as `CalcFeat`, `GiveItem`, `HasItem`,
  `TakeDamage`, dialogue and quest operations;
- AI schedules/tasks that realize locomotion, attacks, reactions and scripted scenes;
- animation events that call back into weapon or inventory policy.

These paths converge at owned simulation seams. A script `TakeDamage(1000)` should enter the
same health commit as weapon damage after supplying its damage context; an entity `Use` should
run the same class method as the player-use path; an AI attack and a player attack should share
the weapon/damage resolver while retaining different intent producers.

## Realized gameplay actions

The recovered action chain is:

```text
command bit / AI task / scripted state
    -> gameplay predicate and realized base activity
    -> player or NPC/form translation
    -> weapon translation
    -> weighted or exact-label sequence lookup
    -> pose parameters, layers, transitions and events
```

The compact player action code is therefore an observation of accepted simulation state, not a
substitute for the command or the attack result. The complete compact-code producers, paired
modes, activity registry and weapon translation tables are canonical in
`docs/vtmb/animation_and_movers.md` and
`research/cases/animation-pose/specs/gameplay_actions.json`.

## Faithful instrumentation

One trace record should keep the layers separate:

```text
frame/time, actor, producer, command/arguments, raw buttons,
mode/target/active item, predicate verdicts, feat ratings,
dice inputs and result, compact action/activity/sequence,
effect descriptor, committed sheet/world delta, outputs/scripts
```

Without this separation, a failed attack can be mistaken for missing input, an accepted action
for a successful hit, and an animation for proof that damage committed.

## Open research gaps

- Recover the complete primary-attack and reload eligibility/state machines for melee and ranged
  weapon families.
- Close the melee record-to-damage commit and the ranged multiplier decomposition.
- Recover discipline selection/target/cost/cooldown/zone checks and the precise one-frame hotkey
  handoff.
- Complete the feed transaction: target predicates, activity identities, blood/health cadence,
  interruptions, completion and outputs.
- Recover conversation mode's treatment of already-held input.
- Complete terminal difficulty/skill attempts (RE39).
- Join representative NPC combat schedules/tasks to the same attack, defense and damage seams,
  including decision cooldowns and target policy.

Project implementation status belongs to `docs/project/roadmap.md`: the current combat slice is
13.3 and this cross-cutting research is RE40.
