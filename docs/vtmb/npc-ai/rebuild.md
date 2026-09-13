# NPC AI — What the rebuild needs

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## What a complete game-side NPC AI requires

The recovered system implies the following minimum architecture. These are responsibilities, not
a proposal to copy Source-era implementation details where Unreal already supplies a better
mechanism.

### Identity and spawn lifecycle

- Native behavior class or an equivalent explicit policy owner.
- Stat-template inheritance and resolved character sheet.
- Model/body/gender/sound identity and equipment loadout.
- Maker child specification, count/frequency/activation, child targetname/squad, and child output
  inheritance.
- Hidden, invincible, reset, boss, drop, and teardown policy.
- Stable entity handles so relationships, scripts, saves, and outputs survive object lookup.

### Senses and stimulus memory

- Vision using range, cone, visibility/occlusion, lighting/stealth contribution, and per-NPC
  tuning.
- Hearing using classified sound radius/type and occlusion policy.
- Damage, combat, criminal, supernatural, and script-generated stimuli.
- Last-seen, last-heard, last-damage, enemy, last-enemy, occlusion, and expiry/search memory.
- Separate concepts for lost line of sight and forgotten/lost target.

### Relationship and social state

- Save-backed entity- and class-relationship tables with correct precedence and priority behavior.
- Initial map-authored player reaction and script mutation through `SetRelationship`.
- Separate emotional disposition/presentation state.
- Separate RPG/social reaction score and modifiers.
- Talk eligibility and dialogue ownership independent of combat hostility.

### Decision state and conditions

- Current and ideal idle/alert/combat/script/prone/dead states.
- A condition set gathered coherently once per decision pass, including damage, enemy, sensory,
  range/attack capability, squad, and investigation facts.
- Forced state and class policy hooks.
- Deterministic state transition and failure behavior.

### Schedule and task runtime

- Named schedule registry with class-local translation/override.
- Ordered tasks, task progress, completion and failure.
- Per-schedule interrupt masks and delayed/suppressed interrupts.
- Fail schedules and schedule-to-schedule transfer.
- Class-specific task start/run handlers.
- Save/restore of the active schedule, task, timers, goals, and owner.

### Navigation and motor

- Path requests, goals, tolerance, cover/flank/flee destinations, patrol paths, and failure reasons.
- Facing and locomotion execution separated from decision policy.
- Ownership transfer among normal AI, scripted sequence, scripted schedule, dialogue, and follower
  control.
- Squad knowledge and group coordination where authored/native policy uses it.

### Combat action policy

- Capability and range checks for melee/ranged actions.
- Weapon/equipment selection, attack, reload, block, dodge, cover, chase, and retreat policy.
- A clean boundary to the authoritative damage resolver.
- Damage, incapacitation, grapple, feeding, and death reactions and outputs.
- Combat sound emission so incidents propagate through hearing.

### Animation and presentation

- Abstract activities selected by AI, not raw clip names scattered through policy code.
- Class and weapon activity translation, then model-specific weighted sequence selection.
- Upper/lower-body or gesture ownership where required.
- Disposition-driven stance, fidget, expression, gaze, and blink as a presentation layer.
- Exact scripted-sequence ownership and restoration.

### Script, I/O, and persistence

- All base and derived NPC outputs with Source-style delay, caller/activator, once-only, and save
  semantics.
- Python access to target lookup, datamap inputs/properties, schedules, sequences, relationship,
  and perception knobs.
- Dialogue action columns and deferred `ScheduleTask` calls.
- Save-backed quest globals plus NPC state, relationships, current owner, memory, schedule, and
  maker state.
- Cancellation and teardown that emit no duplicate completion/death/sequence events.

### Observability and deterministic validation

- A per-NPC trace of stimulus, relationship result, condition changes, state transition, schedule
  selection, task progress, movement owner, activity, and output firing.
- Stable names/IDs in captures so map entity, native class, Python actor, and Unreal actor correlate.
- Ability to freeze or single-step decision time independently of visual capture.
- Replayable scripted stimuli for sight, hearing, damage, relation change, target loss, and save
  restore.
- Side-by-side retail/rebuild incident reports, not only end-state screenshots.

Without this observability, an NPC that reaches the same final location can still be wrong in
detection timing, state, schedule, animation, output order, quest consequence, or save behavior.

## Faithful-first reconstruction order

The dependency order suggested by the evidence is:

1. **Definition and identity:** load the complete map/template/maker surface into a resolved NPC
   specification and expose it in diagnostics.
2. **Relationships and persistence:** implement entity/class rows, precedence, initial
   `player_reaction`, `SetRelationship`, and save/restore before combat policy depends on them.
3. **Stimulus and memory:** reproduce visual, auditory, damage, and script stimuli with last-known
   state and lost-LOS distinctions.
4. **Conditions and high-level states:** establish coherent gather/update passes and
   idle/alert/combat/script/prone/dead transitions.
5. **Schedule/task kernel:** registry, task lifecycle, interrupts, failure, class overrides, and
   deterministic trace. Begin with the schedules exercised by one chosen encounter.
6. **Navigation/motor integration:** preserve one movement service while adding explicit ownership
   and the move/follow/sequence seams.
7. **Combat and civilian reactions:** attacks, cover/chase, flee/cower, sound propagation, damage
   consumption, and output delivery.
8. **Authored controllers:** `aiscripted_schedule`, direct schedule changes, complete maker child
   wiring, Python/dialogue calls, and encounter I/O.
9. **Presentation:** activity translation/sequence parity, disposition, gaze, facial response, and
   transition polish.
10. **Broaden by class and map:** extend from controlled representative incidents to police,
    pedestrians, vampire combatants, animals, hunters, bosses, followers, hubs, and maker waves.

This order is faithful-first: it keeps the recovered behavior available for comparison before any
explicit reconstruction delta. It also prevents a visually convincing animation graph from becoming the
unintended owner of AI state.

## Current rebuild coverage and gap boundary

The current Unreal runtime already has useful lower layers. `FElysiumNpc` creates the skeletal
body, exposes dialogue gates, follows named patrol paths, uses eligible interesting places,
participates in scripted-sequence movement ownership, and saves/restores its implemented state.
`FElysiumCombatCharacter` owns the resolved character sheet, money, talk gating, damage, and death
surface. The character presentation path resolves disposition name plus level into stance,
default/talking expression, gaze and blink policy; dialogue lipsync composes over that baseline.

The independent combat-relationship store is present on `FElysiumNpc`: `player_reaction` seeds an
exact player row, `SetRelationship` writes exact-entity or class rows (including `player` and
wildcards), ordinary lookup is exact entity then class then neutral, and the table survives save.
This is deliberately state only. Nothing in the current runtime derives it from emotional
disposition or RPG reaction, and nothing consumes it to assign an enemy or enter combat.

The current `npc_maker` child specification propagates model, stat template, base gender, default
disposition, angles, interesting-place enablement, and groups. That is narrower than the authored
maker/NPC surface catalogued above. Full equipment, perception, squad, child I/O, and other maker
inheritance remain part of the gap.

The general native senses/memory producers, recovered relationship-priority/enemy-selection
consumers, high-level state/condition loop, combat schedule/task graph, class-specific combat
policy, full follower policy, and RPG reaction-score model are not yet implemented in the rebuild.
`aiscripted_schedule.StartSchedule` is presently a stub. Existing
patrol, dialogue, sequence, animation, damage, and entity-I/O foundations should be extended at
their existing ownership seams rather than replaced by a parallel NPC runtime.

This section is a scope boundary, not a second status tracker. Project status and priority remain
in the declared roadmaps; source remains the as-built record.

## Open questions and targeted capture programme

### Native questions still requiring recovery

- What do all numeric values of `npc_perception`, investigation modes, and player conduct
  thresholds mean at their native consumers?
- Which special flags and oblivious branches alter `IRelationType` before ordinary table lookup?
- What is the semantic name of NPC-state selector case 12?
- Which derived classes add each dialogue/incapacitation/transform output, and what are their exact
  firing conditions?
- What is the precise movement/gait distinction between `aiscripted_schedule` modes 1/2 and 4/5?
- What memory expiry and search rules govern lost LOS, lost enemy, and return to idle?
- How do squad relation knowledge and `SQUAD_SEE_ENEMY` propagate and expire?
- Which concrete ranged-impact paths call generic `DamageFlinch`, and which derived classes replace
  the ordinary light/heavy/flinch policy?

### Controlled retail captures

A useful capture matrix should hold map and NPC constant while changing one variable at a time:

| Case | Manipulation | Observe |
|---|---|---|
| Sight | Enter/leave cone at fixed range and light | found output, state, schedule, memory, lost-LOS timing |
| Hearing | Emit quiet/normal/loud sounds behind/without occluder | heard category, investigate schedule, propagation radius |
| Relationship | neutral/fear/hate/like entity and class rows at varied priority | effective relation, enemy assignment, selected schedule |
| Damage | light/heavy damage from visible and hidden attacker | condition, output order, enemy memory, alert/combat transition |
| Criminal/supernatural | Cross each authored threshold | investigate/flee/attack choice and reset behavior |
| Occlusion | Break LOS during chase at controlled distances | chase/search/cover choice, lost outputs, forgetting |
| Script ownership | Aggress during sequence and scripted schedule | interrupt, cancellation, restoration, output behavior |
| Save/restore | Save in alert/combat/task progress | enemy, relation, memory, schedule/task and movement-owner parity |

Each capture should record entity targetname/class/stat template, all relevant map keyvalues,
relationship rows, stimulus type/time, condition additions/removals, current/ideal state, selected
schedule/task, goal/movement owner, activity/sequence, and ordered I/O/Python callbacks. The same
incident can then become an automated rebuild fixture.

Recommended starting encounters are deliberately diverse:

- Arthur for damage-to-fear and death script consequence;
- Bertram for damage-to-hostility, dialogue closure, and Discipline activation;
- diner civilians and assassins for hearing, discovery, cower, and blame;
- warehouse guards for sight/hearing/damage convergence and stealth-failure I/O;
- theatre guards for `OnFoundEnemy` semantics; and
- the befriended dog for relationship/perception/schedule composition.

## Final model

The general VtMB NPC is best understood as a persistent actor with five interacting authorities:

```text
authored definition
  map + maker + stat template + equipment + groups + outputs

native cognition
  senses + memory + relationships + conditions + state + schedule/tasks

physical execution
  navigator + pathfinder + motor + weapon + damage + animation activity

scripted control
  entity I/O + Python + dialogue actions + sequence/schedule ownership

presentation and social expression
  disposition + expression + gaze/blink + RPG reaction score
```

Aggression crosses all five, but none can substitute for another. The map says who the actor is
and what consequences matter; native AI decides what it notices and does; physical systems execute
the choice; scripts steer exceptional authored moments; and presentation communicates the result.
A complete rebuild needs the boundaries as much as it needs the behaviors, because those
boundaries are what let combat, dialogue, stealth, quests, saves, and cinematics coexist on the
same NPC.

# 2026-09-07 — `OnStateChange` (vtable slot 463) holsters and draws the active weapon

Slot 463 is called on the state EDGE and takes the new `m_NPCState` as its second argument. 79
classes fill it; most take `CAI_BaseNPCTroika::OnStateChange` (`0x102ae140`), which does **not**
touch the weapon — `CNPC_VVampire`, `CNPC_VHuman`, `CNPC_VPedestrian`, `CNPC_VBrujah`,
`CNPC_VGangrel`, `CNPC_VZombie` and 40-odd more. **Three bodies do, and they are the same code**
[VtMB decompiled]:

| Retail class | Body | Shared with |
|---|---|---|
| `CNPC_VGuard1` | `0x1037d020` | — (adds an unrelated `+0x29c`/`+0xa8` probe ahead of the switch) |
| `CNPC_VHunter` | `0x10388880` | — |
| `CNPC_VGhoulCroucher` | `0x103871c0` | `CNPC_VHumanCombatant`, `CNPC_VHumanCombatPatrol`, `CNPC_VSabbatGunman`, `CNPC_VStalker`, `CNPC_VYukie`, `CNPC_ProneDialog` |

The body is one switch on the new state:

* **1 (`NPC_STATE_IDLE`)** — `GetActiveWeapon()->Hide()` (vtable `+0x108`, `CBaseEntity::Hide`
  `0x1009d2a0`), then chain to `CAI_BaseNPCTroika::OnStateChange`;
* **2 (`NPC_STATE_COMBAT`), 3 (`NPC_STATE_ALERT`), 11 (hunt)** — `GetActiveWeapon()->Unhide()`
  (vtable `+0x10c`, `CBaseEntity::Unhide` `0x1009d380`), then chain. (Corrected 2026-09-12: VtMB
  numbers combat 2 and alert 3, the reverse of the SDK enum. `CNPC_VHuman::SelectIdealState`
  `0x103851e0` runs the "Combat state with no enemy" arm on `m_NPCState == 2` and returns 3 from
  it; the state byte `0x1026e3e0` gives 2 → `0x8f` and 3 → `0x39`; `SelectSchedule`'s case 3 is
  the alert selector.)
* everything else — chain, writing nothing.

Both arms are guarded by `GetActiveWeapon() != 0`. So "an armed class holsters while idle and draws
when it goes alert" is a property of **seven concrete classnames** and of nothing else — it is not
a base-AI behaviour and not a weapon behaviour. What the hidden bit then does to the body's
animation is `docs/vtmb/animation_and_movers.md` → the `EF_NODRAW` entry of the same date: a hidden
active weapon translates nothing and selects the relaxed set, exactly as empty hands do.

**Four overrides remain UNREAD**, and are named here rather than assumed to be the base:
`CNPC_VCop` (`0x10371c20`), `CNPC_VBach` (`0x103639b0`), `CNPC_VTzimisce` (`0x103ba2c0`),
`CNPC_VSabbatLeader` (`0x103a6f70`). Two of them — `npc_VCop` and `npc_VSabbatLeader` — are
registered leaves in this runtime today, and they take the Troika base's answer (no weapon write)
until their bodies are decompiled. That is a stated gap.

**Port.** `FElysiumNpc::ApplyStateWeaponVisibility` is the override body, taking the new state as
retail's second argument does; `ClassHolstersOnState` is the seven-classname set, spelled from the
retail class names with the port's `npc_` prefix (`npc_VGuard1`, `npc_VHunter`,
`npc_VHumanCombatant`, `npc_VHumanCombatPatrol`, `npc_VSabbatGunman`, `npc_VStalker`, `npc_VYukie`,
`npc_ProneDialog`, `npc_VGhoulCroucher` — only the first three plus `npc_VHunter` have registered
leaves in `ElysiumNpcClasses.cpp` so far; the rest are listed so a map that spawns one behaves).
The edge is **polled** by `FElysiumNpc::PumpStateChange`, called from `Think` after
`RunConditionPass` and after `ResolveLoadout`: this runtime writes the state from three places (the
ideal-state pass, `aiscripted_schedule forcestate`, and the body arbiter's scripted push), and one
edge tracker is what keeps them from each needing a hook. The first pump always fires, which stands
in for retail's own spawn-time `SetState(IDLE)` — that is what puts a freshly spawned guard's
weapon away.

**Retail state 11 has no port equivalent.** `EElysiumNpcState`'s other members (`Scripted`, `Prone`,
`Dead`) are this port's own and none of them is retail's 11; nothing is known about it beyond the
fact that it draws the weapon, so no port state is mapped onto it. Stated, not guessed.

Proof: `Elysium.Substrate.WeaponHidden.StateChange` (an `npc_VHumanCombatant` hides its active
weapon entering Idle and unhides it entering Alert/Combat; an `npc_VVampire` never changes the bit).
