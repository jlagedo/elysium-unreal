# VtMB NPC AI reverse-engineering survey

## Scope and confidence

This report reconstructs the general non-player-character AI surface of the user's installed
*Vampire: The Masquerade - Bloodlines* build. It connects four bodies of evidence:

- the entity dictionaries exported from the 22 maps currently present in the corpus;
- the installed `vdata/system` tables and level Python/dialogue scripts;
- static recovery from the pinned retail `vampire.dll`; and
- the repository's existing native schedule, task, translation, and script-action surveys.

The installed corpus includes Unofficial Patch/Wesp additions. Counts and authored examples in
this report therefore describe that installed build, not a claim about pristine 1.2 retail.
Generated exports, binaries, decompilation, and survey artifacts remain external evidence and are
not repository content.

Confidence is high for class names, authored keyvalues and outputs, script calls, native datamap
fields, relationship token and priority decoding, the AI update loop, enemy eligibility and
ranking, schedule-gated enemy replacement, the state-switch cases, ordinary humanoid melee/ranged
selection, damage-condition generation, death admission, schedule/task registrations, and the
`aiscripted_schedule` execution modes. Confidence is lower for the exact meaning of several
numeric map keyfields, the human-readable distinction between the two move and two follow modes of
`aiscripted_schedule`, class-specific incapacitation policy, and frame-exact presentation during a
live aggression incident. Those open points need controlled retail capture rather than naming
inference.

This report owns the cross-system NPC-AI reconstruction. Detailed animation selection and task
translation remain in [animation_and_movers.md](animation_and_movers.md); damage calculation and
health commit remain in [combat-and-damage.md](combat-and-damage.md); the generic I/O and scripted
sequence contracts remain in [entity_io.md](entity_io.md); disposition-driven gaze and blinking
remain in [facial_animation.md](facial_animation.md); and the complete recovered Python action
surface remains in [script_api.md](script_api.md).

## Executive reconstruction

VtMB does not define an NPC in one place. A spawned NPC is the product of several layers:

1. A map entity chooses a native classname, model, stat template, equipment, squad, initial
   relationship, perception parameters, investigation policy, ambient groups, and I/O outputs.
2. `vdata/system/npctemplate*.txt` supplies the RPG/stat sheet and inherited character defaults.
3. The native DLL supplies the class hierarchy, senses, enemy memory, relationship tables,
   conditions, state machine, schedule selector, task executors, navigation, combat actions, and
   animation-activity policy.
4. Map outputs and Python add authored consequences: quest flags, dialogue gates, stealth failure,
   tutorial progression, cinematic control, and explicit changes to relationship or schedule.
5. The model resolves the selected activity through class and weapon translation into a weighted
   animation sequence.

The central native loop is:

```text
stimulus
  -> senses, damage memory, relationship query, and scripted conditions
  -> gathered conditions and enemy memory
  -> current and ideal NPC state
  -> class-specific schedule selection
  -> schedule task execution
  -> navigation, motor, weapon, and animation activity
  -> entity outputs, sound emission, Python, dialogue, and quest side effects
  -> next AI update
```

"Aggression" is consequently not one callback. Seeing or hearing a hostile actor, taking damage,
being assigned an enemy, crossing a criminal/supernatural threshold, or receiving a script input
can establish hostility. That stimulus updates memory and conditions; may fire outputs such as
`OnFoundPlayer`, `OnFoundEnemy`, `OnHearCombat`, or `OnDamaged`; may interrupt the current schedule;
causes an idle/alert/combat state decision; and finally selects tasks such as face, pursue, take
cover, flee, cower, equip, or attack. Authored I/O and Python then attach story consequences to the
same incident.

Three similarly named systems must remain separate:

- `SetRelationship` writes the native combat-AI relation table (`D_HT`, `D_FR`, `D_LI`, `D_NU`).
- `SetDisposition` changes the character's emotional/dialogue presentation and associated stance,
  expression, gaze, and fidget policy.
- `reaction.txt` and `reactions000.txt` define an RPG/social reaction score and modifiers.

Conflating those three would make combat hostility, conversational mood, and social-stat outcomes
incorrectly drive one another.

## Evidence set and reproducibility

### Installed data surveyed

The current export root contains entity exports for 22 maps. The survey counts direct `npc_*`
placements and one requested child-class definition for every `npc_maker`; it does not multiply a
maker by the number of children it may produce at runtime. The script corpus contains 36
survey-visible Python files, 147 dialogue files with
50,393 rows, and 22 entity exports with 1,635 output payloads that execute Python. The installation
also contains 36 `npctemplate*.txt` files with 150 template declarations.

The pinned native module used by the recovery is:

| Property | Value |
|---|---:|
| Module | `vampire.dll` |
| Size | 7,860,281 bytes |
| SHA-256 | `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f` |

The principal reproducible repository surveys are:

```powershell
uv run elysium research native_schedule_survey
uv run elysium research npc_task_override_survey
uv run elysium research npc_translation_survey
uv run elysium research action_animation_survey
uv run elysium research script_api_survey --json E:\elysium-work\research\npc_ai_script_api.json
uv run elysium research tutorial_npc_bootstrap --json E:\elysium-work\research\tutorial-npc-bootstrap.json
```

The JSON path above is an external work artifact. Exact paths to the user's game and generated
corpus are supplied by local environment configuration and are intentionally absent from this
document.

### Evidence limitations

This is a static and corpus-correlated reconstruction. No new live retail aggression capture was
performed for this survey. Native code proves the control structures, ordering and thresholds,
and authored data proves what maps request, but static recovery alone does not prove the rendered
pose, blend, impulse or precise frame on which two externally observed outputs appear. Numeric
fields whose consumers have not been decoded are reported as fields and distributions, not given
invented semantics.

## The authored NPC population

### Spawn demand across the current 22-map corpus

The corpus contains 426 NPC spawn definitions: 317 direct placements and 109 maker child
definitions. Nineteen NPC classnames appear in the maps. Of those definitions, 425 translate to
recovered native classes; the single unresolved request is `npc_BaseVampAI`. The DLL contains 77
NPC descendants in total, so the current map slice exercises only part of the native class
library. A maker can produce more than one child, so 426 is not a simultaneous or lifetime actor
count.

| Classname | Spawn definitions |
|---|---:|
| `npc_VPedestrian` | 121 |
| `npc_VHumanCombatant` | 84 |
| `npc_VCop` | 72 |
| `npc_VVampire` | 63 |
| `npc_VRat` | 24 |
| `npc_VDialogPedestrian` | 23 |
| `npc_VCamera` | 10 |
| `npc_VHunter` | 6 |
| `npc_VHuman` | 5 |
| `npc_VVampireBoss` | 3 |
| `npc_VNewscaster` | 3 |
| `npc_VTaxiDriver` | 2 |
| `npc_VProneDialog` | 2 |
| `npc_payphone` | 2 |
| `npc_VAsianVampire` | 2 |
| `npc_VScurrying` | 1 |
| `npc_BaseVampAI` | 1 |
| `npc_VDog` | 1 |
| `npc_VLasombra` | 1 |

The population is not equivalent to the number initially alive in a map. A direct row describes
one placed entity, while a maker row describes a production request whose count, frequency,
activation, and lifecycle are controlled separately. The census is therefore a demand census,
not a simultaneous population measurement.

| Map | Direct | Maker requests | Total demand |
|---|---:|---:|---:|
| `la_hub_1` | 68 | 40 | 108 |
| `sm_apartment_1` | 2 | 0 | 2 |
| `sm_asylum_1` | 21 | 0 | 21 |
| `sm_bailbonds_1` | 2 | 0 | 2 |
| `sm_basement_1` | 2 | 0 | 2 |
| `sm_coffee_1` | 0 | 0 | 0 |
| `sm_diner_1` | 11 | 0 | 11 |
| `sm_gallery_1` | 1 | 0 | 1 |
| `sm_hub_1` | 38 | 48 | 86 |
| `sm_junkyard_1` | 9 | 0 | 9 |
| `sm_medical_1` | 15 | 2 | 17 |
| `sm_oceanhouse_1` | 0 | 0 | 0 |
| `sm_pawnshop_1` | 8 | 0 | 8 |
| `sm_pawnshop_2` | 1 | 0 | 1 |
| `sm_pier_1` | 17 | 3 | 20 |
| `sm_shreknet_1` | 0 | 0 | 0 |
| `sm_smoke_1` | 4 | 0 | 4 |
| `sm_tattoo` | 1 | 0 | 1 |
| `sm_vamparena` | 2 | 0 | 2 |
| `sm_warehouse_1` | 53 | 2 | 55 |
| `sp_theatre` | 42 | 0 | 42 |
| `sp_tutorial_1` | 20 | 14 | 34 |
| **Total** | **317** | **109** | **426** |

The density pattern already exposes several design roles. Hubs rely heavily on pedestrian and
maker populations; the warehouse is a dense combat/stealth encounter; the theatre is a placed
stealth population; and the tutorial uses makers to stage disposable teaching encounters.

## How a map defines an NPC

### Common property surface

The following counts are occurrences among the 426 demand rows. A field can be present with a
sentinel or default value; presence is not proof that it changes behavior for every subclass.
The two `npc_payphone` rows account for most of the 424-versus-426 boundary.

| Property family | Fields and occurrence counts | Role established by evidence |
|---|---|---|
| Transform | `origin`, `angles` (426) | Initial placement and facing |
| Perception | `npc_perception`, `vision`, `hearing` (424) | Per-NPC sensory tuning inputs |
| Initial relation | `player_reaction` (424) | Initial combat-AI relationship input |
| Investigation | `investigate_mode`, `investigate_mode_combat`, `pl_investigate`, `full_investigate` (424/233) | Policy and thresholds for investigation |
| Player transgression | `pl_criminal_flee`, `pl_criminal_attack`, `pl_supernatural_flee`, `pl_supernatural_attack` (424) | Authored thresholds for reacting to player conduct |
| Occlusion policy | `percent_occluded_wait`, `cover`, `walk`, `flank`, `chase` (424) | Weighted behavior choices when the target is occluded |
| Character sheet | `stattemplate` (424), `base_gender` (132), `soundgroup` (127) | RPG/stat and presentation identity |
| Ambient behavior | `use_interesting`, `interesting_place_groups` (416), `hint_groups` (423) | Eligibility and grouping for ambient places/hints |
| Combat loadout | `additionalequipment` (267), `alternateequipment` (184), `cantdropweapons` (78) | Equipment and drop policy |
| Grouping | `squadname` (105), `NPCSquadname` (33), `team_name-wesp` (40) | Squad/team authored membership |
| Dialogue | `dialogname` (82), `WillTalk` through script, `usescript` (11) | Dialogue identity and script participation |
| Lifecycle | `StartHidden` (194), `invincible` (193), `level_reset_type` (23), `is_bossmonster` (45) | Visibility, damage, reset, and boss policy |
| Alert overrides | `allow_alert_lookaround` (343), `ignore_detected_attack` (41), `no_alert_state` (39), `stay_entrenched` (233) | Class/state selection modifiers |
| Maker inheritance | `NPCType`, maker flags/count/frequency (109), `NPCTargetname` (108), `NPCSquadname` (33) | Child identity, production, name, and grouping |
| Presentation | `default_disposition` (424), model (416), appearance colors (424), `crossfade_skin_time` (423), `npc_transparent`/`demo_sequence` (356) | Emotional default, body, and visual state |

Other common fields include `physdamagescale`, `floatfreq`, `teleport_move_timer`,
`bright_route_penalty`, `follower_type`, and `combat_start_activity`. Rare fields include
`warn_range`, `detection_distance`, `fright_*`, and `friendship_level`.

Several apparent customization fields are effectively defaults in this corpus:

- every `follower_type` is `Default`;
- every `bright_route_penalty` is zero; and
- `combat_start_activity` is always a sentinel (`-1` on 350 rows and `ACT_INVALID` on 73), so
  the maps do not provide a live combat-start activity override.

That does not prove the native fields are unused globally; it proves the current 22-map authored
slice does not vary them.

### Perception and player-reaction distributions

The following distributions are valuable for faithful defaults and for selecting retail capture
subjects. They do not by themselves decode every numeric value.

| Field | Observed values |
|---|---|
| `npc_perception` | 3: 311; 4: 55; 0: 29; 5: 8; 1: 7; 2: 4; 9: 3; 7: 3; 6: 2; 8: 2 |
| `vision` | -1: 286; 0: 68; 500: 15; 750: 10; 540: 8; 600: 8; 1000: 6; 3000: 6; other values form the tail |
| `hearing` | -1.00: 318; 0: 55; 0.10: 16; 1.00: 8; 0.75: 8; 0.40: 6; other values form the tail |
| `investigate_mode` | 4: 378; other values: 46 |
| `investigate_mode_combat` | 4: 375; other values: 49 |
| `pl_investigate` | 6: 350; -1: 60; 1: 11; 5: 3 |
| `pl_criminal_flee` | 6: 191; 5: 92; 2: 75; -1: 45; other values: 21 |
| `pl_criminal_attack` | 6: 260; 1: 103; -1: 45; other values: 16 |
| `pl_supernatural_flee` | 6: 214; 1: 46; -1: 45; 3: 41; 2: 40; 4: 28; 5: 10 |
| `pl_supernatural_attack` | 6: 260; 1: 101; -1: 48; other values: 15 |

### Player-law observation transaction

The four `pl_*` fields are executable thresholds, not descriptive disposition. The native
condition-gathering pass clears and recomputes `COND_INVESTIGATE_LEVEL` plus four law conditions:

| Condition | ID | Authored threshold |
|---|---:|---|
| `COND_CRIMINAL_FLEE_LEVEL` | 31 | `pl_criminal_flee` |
| `COND_CRIMINAL_ATTACK_LEVEL` | 32 | `pl_criminal_attack` |
| `COND_SUPERNATURAL_FLEE_LEVEL` | 33 | `pl_supernatural_flee` |
| `COND_SUPERNATURAL_ATTACK_LEVEL` | 34 | `pl_supernatural_attack` |

The player clamps activity to `0..5`; an authored threshold of 6 is therefore an effective disable.
This explains the dominant 6 rows above without treating 6 as another attainable crime tier.

One NPC retains independent criminal and supernatural processed counts, witnessed levels,
locations and offender handles, plus `m_bPLSupernaturalActFleeOnly`. For the direct-player lane it
requires a valid `m_hClosestPlayer` and `COND_SEE_PLAYER`, compares the player's monotonically
increasing act count with its own processed count, then tests the current player level against each
threshold. Passing a threshold records the player as offender and raises the corresponding AI
condition. If that channel's observation window is closed, the NPC advances its processed count
without producing the condition, so the same act is not replayed when observation resumes.

A parallel global-event lane carries expiring criminal or supernatural records with severity,
origin and offender. `CAI_BaseNPCTroika` accepts a record only when the origin is inside its view
cone, no farther than `m_flSeekDistInspection`, and reached by an unobstructed trace. Among accepted
records it retains the strongest criminal and supernatural entries independently, then tests both
flee and attack thresholds. This is a visual witness transaction; `TriggerAISound`, hearing and
ordinary enemy acquisition remain separate stimuli.

The pass is suppressed while the NPC has frenzy flag `0x10` or is busy with a dynamic interaction.
NPC spawn zeros the criminal, supernatural and Nosferatu ignore deadlines. Feeding opens both law
windows on the victim for three seconds; entering NPC state 14 opens the criminal window for two
seconds; the closest-player special case opens the Nosferatu window for five seconds. Those are
the complete static callers of the three deadline setters in the pinned DLL.

Condition gathering never mutates Masquerade or spawns police. Schedule selection/translation
requires the retained offender still be the player, submits the retained severity/origin to the
player's incident consumer, and copies the player's current act count into the NPC processed count.
The supernatural flee-only schedule path instead queues a player-owned scare record. Seeing a
nearby `Player_Nosferatu` can create that severity-2 flee-only record independently of a new
supernatural activity count. The downstream player ordering is in
[player-entity.md](player-entity.md).

`player_reaction` combines a relation token and a numeric priority-like value:

| Authored value | Rows |
|---|---:|
| `D_NU 0` | 275 |
| `D_HT 5` | 96 |
| `D_HT 10` | 38 |
| `D_LI 0` | 7 |
| `D_HT 0` | 5 |
| `D_LI 10` | 3 |

The dominance of neutral rows does not mean most NPCs cannot become hostile. Damage, player
criminal/supernatural state, class relations, scripted `SetRelationship`, encounter logic, and
enemy assignment can all change the effective relation or behavior after spawn.

### Templates and inheritance

The 36 `vdata/system/npctemplate*.txt` files declare 150 templates, with 64 non-empty parent links.
One declaration is duplicated in the installed data. Forty-nine template names are referenced by
the current map rows. Common references include `NPCGeneric` (108), `Officer` (64), `Civilian`
(30), `Rat` (24), `WarehouseThug` (24), `VampireGeneric` (23), `Bum` (16), and
`VampireCritical` (15). `VampireGeneric` is itself the common parent of 24 templates.

The template family is a character sheet, not an AI behavior tree. Its fields include:

- template and parent names;
- body, gender, kindred classification, sound group, footfall, and fake-reload policy;
- `AlwaysInnocent` and other character flags;
- attributes, blood, health, abilities, and equipment;
- reactions to and from clan categories; and
- loiter activities.

The native classname still decides which schedule selector and task overrides exist. Two NPCs can
share a stat template but differ in native behavior, while two instances of one native class can
have different sheets, equipment, relations, outputs, and sensory policy.

### Ambient groups and interesting places

`interesting_place_groups` is present on 416 demand rows and contains 56 distinct group strings.
`use_interesting` is enabled on 156 rows and disabled on 260. `interestingplacetypelist.txt`
defines accepted NPC classes/stat templates and weighted activities for types of ambient place.
This is the data-driven loiter/ambient layer: it offers eligible actors places and activities, but
it does not replace their native state, schedule, navigation, or interruption machinery.

`squadname` is present on 105 rows with 28 distinct values. Schedule interrupt data includes
`SQUAD_SEE_ENEMY`, confirming that group knowledge is also a native combat/investigation concern,
not just an editor grouping label.

## Native NPC object and state

The recovered `CAI_BaseNPC` datamap is rooted at `0x105c9814`. Important recovered state includes:

| Offset | Field | Function in the recovered loop |
|---:|---|---|
| `0x1a40` | `m_bShouldMove` | Movement intent/gate |
| `0x1a94` | `m_hLastEnemy` | Previous enemy handle |
| `0x1b28` | force-state-change storage | Forces reconsideration of state |
| `0x5b68`-`0x5b74` | last seen hate/fear/dislike/nemesis handles | Relationship-category memory |
| `0x5b78` | last-heard storage | Auditory memory |
| `0x5b7c`, `0x5b80` | last damage and took-damage state | Damage stimulus memory |
| `0x5c3c` | `m_IdealSchedule` | Requested schedule identity |
| `0x5c40` | `m_ScheduleState` | Active schedule execution state |
| `0x5c54` | fail schedule | Recovery path when a task/schedule fails |
| `0x5ca4` | conditions-gathered state | Prevents inconsistent re-gather within a decision pass |
| `0x5cc0` | `m_NPCState` | Current high-level state |
| `0x5cc4` | `m_IdealNPCState` | Desired high-level state |
| `0x5cdc` | senses component | Sensory gathering |
| `0x5ce0` | enemy handle | Current enemy |
| `0x5ce4` | target handle | Current non-enemy target/goal |
| `0x5cec` | capabilities | What movement/combat actions the NPC may perform |
| `0x5d34` | navigator | Route and movement-goal ownership |
| `0x5d3c` | pathfinder | Path construction |
| `0x5d44` | motor | Physical facing and locomotion execution |

The object also stores delayed conditions, sound conditions, enemy-occlusion state, ideal
sequence/activity/weapon activity, and the last state-change time. This separation matters: an
enemy handle, an alert/combat state, a schedule, a movement goal, and a playing animation are
related but not interchangeable pieces of state.

### `TeleportToEntity` and the next AI admission

`TeleportToEntity` is a `FIELD_EHANDLE` input on `CAI_BaseNPCTroika`, not on the base
`CAI_BaseNPC` map. Its full name conversion and transform contract is owned by
`docs/vtmb/entity_io.md`. The AI-specific tail is virtual slot `+0x998`. All three concrete
receiver families in the current corpus resolve that slot through thunk `0x10010f0f` to
`FUN_102c23f0`: Jack's `CNPC_VVampire`, `CNPC_VHumanCombatant` (vtable `0x104b7ff4`, slot 614),
and `CNPC_VHunter` (vtable `0x104b9784`, slot 614). The function assigns the current game time to
exactly five deadlines:

| Offset | Recovered field | Write |
|---:|---|---|
| `0x17c` | `m_flNextThink` | `curtime` |
| `0x6244` | `m_flNextUpdateThink` | `curtime` |
| `0x6248` | `m_flNextNormalThink` | `curtime` |
| `0x624c` | `m_flNextMoveThink` | `curtime` |
| `0x6250` | `m_flNextAIThink` | `curtime` |

These are scheduling writes, not an AI reset. The handler does not touch the navigator at `+0x5d34`,
the active or ideal schedule, movement goal, enemy/target handles, NPC state, conditions, activity,
sequence, velocity, or the authored `teleport_move_timer` field at `+0x65dc`. For an ordinary queued
map output, `GameFrame` has already completed its think phase before the teleport input is serviced,
so the newly due work begins on the next server frame. The input also forces network transmission for
one second, which makes the discontinuous placement observable without changing decision state.

### Map creation, spawn, activation, and first AI admission

RE47 pins the general bootstrap to the retail DLL instead of borrowing the similar Source SDK
implementation. For a normal fresh map, the recovered order is:

1. `CServerGameDLL::LevelInit` (`0x1011a7a0`) enters the map-entity loader. The loader reads one
   block, resolves its `classname`, constructs the registered native class, and sends the complete
   block through the entity's map-data/keyvalue path (`0x10136b40`).
2. Unparented entities enter `DispatchSpawn` (`0x101d1280`) immediately. Parented entities are
   collected, hierarchy-sorted, attached, and spawned afterward. `DispatchSpawn` invokes virtual
   slot `+0x19c`, rejects an entity deleted or marked for deletion by `Spawn`, then completes its
   post-spawn bookkeeping.
3. A `npc_VVampire` uses `CNPC_VVampire::Spawn` at `0x103c4ef0`. Its recovered chain enters the
   Human/Troika spawn body at `0x10298d30`, which performs model, hull/solid, capability, equipment,
   cached-transform and Troika state setup around `CAI_BaseNPC::Spawn` at `0x10273200`. The base
   spawn performs its AI-admission gate, optional equipment path and base combat-character spawn.
4. Only after creation/spawn finishes does `ServerActivate` (`0x1011aaf0`) iterate every surviving
   server entity and invoke virtual `Activate` (`+0x1c4`) before post-entity systems run. Spawn and
   activation are therefore distinct passes; BSP entity order is not permission to interleave one
   actor's activation with the next actor's keyvalue load.
5. The NPC initialization think at `0x10273aa0` first applies an authored relationship override
   when present, then invokes virtual `+0x698` followed by `+0x694`. The `CAI_BaseNPC` `+0x698`
   body (`0x10273ad0`) performs readiness work: clears initialization flags, repairs ground
   placement where applicable, resolves an optional target, installs the ordinary AI think, and
   applies target/spawnflag-driven state/schedule changes. `CNPC_VVampire` inherits the Troika
   override at `0x1029a8b0`: it calls that base body, reinstalls the AI think, resolves follower
   activity/distance data, and records the closest player handle. It does not turn toward that
   player. The base `+0x694` body at `0x101a6540` is empty.
6. Ordinary `RunAI`, condition gathering, state/schedule selection, motor work and activity
   maintenance occur only after that admission path.

The recovered concrete `npc_VVampire` path contains no turn-to-player operation and no direct call
to `SetActivity`/`SetIdealActivity` during the traced Spawn → Activate → concrete `NPCInitThink`
chain.
This is a negative call-path result, not proof that the character remains static: the subsequent
AI schedule, motor, interesting-place, gaze, dialogue and scripted-scene owners can all change
facing or presentation. Static recovery also does not identify the exact sequence visible on the
first rendered frame or the exact frame of the first ordinary AI schedule. Those require a
hash-gated live capture; a rebuild must not fill the gap by inventing an unconditional idle or
face-player action.

### AI update loop

`CAI_BaseNPC::RunAI` at `0x1026f110` begins a decision pass by clearing the gathered marker. Unless
a script owner prevents native gathering, it calls the virtual condition-gathering path, updates
state/decision policy, maintains or runs the schedule through the path at `0x102817c0`, and clears
transient conditions at the end of the pass. The base `GatherConditions` path at `0x1026ec30`
integrates senses, sound, damage, and current state into condition flags.

Schedule selection is not a direct switch from one stimulus to one animation. The wrapper at
`0x102814d0` ensures conditions are gathered, invokes the selector, resolves global schedule IDs
through the owning schedule space, and stores the ideal schedule. The active schedule then runs
its tasks over subsequent AI updates until it completes, fails, or an interrupt condition forces
reselection.

The base selector at `0x1028a380` switches on the high-level state. Recovered cases are:

| Case | State |
|---:|---|
| 0 | none/uninitialized; warns rather than behaving normally |
| 1 | idle |
| 2 | combat |
| 3 | alert |
| 4 | scripted |
| 6 | prone |
| 7 | dead |
| 12 | class-specific/fallback idle-like branch; exact semantic label unresolved |

The numbering is directly constrained twice: `CAI_BaseNPC::SelectIdealState` (`0x1026f660`) emits
`Combat state with no enemy` from case 2 and falls back to state 3, while the concrete human
selectors enter their weapon/combat policy only for state 2. The combat branch further tests enemy
state, damage, attack capability, range/occlusion, and other conditions before choosing a
schedule. Derived VtMB NPC classes override or extend the selection and task machinery, which is
why the native class is a load-bearing part of authored NPC identity.

### The idle branch, decided

`CNPC_VVampire` has no `SelectSchedule` of its own: vtable `+0x6d8` is `CNPC_VHuman::SelectSchedule`
(`0x10384ee0`, 12 clan classes), which handles only `m_NPCState == 2` and tail-calls
`CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`). **There is no `SCHED_VVAMPIRE_IDLE_STAND` in the
binary** — the only `SCHED_VVAMPIRE*` names are the two VampireBoss transform schedules.

`0x102af660` `case 1` evaluates in this order, and the first match wins:

| Step | Test | Result |
|---:|---|---|
| 1 | `IsBusyWithDiscipline` (`0x1033e2b0`, returns `this+0x14b8 & 1`) **or** `m_bInChoreoScene(+0x5bc4)` | `SCHED_TROIKA_IDLE_DISPOSITION` `0x6b` |
| 2 | virtual `+0x97c` (`0x102b93c0`) returns non-zero | returned verbatim |
| 3 | a patrol path is present | the patrol schedule |
| 4 | `m_bUseInteresting(+0x63d9)` | `0xff` SETUP, or `0x102`/`0x106`/`0x105` on the crosswalk/interact/loiter conditions, else `0x100` |
| 5 | `m_bAllowAlertLookaround(+0x6434)` and `RandomInt(0,99) < min(30, (m_iEnemySightings(+0x60a8)+2)*5)` | `SCHED_TROIKA_ALERT_LOOK_AROUND_NI` `0x4f` |
| 6 | `m_hBlockedDoor` or `m_hCondHitByDoor` is valid and `SelectDoorObstructionSchedule` (`0x102b7370`) returns non-zero | returned verbatim |
| 7 | `m_bReturnToInitialPos(+0x6494)` | `0x45`, else `SCHED_TROIKA_IDLE_DISPOSITION` `0x6b` |

`SCHED_TROIKA_IDLE_STAND` (`0x44`) is returned only for state `0xd`; `_NT` (`0x6c`) is never
returned here. Registered IDs decoded from their registration sites: `0x44` `SCHED_TROIKA_IDLE_STAND`,
`0x45` `SCHED_TROIKA_IDLE_RETURN_TO_INITIAL`, `0x4f` `SCHED_TROIKA_ALERT_LOOK_AROUND_NI`,
`0x6b` `SCHED_TROIKA_IDLE_DISPOSITION`, `0x6c` `_NT`, `0xff`
`SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP`, `0x100` `SCHED_TROIKA_WALK_TO_INTERESTING_PLACE`,
`0x102` `SCHED_TROIKA_WAIT_AT_CROSSWALK`, `0x105` `SCHED_TROIKA_LOITER`, `0x106`
`SCHED_TROIKA_INTERACT`.

Two consequences carry weight. **An NPC inside a choreographed scene always takes the disposition
stance**, ahead of `use_interesting`, patrol and alert-lookaround. And **virtual `+0x97c` is the
follower controller, not a general priority hook**: it returns `0` immediately unless the follow
target EHANDLE at `+0x647c` is valid, and otherwise picks
`SCHED_TROIKA_FOLLOWER_BACKAWAY`/`_FOLLOW_WALK`/`_FOLLOW_RUN`/`_WAIT` (`0x10c`/`0x112`/`0x113`/`0x115`)
by comparing squared distance against the three radii at `+0x6484`/`+0x6488`/`+0x648c`, which a
`follower_type` row supplies.

`SCHED_TROIKA_IDLE_DISPOSITION` is `TASK_SPECIAL_IDLE_ACTIVITY 5; TASK_WAIT_PVS 0`, and
`SCHED_TROIKA_ALERT_LOOK_AROUND_NI` is
`TASK_SET_ACTIVITY ACT_ALERT_FIDGET_LOOKAROUND; WAIT 3; WAIT_RANDOM 3; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 2`.
What those idle tasks commit, and how a stance is chosen, is the disposition stance machine in
`animation_and_movers.md`.

`SCHED_TROIKA_IDLE_RETURN_TO_INITIAL` (`0x45`) is the selector's other terminal answer, and unlike
`_IDLE_DISPOSITION` it navigates:

```
TASK_SET_LASTPOSITION_TO_INITIAL 0 ; TASK_SET_TOLERANCE_DISTANCE_ABS 5
TASK_GET_PATH_TO_LASTPOSITION 0    ; TASK_WALK_PATH 0
TASK_WAIT_FOR_MOVEMENT 0           ; TASK_FACE_LASTANGLE 0
TASK_CLEAR_LASTPOSITION 0
Interrupts  COND_NEW_ENEMY COND_SEE_ENEMY COND_SQUAD_SEE_ENEMY COND_SEE_FEAR
            COND_LIGHT_DAMAGE COND_HEAVY_DAMAGE COND_GIVE_WAY COND_INVESTIGATE_SOUND
            COND_INVESTIGATE_SIGHT COND_IGNORE_UNKNOWN COND_DETECTED_ATTACK COND_PLAYER_ON_HEAD
```

### `m_bReturnToInitialPos` is a one-shot armed only by alert or combat

`CAI_BaseNPCTroika + 0x6494` decides between those two terminal answers, and it carries **no
external name** in the datamap (`0x105ce470`), so no map, FGD property or entity input can set it.
`CAI_BaseNPCTroika`'s spawn (`0x1029a0b0`) clears it, and the selector (`0x102af660`) consumes it:
reading it true both returns `0x45` and writes it back to false in the same branch.

The only writer is `OnStateChange` (`0x102ae140`, `CNPC_VVampire` vtable slot `[463]`), which sets it
on transition **into ALERT (2) or COMBAT (3)** — plus custom states `8`, `0xb` and `0xe`. `DEAD` (7)
and the `default` case do not, and **`SCRIPT` (4) falls into that default**, so entering or leaving a
`scripted_sequence` never arms it.

The consequence is load-bearing for authored scenes: an NPC that has only ever sat under script
control has the flag clear, so when a beat releases it the selector returns
`SCHED_TROIKA_IDLE_DISPOSITION` — which carries no navigation task at all. **A released, enemy-less
NPC stands still.** It can only move in that gap by first entering alert or combat, which also arms
the walk-home for its next idle reselection.

### Door-obstruction schedule selection

`FUN_102b7370` at `0x102b7370` is the Troika NPC's door-obstruction selector; the working semantic
name is `CAI_BaseNPCTroika::SelectDoorObstructionSchedule`. It is not an idle or disposition
helper. Idle state calls it only when `m_hBlockedDoor(+0x5d28)` or
`m_hCondHitByDoor(+0x5d2c)` is valid, and combat state calls it after the two higher-priority
combat helpers. A non-zero result is a selected schedule returned through
`CAI_BaseNPCTroika::SelectSchedule`; zero means this policy declined to handle the obstruction.

The selector first rejects an NPC that already owns `m_pHintNode(+0x5ddc)`. It then chooses the
obstruction source in this order:

1. A valid `m_hBlockedDoor`, provided the door's expiry value at `+0x640` is later than game
   `curtime`; an expired handle is cleared.
2. Under `COND_ENEMY_UNREACHABLE` (`0x59`), no active movement-state entry at `+0x5bb0`, and a
   valid timed record rooted at `+0x5da4`, the entity held by that record.
3. A valid `m_hCondHitByDoor`, but only while `COND_HIT_BY_DOOR` (`0x34`) is set.

With a source selected, the eligible path records its handle at `+0x6448` and asks the ordinary
hint-node machinery to find and claim cover. A claimed medium-cover, low-cover or corner-cover
context (type `100`, `101` or `0x27d8`) returns
`SCHED_TROIKA_TAKE_COVER_HINT_DOOR` (`0x9c`). Any other claimed hint is released with a five-second
delay and the selector returns zero.

When no hint is claimed, the source origin becomes `m_vSavePosition(+0x5dd0)`. The squared
NPC-to-source distance is compared with `65536.0` (256 units), and virtual `+0x29c` is
`GetEnemy`; the resulting schedules are:

| Distance from source | Enemy | Schedule |
|---:|---|---|
| at most 256 units | present | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR` (`0x90`) |
| at most 256 units | absent | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE` (`0x91`) |
| over 256 units | present | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT` (`0x94`) |
| over 256 units | absent | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE` (`0x96`) |

The nearby schedules perform repeated step-backs and fail over to their run variants. The distant
schedules wait while facing the enemy, or the saved obstruction position in the `_NE` variants.
This makes the helper a complete obstruction reaction policy: cover at a compatible door hint,
back away while near the obstruction, and wait once already clear of it.

### Interesting-place eligibility

`TASK_FIND_INTERESTING_PLACE` (task 164) is Troika `StartTask` case 48, body `0x102a1f23`: it calls
`PickRandomInterestingPlace` (`0x102db590`), stores the result at `+0x62ec`, then requires
`PickSpotFor` (`0x102da0d0`) to sample a free spot inside the node's bounds; either failure is
`TaskFail(0x22)`.

`BuildCandidates` (`0x102db470`) walks the global place list (head `DAT_10927194`, next at `+0x540`)
at **rating 5 down to 0**, takes the first rating level that yields any eligible node, and picks
uniformly within it. Eligibility (`0x102dad60`) is:

```
place->+0x57c != 0                                   // authored `enabled`
place->+0x57d == 0                                   // runtime-disabled
(place->+0x584 - place->+0x58c - place->+0x588) > 0  // max_npcs - reserved - users
(place->+0x574 & npc->+0x62dc) != 0                  // group mask & interesting_place_groups
|place - npc|² <= 1.0e8                              // 10000 units, straight line
```

There is **no pathfinding and no line-of-sight test in the find stage** — a node 9,000 units away
behind a wall is eligible, and the walk schedule is where such a goal fails. The entity classname
is `intersting_place`, misspelled in VtMB itself.

`vdata/system/interestingplacetypelist.txt` binds a node's `type` to activities. `wall_lean` is
`ACT_WALL_LEAN_INTO` → `ACT_WALL_LEAN_IDLE` → `ACT_WALL_LEAN_OUTOF`, and unlike `sitting` and the
`conversation_*` types it declares no `AcceptedClasses` block, so any NPC may claim one.

### `no_alert_state` does not suppress the alert state

Datamap offsets, from the `typedescription_t` records: `m_bNoAlertState` `0x65f6`,
`m_bAllowAlertLookaround` `0x6434`, `m_iNPCPerception` `0x63b0`, `m_flSeekDistBase` `0x63b4`,
`m_flHearingScalarBase` `0x63bc`.

The keyvalue does real work in four places — `TASK_SUGGEST_STATE` rewrites a request for state 3 to
state 1 (`0x102a1bc8`); `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`) skips its damage and
sense promotions; `CNPC_VHuman::SelectIdealState` (`0x103851e0`) and
`CNPC_VHumanCombatPatrol::SelectIdealState` (`0x10387380`) gate theirs on it; and `CNPCMaker`'s
spawn helper (`0x10310c10`) puts a child into state 1 rather than 3.

It is nonetheless **not a suppression**. `0x102ad660` ends in an unconditional
`return CAI_BaseNPC::SelectIdealState(this)` (`0x1026f660`), whose `case 1` promotes idle → alert on
`COND_LIGHT_DAMAGE`, `COND_HEAVY_DAMAGE` and the whole hear-family **with no `m_bNoAlertState`
test**. A second route is independent of it entirely: `SCHED_TROIKA_ALERT_LOOK_AROUND_NI` is
selected from state 1 gated only on `m_bAllowAlertLookaround` and its chance roll.

`InitPerceptionDistances` (`0x1028fb70`) reads `vision` and `hearing` as sentinels: **`-1.0f` means
derive from `npc_perception`**, indexing the `Inspection_Vision_Distances` and
`Inspection_Hearing_Scalars` tables into the effective values at `+0x63b8` and `+0x63c0`; any other
authored value is copied through and `npc_perception` is then inert for that NPC.

## Perception, sound, memory, and hostility

### Visual and auditory input

Map fields provide per-actor sensory tuning. The exact player-light target surface, visual-range
and cone scaling, closest-player LOS cache, sound-radius reduction, and found/lost transition are
owned by [stealth.md](stealth.md). `sound_volume_table.txt` classifies sound levels and occlusion:

| Level/example | Radius/value | Occlusion behavior |
|---|---:|---|
| quiet | 180 | occludable |
| normal | 240 | occludable |
| loud | 1200 | non-occluded |
| player stealth | 120 | authored special value |
| feeding | 240 | authored special value |

Named sound categories include footsteps, gunshots, impacts, `NPC_TAKE_DAMAGE`, `NPC_FLEE`,
`NPC_DISCIPLINE_ALERT`, doors, explosions, and physics sounds. A combat incident can therefore
propagate to actors that did not see the original attacker. This matches both the native
`HEAR_COMBAT` interrupt condition and the authored `OnHearCombat` output.

The native object retains last-seen relation-category targets, last-heard stimulus, damage state,
current enemy, last enemy, and occlusion state. Perception is thus a memory-producing subsystem,
not a per-frame yes/no raycast. Losing current line of sight does not necessarily erase the enemy
or force a return to idle.

### Relationship table, exactly decoded

`InputSetRelationship` at `0x10273790` parses one or more triples:

```text
[entity name | entity class | player] [D_* token] [priority]
```

The exact token-to-native relation mapping is:

| Token | Internal value | Meaning |
|---|---:|---|
| `D_HT` | 1 | hate/hostile |
| `D_FR` | 2 | fear |
| `D_LI` | 3 | like |
| `D_NU` | 4 | neutral |

The parser accepts repeated triples in one input. A target ending in `*` wildcard-matches entities
and installs entity relationships. An exact target resolves an entity override. `player` has a
special class mapping when no named entity resolves; another unresolved token can resolve as a
classname and install a class relationship.

`AddClassRelationship` at `0x10332aa0` and `AddEntityRelationship` at `0x10332ca0` update an
existing row or grow their storage in groups of five. The rows retain target handle or class,
disposition, and numeric priority. The tables are save-backed. The diagnostic text describes a
priority range of 1-10, but the parser uses raw integer conversion without clamping; the installed
corpus includes zero and 99. `IRelationPriority` at `0x10333700` uses the exact entity row before
the class row, returns the row's raw integer, and otherwise returns 5 for a non-null actor or 0 for
null. Higher priority wins enemy arbitration; out-of-diagnostic-range values therefore remain
ordered rather than being clamped.

`IRelationType` at `0x10333340` resolves an entity override before a class override and otherwise
returns neutral, with additional special-flag/oblivious branches. Self and null relations resolve
neutral. The proven ordinary-table precedence is therefore:

```text
exact entity relationship
  -> class relationship
  -> neutral default
```

The exact placement and meaning of the additional special branches remain open, but the ordinary
relation-type and priority precedence is closed.

The map's `player_reaction` is an initial authored input to this combat relationship domain. A
later `SetRelationship` call can deliberately alter it.

### Enemy acquisition and replacement

The ordinary target-selection transaction is recovered from the pinned retail DLL. In
`CAI_BaseNPC::GatherConditions` (`0x1026ec30`), senses and hostile-category conditions are gathered,
the enemy-memory component refreshes its records, `ChooseEnemy` (`0x10279dd0`) runs, the
new-enemy-condition repair at `0x1026fb40` runs, and only then are conditions for the committed
current enemy gathered. Candidate discovery, memory, enemy choice and attack capability are
separate stages.

**`BestEnemy` enumerates the NPC's own enemy memory, never the world entity list.** It fetches the
memory component through vtable `+0x874` and walks the linked list at component `+0xc`: each entry
holds the remembered actor's handle at `+0x24`, its eluded byte at `+0x35`, and the next entry at
`+0x38`. `0x102e0210` walks the same list to answer the eluded test for one candidate. An actor the
NPC has never sensed has no entry, so it is not a candidate at all — perception is the admission
stage, and the arbitration below only ever ranks what memory already holds. This is why an NPC
authored with `vision 0` and a near-zero `hearing` cannot acquire an enemy, and therefore cannot
enter `COMBAT`, however hostile its `player_reaction` row.

Within that list `BestEnemy` (`0x102743c0`) considers only a handle that resolves to a living actor
other than self, passes the ordinary owner/flag and virtual `IsValidEnemy` gates, has relation
`D_HT` or `D_FR`, and does not carry the eluded marker. It then arbitrates as follows:

1. A reachable candidate beats an unreachable candidate.
2. At the same reachability class, larger `IRelationPriority` wins.
3. At equal priority, smaller integer enemy distance normally wins.
4. Visibility modifies that last comparison: a visible candidate can displace a farther unseen
   incumbent, while a closer unseen candidate displaces only an unseen incumbent. Selection is
   therefore not simply nearest hostile actor.

The current enemy is sticky. `ShouldChooseNewEnemy` (`0x10279d00`) declines a search when an
unnamed internal bit at `+0x14bc` (`0x10000`) is set. Otherwise it searches immediately when there
is no current enemy, when that actor is dead, when its enemy-memory record is marked eluded, or
when `SEE_HATE` (`0x43`), `SEE_DISLIKE` (`0x45`), `SEE_NEMESIS` (`0x5b`) or `ENEMY_DEAD` (`0x58`)
is present. A living, non-eluded current enemy with none of those conditions remains selected.
Notably, this retail body does not test `SEE_FEAR`, although a remembered `D_FR` candidate is
eligible in `BestEnemy`.

Even a better candidate does not automatically pre-empt the behavior in progress. The active
schedule's interrupt mask is consulted first: ordinary replacement needs `NEW_ENEMY` (`0x54`), an
eluded/went-null path needs `LOST_ENEMY` (`0x47`), and a dead target needs `ENEMY_DEAD` (`0x58`). A
schedule uninterested in the relevant condition keeps ownership and enemy choice is skipped; a
null enemy under such a schedule produces a retail warning rather than a plausible fallback.
This schedule gate is the starvation rule a one-frame global target scorer would miss.

When the choice changes, `SetEnemy` (`0x10279a50`) first transfers the old handle through the
last-enemy path and notifies the prior-enemy hook, then writes `m_hEnemy` at `+0x5ce0`; a non-null
enemy is also registered with the response system. `ChooseEnemy` clears stale had-enemy/player
memory, sets `ENEMY_DEAD` for a dead old target, sets or clears `NEW_ENEMY`, vacates an occupied
strategy slot and forgets the previous LOS claim. An eluded target that resolves to null adds
`LOST_ENEMY`, emits the lost-enemy sound hook, and fires `OnLostPlayer` or `OnLostEnemy` according
to the remembered target kind. A non-null replacement records whether it is the player or another
enemy. `OnFoundPlayer`/`OnFoundEnemy` belong to the sensory observation path; they are not aliases
for the `m_hEnemy` write.

This yields the implementation order:

```text
sense / hear / take damage / script relation
  -> update category conditions and enemy-memory records
  -> schedule interrupt-interest gate
  -> ShouldChooseNewEnemy
  -> BestEnemy eligibility and arbitration
  -> SetEnemy plus last-enemy, condition, slot and lost-output effects
  -> gather range, LOS, facing and attack conditions for that committed enemy
  -> state and class schedule selection
```

### Emotional disposition and social reaction are different domains

`SetDisposition` addresses an emotional/dialogue component at character offset `+0x98`, not the
combat relationship rows above. `DispositionTable.txt` defines presentation states including
`Neutral`, `Anger`, `Joy`, `Sad`, `Fear`, `Disgust`, `Apathy`, `Dead`, `Sitting`, `Bartender`,
`ChairDamaged`, `PrinceSitting`, and `BehindBack`. Neutral is the first/fallback inheritance entry.
These states select expression, stance/fidget, eye target, and blink behavior.

Separately, `reaction.txt` divides an RPG/social score into named bands:

| Lower boundary | Label |
|---:|---|
| 0 | Want To Kill |
| 20 | Hatred |
| 40 | Dislike |
| 60 | Neutral Reaction |
| 80 | Admire |
| 100 | Love |
| 9999 | Obsession |

`reactions000.txt` modifies that score based on histories and Disciplines; examples include
Megalomaniac scaling, a Close to Beast penalty against kindred and kine, and Presence bonuses.
Those tables serve social/RPG reaction calculations. A label such as `Hatred` in this scale is not
evidence that the native AI relation table contains the corresponding `D_HT` row.

## Schedules and tasks: the behavior program

The recovered native schedule corpus contains 691 schedules and 4,139 task invocations over 441
distinct task identities. A schedule is an ordered task program with failure and interrupt policy;
it is not an animation clip and it is not merely a state label. The schedule can ask for a path,
wait for movement, face an entity, remember a condition, change an activity, attack, wait, or
switch to another schedule. A class-specific `StartTask` or `RunTask` body performs work that the
declarative schedule cannot express alone.

### Registration and override surface

The DLL registers 330 shared and 184 class-local task identities across 20 owners. The recovery
finds 29 `StartTask` bodies, 24 `RunTask` bodies, and 49 custom handler bodies. Thirty-one of those
custom handlers bear animation policy and 18 do not. The action-policy survey resolves 100 policy
rows and 111 routes. This is the native implementation seam where a generic schedule becomes
police, civilian, vampire, animal, boss, or other class-specific behavior.

The most frequent tasks show the structure of the library:

| Task | Invocations |
|---|---:|
| `SET_FAIL_SCHEDULE` | 328 |
| `SET_NPC_FLAG` | 312 |
| `STOP_MOVING` | 275 |
| `WAIT_FOR_MOVEMENT` | 230 |
| `SET_ACTIVITY` | 216 |
| `WAIT_RANDOM` | 213 |
| `WAIT` | 203 |
| `SET_TOLERANCE_DISTANCE` | 175 |
| `SET_SCHEDULE` | 145 |
| `RUN_PATH` | 133 |
| `FACE_ENEMY` | 69 |
| `REMEMBER` | 41 |
| `GET_PATH_TO_ENEMY` | 37 |

The tail contains path-to-cover, flank, flee, attack, dialogue, scripted, follower, and other
specialized tasks. Movement tasks target the navigator/motor layer. `SET_ACTIVITY` changes an
abstract activity, which then passes through class/weapon translation and weighted model-sequence
selection; the full activity-to-sequence chain is documented in
[animation_and_movers.md](animation_and_movers.md).

Schedule identifiers are **per-class, not global**. Every class registers its own name table, and
the same number names a different schedule in each one. `0x156` is `SCHED_VTZIMISCE_TEST` in
`CNPC_VTzimisce`'s table, `SCHED_TZIMISCEHEADCLAW_WAIT_FOR_MELEE_ADVANCE` in
`CNPC_VTzimisceHeadClaw`'s, `SCHED_VTZIMISCERUNNER_WAIT_FOR_MELEE_ADVANCE` in
`CNPC_VTzimisceRunner`'s, `SCHED_VMING_XIAO_TRANSFORM` in `CNPC_VMingXiao`'s,
`SCHED_VANIMAL_WALK_TO_INTERESTING_PLACE_SETUP` in `CNPC_VAnimal`'s, and
`SCHED_VWEREWOLF_CONSIDER_SITUATION` in `CNPC_VWerewolf`'s. A bare `return 0x156` from a
`TranslateSchedule` body therefore states nothing until the receiving class's own table is read,
and asking who reaches a schedule by scanning the module for its numeric identifier yields a false
hit for every other class that happens to register at that number.

### The `TASK_TEST*` scaffolding is inert

`CAI_BaseNPC` registers five test tasks in `FUN_10316ff0` — `TASK_TEST1` (`0xa7`) through
`TASK_TEST5` (`0xab`), immediately after `TASK_PAUSE_MOVING` (`0xa6`). Two carry working handlers
in `CAI_BaseNPCTroika::StartTask` (`0x102a1910`), each gated by its own console variable. The
other three reach the default arm, which tail-calls `CAI_BaseNPC::StartTask` (`0x102827f0`).
**No shipped code path runs any of the five.**

`StartTask` dispatches on `pTask->iTask` over the range `5`–`0x149` through a byte index table at
`0x102a7ab8` and a jump table at `0x102a77f8`; `TASK_TEST1` lands at `0x102a1943` and `TASK_TEST2`
at `0x102a1a60`. The Ghidra case labels for this switch are compressed jump-table indices, not
task identifiers, so the two must be resolved through the byte table before a case is named.

The two console variables are ordinary `ConVar`s in `vampire.dll`, both defaulting to `"0"` with
flags `0` — no `FCVAR_CHEAT`, `FCVAR_ARCHIVE` or `FCVAR_REPLICATED` — and both sharing the help
string "Toggles stuff for the test task." There is no `client.dll` counterpart.

| | `debug_test_switch1` | `debug_test_switch2` |
|---|---|---|
| object | `0x10924678` | `0x10924630` |
| initializer | `FUN_1028b5f0` | `FUN_1028b680` |
| gates | `TASK_TEST1` | `TASK_TEST2` |
| read at | `0x102a1950`, `0x102a195f` | `0x102a1a62`, `0x102a1a75` |

Each read is an inlined `ConVar::GetInt()`: `m_pParent` at `+4`, `IsCommand()` through vtable slot
1, and the value from `m_nValue` at `+0x2c`, taken as zero when `IsCommand()` is true. No
absolute-addressed write to either object exists anywhere, because the constructor assigns
`m_pParent = this` through `ECX`; the objects are reachable only as the `this` operand of their
own constructor and destructor.

`TASK_TEST1` hides or unhides the NPC's active weapon. With the variable at its default `0` it
calls `CBaseEntity::Hide` (weapon vtable `+0x108`); nonzero calls `CBaseEntity::Unhide`
(`+0x10c`). Those set and clear `EF_NODRAW` (`0x40`) in `m_fEffects`, or in
`m_fScriptSavedEffects` when `m_bScriptHidden` is set, so a script-hidden weapon stays hidden.
The task then unconditionally jitters two axes of the NPC's origin by `RandomFloat(-200, 200)` and
draws `NDebugOverlay::Box(pos, (-2,-2,-2), (2,2,2), 192, 255, 192, 0, 2.0)`. It does not complete
itself.

`TASK_TEST2` forces an activity through `RestartIdealActivity` and then completes the task. The
arm is selected by a four-entry jump table over the values `1`–`4`:

| value | activity | id |
|---|---|---|
| 1 | `ACT_AIM` | `5` |
| 2 | `ACT_CORNER_COVER_IDLE` | `0x1118` |
| 3 | `ACT_RANGE_ATTACK1` | `0x19` |
| 4 | `ACT_DRYFIRE` | `0x58` |
| 0 or any other | `ACT_IDLE` | `1` |

`RestartIdealActivity` (`0x10289ee0`) zeroes `m_IdealActivity` (`this+0xfec`) when it already
holds the requested activity before setting it, so the clip restarts even when the activity does
not change. The completion helper (`0x10273e80`) writes `TASKSTATUS_COMPLETE` (`4`) to
`this+0x5c44`.

Three schedules contain these tasks, and nothing selects any of them:

| schedule | id | task program |
|---|---|---|
| `SCHED_TASK_TEST1` | `0x152` | `TASK_TEST1 0`, `TASK_TEST2 0`, `TASK_WAIT 3` |
| `SCHED_TASK_TEST2` | `0x153` | `TASK_TEST2 0`, `TASK_WAIT 3` |
| `SCHED_VTZIMISCE_TEST` | `0x156` | `TASK_SET_INTERRUPT_TIME 4`, `TASK_TEST1 0`, interrupts on `COND_INTERRUPT_TIME` |

`SCHED_TASK_TEST3` (`0x154`) and `SCHED_TASK_TEST4` (`0x155`) hold the two handlerless tasks. The
first three identifiers belong to the Troika table, the last to `CNPC_VTzimisce`'s own — see the
per-class identifier note above, which is what makes `0x156` look reachable from several
`TranslateSchedule` bodies that in fact name their own schedules.

Unreachability is established by exhaustion rather than by absence of an obvious caller. Scanning
the whole of `.text` for `0x152`–`0x156` in every immediate form — `PUSH imm32`, `MOV <reg>,imm32`
across seven registers, `CMP EAX,imm32`, `MOV [ESP+d],imm32`, `MOV [EBP-d],imm32` — and resolving
each hit to its containing function returns only other classes' schedule-table builders, the
activity registry (a separate identifier space), and one unrelated comparison in
`CNPC_VWerewolf::StartTask`. A by-name path is ruled out separately: every reference to a schedule
name string in the module is its own registration, and the module holds no name-to-identifier
lookup for schedules.

The trigger this scaffolding was built around is present and dead. `debug_test_schedule` (object
`0x10924db0`, initializer `FUN_1028b550`) is a `ConVar` built through the min/max overload —
default `"0"`, flags `0`, clamped to `[0, 4]`, help "Forces the AI into test schedules." — which
matches `SCHED_TASK_TEST1`–`4` exactly. It has **no readers**: a scan of `.text` for the object and
for its `m_pParent`, `m_flValue` and `m_nValue` fields finds only the constructor and destructor.

Nothing here is a game rule and none of it reaches a frame a player sees, so the rebuild
reproduces none of it. Its remaining value is as an oracle: the `TASK_TEST2` arms state five
activity identifiers unambiguously, and the handler pair states `RestartIdealActivity` and
`CBaseEntity::Hide`/`Unhide` semantics without any surrounding gameplay to disentangle.

### Schedule families

Name-family counts are a useful index, not an exclusive taxonomy: a schedule can participate in
more than one conceptual concern and not every relevant name shares a prefix.

| Family substring | Schedule count | Typical concern |
|---|---:|---|
| `INVESTIGAT` | 32 | Visual/sound anomaly investigation |
| `COVER` | 25 | Find, occupy, or leave cover |
| `COMBAT` | 20 | Combat transitions and positioning |
| `SCRIPT` | 20 | Script-owned entry, wait, and cleanup |
| `FOLLOW` | 16 | Follow target/leader policy |
| `COWER` | 13 | Civilian fear and shelter behavior |
| `FLEE` | 11 | Escape from threat |
| `DODGE` | 10 | Avoid an incoming threat/attack |
| `PATROL` | 10 | Path-based patrol behavior |
| `CRIMSUSP` | 6 | Criminal/suspicion response |
| `DISPOSITION` | 5 | Emotional/disposition-related action |
| `BLOCK` | 4 | Defensive blocking |
| `DIALOG` | 3 | Dialogue-linked control |

The library also contains start-combat, chase, ranged/melee attack, flank, reload/equip, feeding,
prone, animal, follower, boss, and class-specific schedules outside those simple prefixes.

### The incapacitation tasks and the NPC flag word

`CAI_BaseNPCTroika::StartTask` (`0x102a1910`) and `RunTask` (`0x102aacf0`) are two-level MSVC jump
tables Ghidra does not recover. `StartTask`'s dispatch at `0x102a1938` indexes `taskID - 5` through
byte table `0x102a7ab8` into dword table `0x102a77f8`; `RunTask`'s at `0x102aad03` indexes
`taskID - 2` through `0x102ac844` / `0x102ac760`. `[VtMB]`

| Task | ID | `StartTask` arm | `RunTask` arm |
|---|---:|---|---|
| `TASK_SET_NPC_FLAG` | `0x100` | `0x102a585d` | none |
| `TASK_CLEAR_NPC_FLAG` | `0x101` | `0x102a58ae` | none |
| `TASK_SET_MISC_FLAG` | `0x102` | `0x102a5886` | none |
| `TASK_MAKE_OBLIVIOUS` | `0x131` | `0x102a72e3` | none |

All four are `StartTask`-only: they perform their write and call `TaskComplete` (`0x10273e80`) on
the think that begins them. `[VtMB]`

#### `TASK_SET_NPC_FLAG` and the 62-name flag vocabulary

The operand `NPCFlag:<name>` is resolved at schedule-load time by the `strcmpi` chain `0x1030cbd0`,
which returns the bare mask for a word-one name and `0x80000000 | bit` for a word-two name; an
unknown name is an `Error`. The task arm routes on that sign bit into `m_bfAINPCFlags` (`+0x14b8`,
`0x102a97a0`) or `m_bfAINPCFlags2` (`+0x14bc`, `0x102a9800`). Word two's bit 31 is therefore the
routing marker, not a flag. `TASK_CLEAR_NPC_FLAG` is the exact mirror (`0x102a97d0` / `0x102a9830`).
`[VtMB]`

`m_bfAINPCFlags` (`+0x14b8`), bit 0 → 30:

`D_IS_BUSY`, `DO_STARTLED`, `AT_CROSSWALK`, `PRESERVE_PATH`, `FINDING_BODY`, `CARRYING_BODY`,
`NAV_IGNORE_NPC`, `IN_FLEE_SCHED`, `INITIAL_FLEE`, `COWER_PATH`, `COWERING`, `DODGING`,
`MADE_HUNT_PATH`, `AT_COVER_HINT`, `ANIM_MOVEMENT`, `DONE_EXTRAPOLATING`, `FORCE_RELAXED_ANIMS`,
`SLEEPING`, `BOTCHED_ATTACK`, `NO_DIALOG`, `SKIPPED_SOUND`, `LOOKED_AT_UNKNOWN`, `IGNORE_UNKNOWN`,
`ATTACK_UNKNOWN`, `MADE_INITIAL_RESPONSE`, `FINISHED_IGNORE_UNKNOWN`, `DONT_INVESTIGATE`,
`PLAYING_FACE_ANIM`, `FORCED_OCCLUDE`, `INTERESTING_INTO`, `ONE_HIT_KILL`.

`m_bfAINPCFlags2` (`+0x14bc`), bit 0 → 30:

`SLEEP_BOUNDING_BOX`, `FINISH_SPECIAL_NAV`, `SCHEDULE_CHANGED`, `INTERESTING_LOST`,
`TASKS_FACE_ENEMY`, `TASKS_FACE_TARGET`, `IGNORE_SQUAD_SEE_ENEMY`, `NO_UNKNOWN_ATTACK`,
`COVER_VS_MELEE_MODE`, `IGNORE_DOOR_FAILURE`, `MOVE_FACE_ENEMY`, `DISALLOW_TGT_DISCIPLINE`,
`MADE_OBLIVIOUS`, `SQUAD_NEW_ENEMY`, `DONT_FALL_TO_GROUND`, `DISABLE_BURST_FIRE`, `D_CALM`,
`D_INSANE`, `D_POSSESSED`, `D_MILDLY_CRAZY`, `D_FOLLOW`, `D_NIGHTMARE`, `D_AUTO_FEEDABLE`,
`D_DISCONNECT_SQUAD`, `D_WPN_HIDDEN`, `CHOOSE_NEW_SCHEDULE`, `NO_UNKNOWN_VISION`, `NOT_FEEDABLE`,
`NO_DIALOG_PERSISTENT`, `DISAPPEAR`, `ACTIVITY_COPY_PROP_CLEAN`.

Word one's bit order is independently confirmed by the AI debug overlay `0x1028d990`, which walks
bits 0–29 against the legend string `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO"` at `0x105d88b8`.

Every call site of the five generic flag accessors — all 16 — lies inside `StartTask`, so no hidden
writer of these bits exists outside the task vocabulary. `[VtMB]`

Consumers of the three bits the mesmerize program writes, each proved exhaustive against the field
ledger:

| Bit | Readers | Meaning |
|---|---|---|
| `D_IS_BUSY` (0) | **one**: `CBaseCombatCharacter::IsBusyWithDiscipline` (`0x1033e2b0`), whose entire body is that bit test | the bit *is* the predicate. Its 17 callers include `CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`), the dialogue gate (`0x102c21c0`) and all three `StartPlayerDialog` inputs — so a flagged NPC will not be re-targeted by a discipline, will not start dialogue, and is refused an ordinary schedule |
| `NO_DIALOG` (19) | **two**, both the same virtual slot 295: `0x102c21c0` and the `CPayphone` override `0x101aaee0` | one link of the "can the player talk to me" chain, beside `m_iDialog`, `IsUnconscious`, `m_bWillTalk`, `IsBusyWithDiscipline` and `NO_DIALOG_PERSISTENT`. The per-schedule form of dialogue suppression |
| `DONT_INVESTIGATE` (26) | **one**: `0x102b3270`, whose first line rejects on `DONT_INVESTIGATE \| IN_FLEE_SCHED` | the per-candidate interest predicate, reached only from the two per-entity sweeps `CAI_BaseNPCTroika::GatherConditions` (`0x102b27f0`) runs back to back. Every sensed entity fails the interest test |

#### `TASK_MAKE_OBLIVIOUS` and `m_iIsOblivious`

The operand is a float: the schedule compiler writes `TRUE`/`ON` → 1.0 and `FALSE`/`OFF` → 0.0
(`0x1030e65f`), and the arm compares against 0.0 exactly. All 35 shipped operands are `TRUE`; the
clear branch (`0x102a731a`) is dead in shipped data. `[VtMB] [script/data]`

The TRUE arm (`0x102a72f5`) does four things in order: sets `MADE_OBLIVIOUS`; calls `0x1026d130`,
which is `SetEnemy(NULL)` + squad disconnect (`0x1026d050`) + `++m_iIsOblivious`; fires
`OnIncapacitatedStart` (`+0x5fd4`); and completes. The FALSE arm is its exact mirror, ending in
`OnIncapacitatedEnd` (`+0x5fec`). `[VtMB]`

**`CAI_BaseNPC::m_iIsOblivious` (`+0x5bb4`) is an `int` refcount, not a boolean** — retail nests its
sources, so a body oblivious for two reasons stays oblivious when one ends. Its increment sites are
the scripted-scene starts (`CCineNPC`/`CCineAI`/`CCineAISchedule` `vfunc583`), grapple entry
(`0x1026cdc0`), fed-upon begin (`0x1026cec0`) and this task. `MADE_OBLIVIOUS` itself has **zero
readers anywhere in the binary**; it is pure bookkeeping so the schedule-change clear knows a
decrement is owed. All behaviour hangs off the counter, which has exactly four consumers:

| Consumer | Effect |
|---|---|
| `CAI_BaseNPC::PerformSensing` (`0x1026e4f0`) | `if (m_iIsOblivious < 1)` gates the whole sense pass — an oblivious body takes in **no sight, sound or scent at all** |
| `CAI_BaseNPCTroika` slot 587 (`0x1028ef20`) | reject rung in a reaction predicate |
| `CAI_BaseNPCTroika` slot 314 (`0x102bf070`) | clears `m_bAimWeaponAtTarget` and zeroes the aim pose params — the body stops aiming |
| `CStealthKillRules::FindVictim` (`0x101be1f0`) | an oblivious body is **stealth-killable from any angle**, bypassing the behind/deaf-arc test |

#### Nothing in schedule data clears these bits — the schedule *change* does

Across all 38 `TASK_CLEAR_NPC_FLAG` operands in the image, `D_IS_BUSY` and `NO_DIALOG` are never
cleared, and `DONT_INVESTIGATE` only once (`SCHED_TROIKA_SHOT_BY_UNKNOWN`). The release is
structural: `SetSchedule` → `ForceScheduleChange` (`0x102ae490`) → **virtual slot 435**, which for
all 60 classes of the Troika hierarchy is `CAI_BaseNPCTroika::OnScheduleChange` (`0x102a0940`):

```c
m_bfAINPCFlags2 |= SCHEDULE_CHANGED;
if (!(m_bfAINPCFlags & PRESERVE_PATH)) {
    ... navigator / motor / goal reset ...
    uVar1 = m_bfAINPCFlags2;
    m_bfAINPCFlags  &= 0xbbf4b97e;      // ~0xbbf4b97e == 0x440b4681
    m_bfAINPCFlags2 &= 0x77fff14f;
    if (uVar1 & MADE_OBLIVIOUS) { m_bfAINPCFlags2 = uVar1 & 0x77ffe14f; UnOblivious(this); }
}
m_bfAINPCFlags2 &= 0x3fffffff;
m_bfAINPCFlags  &= 0xd7ffffff;
```

`0x440b4681` is `D_IS_BUSY`, `IN_FLEE_SCHED`, `COWER_PATH`, `COWERING`, `ANIM_MOVEMENT`,
`FORCE_RELAXED_ANIMS`, `SLEEPING`, `NO_DIALOG`, `DONT_INVESTIGATE`, `ONE_HIT_KILL`. So an
incapacitating schedule needs no teardown tasks: **the next schedule the NPC is given unwinds it
completely and symmetrically**, and the same virtual ran when the program was installed, which is
why its tasks always write onto a cleared word. `[VtMB]`

One genuine retail defect found here: virtual slot 448 (`CAI_BaseNPCTroika::FUN_1029adb0`, the
task-failure/teardown virtual) applies `m_bfAINPCFlags2 &= 0x7fffe24f`, **clearing `MADE_OBLIVIOUS`
without decrementing `m_iIsOblivious`**. An NPC that takes that path keeps a positive refcount with
the bookkeeping bit gone, so no later `SetSchedule` will decrement it and the body stays sense-blind
and stealth-killable. No compensating decrement was found. `[VtMB]`

### Interrupt conditions

Schedules declare which new conditions are allowed to abort their current task program. The most
common compiled interrupt conditions are:

| Condition | Schedules |
|---|---:|
| `NEW_ENEMY` | 332 |
| `HEAVY_DAMAGE` | 279 |
| `LIGHT_DAMAGE` | 224 |
| `ENEMY_DEAD` | 182 |
| `SEE_FEAR` | 169 |
| `SEE_ENEMY` | 166 |
| `CAN_MELEE_ATTACK1` | 127 |
| `LOST_ENEMY` | 119 |
| `SQUAD_SEE_ENEMY` | 93 |
| `CAN_MELEE_ATTACK2` | 73 |
| `DETECTED_ATTACK` | 72 |
| `GIVE_WAY` | 67 |
| `CAN_RANGE_ATTACK1` / `CAN_RANGE_ATTACK2` | 65 each |
| `INVESTIGATE_SIGHT` | 65 |
| `INVESTIGATE_SOUND` | 60 |
| `IGNORE_UNKNOWN` | 60 |
| `HEAR_DANGER` | 54 |
| `ENEMY_OCCLUDED` | 36 |
| `HEAR_COMBAT` | 29 |

Forty-two schedules carry a `DELAY_INTERRUPTS` flag. The schedule—not merely the existence of a
condition—decides whether a new stimulus may pre-empt the current behavior immediately. This is
why a faithful AI cannot be implemented as a single global priority list with no current-task
context.

#### `DELAY_INTERRUPTS`, decoded

`DELAY_INTERRUPTS` is the **only** schedule flag the engine has. The token table `0x1030d7e0`
answers exactly two spellings — `NONE` → 0 and `DELAY_INTERRUPTS` → **bit 0** — and makes anything
else a load-time `Error`. The schedule-table parser `0x1030d850` OR-accumulates the `Flags` section
into `CAI_Schedule+0x18`, defaulting to 0 when a schedule declares none. `[VtMB]`

Recovered `CAI_Schedule` layout, from that parser and `CAI_BaseNPC::CacheInterruptConditions`
(`0x1026a0f0`):

| Offset | Meaning |
|---|---|
| `+0x00`–`+0x17` | **inverted** interrupt mask, 192 bits — the `!COND_*` form |
| `+0x18` | flags word; bit 0 = `DELAY_INTERRUPTS` |
| `+0x1c` | schedule id |
| `+0x20` / `+0x24` | task array (8 bytes/entry) and count (parser caps at 64) |
| `+0x28`–`+0x3f` | **normal** interrupt mask, 192 bits |
| `+0x40` | name |

The flag has exactly one tester: `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`), called only from
`MaintainSchedule` (`0x102817c0`). It is **not** consulted on its own — it is ANDed with the NPC's
`m_bDidMaintainSchedule` (`+0x5bb8`):

```c
if (!(!m_bDidMaintainSchedule && (m_pSchedule->flags & 1)))
{
    testBits = (m_Conditions & m_CustomInterruptConditions)
             | (m_InvertedInterruptConditions & ~m_Conditions);
    if (testBits) return false;          // "Break condition: > %s"
}
if (HasCondition(COND_SCHEDULE_DONE) || HasCondition(COND_TASK_FAILED)) return false;
return true;
```

`m_bDidMaintainSchedule` is written in exactly three places: false at spawn (`0x10273ad0`), **false
by every `SetSchedule`** (`0x10280e50`), and true on `MaintainSchedule`'s common exit
(`0x102821ae`). So the flag buys a schedule **one think of immunity, re-armed by every install** —
no timer, no task boundary, no deferral store. Nothing is latched: a condition suppressed on that
think is simply not consulted, and a stimulus that persists is re-gathered and fires on the next
one. `COND_TASK_FAILED` (`0x5c`) and `COND_SCHEDULE_DONE` (`0x5d`) are tested after the gate and
are never delayed. `[VtMB]`

All 42 flagged schedules are installed from **outside** the AI think, which is the case the flag
exists for, and they are one family: the Discipline effects and externally forced states —
`SCHED_TROIKA_D_MESMERIZE`, `D_DAZE`, `D_BERSERK`, `D_TRANCE`, `D_HYSTERIA`, `D_SUICIDE`,
`D_BRAINWIPE`, the eighteen `DO_*_ACTIVITY` performers, `TROIKA_MESMERIZED`, `LAUGHING`, `CALMED`,
`LOST`, `DISORIENTED`, `FLEE_AND_DIE`. Without the flag a forced state would be re-selected away on
the same think that forced it. `[VtMB]`

#### `SetSchedule` clears the condition set

`CAI_BaseNPC::SetSchedule` (`0x10280e50`) zeroes six dwords at `+0x5c5c` — the 192-bit condition
set `SetCondition` (`0x10269a20`) and `HasCondition` (`0x10269aa0`) write and read. A condition
standing at the instant a schedule is installed is **destroyed by the install**, so only a stimulus
the next pass re-observes can interrupt the new program. This is the other half of
`DELAY_INTERRUPTS`: without it the flag would be nearly redundant. The same body also calls the
schedule-change virtual (slot 435) first and clears `m_bDidMaintainSchedule`. `[VtMB]`

Two further facts about the effective mask, neither previously recorded: the interrupt column
supports an **inverted `!COND_*` form** (mask at `CAI_Schedule+0x00`, evaluated `mask & ~conditions`;
no shipped schedule uses it), and the mask an NPC actually runs against is **not** the authored one
— `CacheInterruptConditions` copies the schedule's mask onto the NPC each think and then lets
`BuildScheduleTestBits` (`CAI_BaseNPCTroika::0x102ad140`) add and remove conditions per NPC. `[VtMB]`

### Ordinary humanoid combat selection

State 2 is the concrete combat branch. `CNPC_VHuman::SelectSchedule` (`0x10384ee0`) and the
`CNPC_VHumanCombatant` override (`0x103872d0`) query the active weapon's capability bits. A weapon
with `0x18000` enters virtual `+0x970`, the melee selector at `0x10385e40`; other weapons enter
virtual `+0x974`, the ranged selector at `0x10386560`. A selector returning zero falls through to
`CAI_BaseNPCTroika::SelectSchedule`, so the weapon policy composes with damage, door, fear and base
state reactions rather than replacing them.

The combat-facing condition identities needed by those bodies are fixed by the retail registry:

| ID | Condition | ID | Condition |
|---:|---|---:|---|
| `0x0c` | `SHOULD_DODGE` | `0x0d` | `SHOULD_BLOCK` |
| `0x0e` | `SHOULD_STEPBACK` | `0x0f` | `SHOULD_KICK` |
| `0x28` | `KNOCKBACK` | `0x2f` | `WAITING_ATTACK_TIME` |
| `0x3c` | `WEAPON_THROUGH_WALL` | `0x40` | `NO_PRIMARY_AMMO` |
| `0x48` | `ENEMY_OCCLUDED` | `0x4a` | `HAVE_ENEMY_LOS` |
| `0x4c` | `LIGHT_DAMAGE` | `0x4d` | `HEAVY_DAMAGE` |
| `0x4e` | `REPEATED_DAMAGE` | `0x4f` | `CAN_RANGE_ATTACK1` |
| `0x50` | `CAN_RANGE_ATTACK2` | `0x51` | `CAN_MELEE_ATTACK1` |
| `0x52` | `CAN_MELEE_ATTACK2` | `0x59` | `ENEMY_UNREACHABLE` |
| `0x5f` | `TOO_CLOSE_TO_ATTACK` | `0x60` | `TOO_FAR_TO_ATTACK` |
| `0x63` | `WEAPON_BLOCKED_BY_FRIEND` | `0x66` | `WEAPON_SIGHT_OCCLUDED` |

`KNOCKBACK` is registered and read but never produced, so the two `SelectSchedule` branches that
test it and the three melee-idle schedules that list it as an interrupt are unreachable; the
knockback reaction forces its schedule directly instead (`docs/vtmb/combat-and-damage.md` →
"`COND_KNOCKBACK` is a dead condition").

The melee selector is ordered policy, not a random attack picker. Scripted combat-mode and weapon
switch gates run first, followed by door/class helpers. `SHOULD_DODGE` returns
`SCHED_TROIKA_MELEE_DODGE` (`0xd5`); `SHOULD_BLOCK` returns
`SCHED_TROIKA_MELEE_PREBLOCK` (`0xd6`). When kick and step-back are both requested, a binary random
choice selects `SCHED_TROIKA_MELEE_KICK` (`0xdb`) or `SCHED_TROIKA_MELEE_STEPBACK` (`0xd3`);
either condition outranks an ordinary attack. A usable
`CAN_MELEE_ATTACK1` then chooses `SCHED_TROIKA_MELEE_ATTACK1` (`0xdc`) or its no-turn variant
(`0xdd`). Remaining branches can switch to ranged (`0xe9`), take cover on unreachable enemy
(`0x17`), idle in melee (`0xc7`), circle (`0xe0`/`0xe1`), use class helpers, advance
(`0xca`/`0xcb`), or slow-advance (`0xd1`/`0xd2`) according to facing, distance, reachability and
per-NPC timers. The selector retains timer state, so repeated ticks do not independently reroll
every action.

The ranged selector first honors a pending switch to melee (`0xe3`) and weapon/timing setup. A
weapon protruding through a wall selects back-away (`0xb8`). Its class helpers then own dodge,
door, cover and chase decisions. Attack-ready branches choose the registered range-attack shoot,
range-attack, step-back and forced-range schedules (`0xec`–`0xf0`); no ammo, blocked line of fire,
occlusion, excessive distance or an obstructing friend instead route through reload/hide,
move-for-clear-shot (`0xbb`/`0xbd`), wait-for-clear-shot (`0xbc`), cover, chase (`0xb1`) or
run-away (`0xb9`) policy. Exact fire and ammunition commit remains in the weapon controller: a
schedule only establishes the task/activity that reaches its animation event.

The selected schedules preserve the multi-update action contract:

- `SCHED_TROIKA_CHASE_ENEMY` sets fail schedule `CHASE_ENEMY_FAILED`, tolerance 24, gets a path to
  the enemy, forces relaxed locomotion, runs it and waits for movement. It interrupts on a new,
  dead, unreachable, occluded or lost enemy; any newly available melee/ranged attack; too-close,
  task-failed or better-weapon conditions. Pathing cannot monopolize an attack-ready NPC.
- `SCHED_TROIKA_MELEE_ATTACK1`/`_NR` set melee-idle as failure, face, stop, then transfer to
  `SCHED_TROIKA_MELEE_ATTACK1_SWING`. The ordinary form can still abort on too-far-for-melee;
  both admit enemy death/loss, damage, being attacked, dodge and block before transfer. The swing
  schedule has exactly `TASK_ANNOUNCE_ATTACK 1 -> TASK_MELEE_ATTACK1 0` and no interrupts, so once
  that terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each tick.
- `SCHED_TROIKA_RANGE_ATTACK1` sets ignore-failure, stops, resolves a prior botch, faces, announces,
  runs `TASK_RANGE_ATTACK1`, applies a zero base plus up-to-one-second finish wait, resolves botch,
  then waits for attack time. It can be interrupted by new/dead enemy, light/heavy damage,
  occlusion, no primary ammo or too-close-to-attack.
- `SCHED_TROIKA_MELEE_PREBLOCK` stops then runs `TASK_MELEE_PREBLOCK`; melee dodge stops, runs
  `TASK_MELEE_DODGE`, stops again and runs `TASK_MELEE_DODGE_ATTACK`. Both use melee-idle as their
  failure schedule and interrupt only on lost enemy.

These selectors define the minimum ordinary fight loop: acquire one committed enemy, derive
attack/spacing/obstruction conditions, choose a schedule with current-schedule context, run its
movement and attack tasks, let the weapon commit the hit, then feed the committed damage back into
memory and conditions on the next decision pass.

## What fires when aggression begins

### Native incident chain

The following is the proven general chain. Individual classes and schedules specialize the middle
of it, and not every incident emits every output.

1. **A stimulus exists.** An actor is seen or heard, the NPC takes damage, combat noise reaches its
   senses, a player conduct threshold is crossed, an entity input changes relation, or a script
   assigns behavior/enemy state.
2. **Relationship and eligibility are evaluated.** Exact entity relation precedes class relation;
   capabilities, flags, perception parameters, and class policy decide which observations matter.
3. **Memory and conditions change.** Enemy/last-enemy, last-seen category, last-heard, damage,
   occlusion, range, and attack-capability conditions are updated.
4. **Observation outputs may fire.** The map can receive `OnFoundPlayer`, `OnFoundEnemy`,
   `OnHearCombat`, or `OnDamaged`. `OnFoundPlayer` and `OnFoundEnemy` are distinct surfaces and
   maps use both.
5. **State is reconsidered.** A typical escalation is idle to alert to combat, but fear, scripted
   ownership, prone state, `no_alert_state`, and class policy can produce a different route.
6. **The current schedule may be interrupted.** Interruption occurs only if the new condition is
   in that schedule's mask and is not currently delayed or suppressed.
7. **The class selects a schedule.** Possibilities include investigate, start combat, pursue,
   cover, flank, attack, flee, cower, or a specialized class response.
8. **Tasks execute over time.** The NPC faces or paths, waits for movement, equips, chooses an
   activity, attacks, remembers state, or transfers to another schedule.
9. **Animation is resolved.** Activity passes through NPC and weapon translation before the model
   chooses a weighted sequence.
10. **Consequences propagate.** Combat sounds alert other NPCs; damage, incapacitation, feeding,
    dialogue, and death outputs can drive I/O and Python; scripts may change relationship or
    schedule again.

This chain explains why four tempting shortcuts are wrong:

- `OnDamaged` is an authored output, not the entire native damage response.
- `SetRelationship D_HT` changes a relationship row; it does not directly name the next animation.
- entering combat state does not itself encode pathing, attack choice, or cover behavior.
- a Python quest callback is a consequence/controller layered over the native loop, not proof that
  Python performs sensing and combat selection.

### Losing a target

The base NPC exposes separate outputs for losing line of sight and losing the actor entirely:
`OnLostEnemyLOS`, `OnLostEnemy`, `OnLostPlayerLOS`, and `OnLostPlayer`. The native object also
retains last enemy and last-seen/heard state. Search, investigate, chase, and occlusion schedules
can therefore continue after direct visibility ends. A rebuild must not collapse "not currently
visible" into "forgotten and neutral." The player-specific debounce, including its ten failed
committed-enemy LOS checks, is in [stealth.md](stealth.md).

## Authored NPC outputs

### Native output surface

The recovered base `CAI_BaseNPC` datamap exposes 16 exact outputs:

```text
OnDamaged          OnDeath             OnHalfHealth
OnFoundEnemy       OnLostEnemyLOS      OnLostEnemy
OnFoundPlayer      OnLostPlayerLOS     OnLostPlayer
OnHearWorld        OnHearPlayer        OnHearCombat
OnGrappleBegin     OnGrappleEnd        OnFedUponBegin
OnFedUponEnd
```

Derived VtMB classes add surfaces used by maps, including dialogue and incapacitation events.

### Output census

NPC and maker rows contain 829 raw output rows. Of those, 242 have a blank exported event name and
are retained as raw evidence but excluded from the named-event behavior census. The remaining 587
named rows are:

| Event | Rows |
|---|---:|
| `OnDeath` | 152 |
| `OnFoundPlayer` | 142 |
| `OnDialogEnd` | 89 |
| `OnSpawnNPC` | 62 |
| `OnDamaged` | 47 |
| `OnHearCombat` | 21 |
| `OnDialogBegin` | 19 |
| `OnFedUponBegin` | 10 |
| `OnIncapacitatedStart` | 10 |
| `OnLostPlayer` | 6 |
| `OnNPCDied` | 6 |
| `OnIncapacitatedEnd` | 6 |
| `OnFoundEnemy` | 5 |
| `OnFedUponEnd` | 5 |
| `OnSellWeapon` | 2 |
| `OnHearPlayer` | 2 |
| `OnLastNPCDied` | 1 |
| `OnUnknownVisionPlayer` | 1 |
| `OnTransformComplete` | 1 |

The largest named-output maps are `sm_warehouse_1` (211), `la_hub_1` (103), `sm_hub_1` (62),
`sp_tutorial_1` (53), `sm_medical_1` (40), `sm_diner_1` (29), and `sm_pier_1` (19). This is not a
measure of AI complexity by itself; it measures how much authored map logic is attached to the
NPC event surface.

### Concrete aggression wiring

The authored examples show that native aggression and story scripting deliberately interleave:

- **Arthur, `sm_bailbonds_1`:** `OnDamaged` sends himself `SetRelationship` with
  `player D_FR 5`; `OnDeath` calls `kilpatrickDeath()`. The injury changes how Arthur relates to
  the player, while death advances authored state.
- **Bertram, `sm_hub_1`:** `OnDamaged` disables talking, sets `player D_HT 5`, and applies scripted
  Potence level 3. Damage simultaneously closes dialogue, establishes hostility, and turns on a
  combat power.
- **Igor, `la_hub_1`:** both `OnDamaged` and `OnHearCombat` trigger `logic_IgorHate`. His death
  updates Python/global-counter state. Either direct injury or nearby combat can enter the
  encounter's hostile path.
- **Diner:** assassins fire `go_attack` on `OnFoundPlayer`; civilians begin a cower sequence on
  `OnHearCombat`; civilian deaths make the Killer hate the player; and damage makes the Killer
  himself hate the player. Sensing, civilian reaction, and blame are separately authored.
- **Warehouse:** `OnFoundPlayer`, `OnHearCombat`, and `OnDamaged` terminate conversations, record
  stealth failure in `G.Warehouse_Spotted`, and drive solidify/unhide relays. Native detection is
  the source event; Python/I/O decides the mission consequence and encounter staging.
- **Theatre:** guards use `OnFoundEnemy`, rather than `OnFoundPlayer`, to fail the Mitnick stealth
  condition; death also calls `setMitnickFail()` and sets `G.Shubs_Botch = 1`.
- **Tutorial:** maker children connect discovery, damage, death, and feeding to tutorial failure,
  progress, UI, and relationship changes. The maker's child I/O is part of the lesson controller.

The examples prove that a faithful implementation needs both layers. Implementing only native
combat produces NPCs that fight but do not advance the authored game. Implementing only outputs
produces quest events without credible sensing, interruption, pursuit, combat, or recovery.

## Python, dialogue, and level orchestration

### Binding model

The map `worldspawn` names the level script. Python resolves entities through `Find`/`Finds` by
targetname or classname. Entity output field six can execute a Python payload, dialogue columns 4
and 5 can mutate state, `ScheduleTask` can defer an action, and shared `G` values coordinate quest
state across callbacks. The bridge exposes datamap inputs and properties through dynamic
`__getattr__`/`__setattr__`; scripts are not limited to a small, formally declared AI API.

This means Python can drive the native AI without implementing its inner loop. It can write a
relationship, start or change a schedule, begin a sequence, change perception policy, or ask an
NPC to flee. The subsequent movement, conditions, task progress, and animation remain native.

### Action-facing corpus

The broad script-API survey resolves 16,814 call sites across 676 callable names. The focused
action survey below then selects the 3,367 calls that can directly change an actor, action,
sequence, schedule, relationship, or presentation. The installed script tree contains 41 `.py`
files and 14 compiled-only `.pyc` files; the focused level-script survey parses 36 applicable
source files, so those figures describe different corpus boundaries rather than a missing-file
claim.

The focused action/animation survey finds 3,367 action-facing calls:

| Action | Calls | Principal source |
|---|---:|---|
| `SetDisposition` | 2,509 | dialogue column 4: 2,467; dialogue column 5: 34; Python: 7; scheduled: 1 |
| `SetRelationship` | 346 | 334 immediate plus 12 scheduled |
| `BeginSequence` | 70 | Python: 67; dialogue: 1; scheduled: 2 |
| `CancelSequence` | 8 | immediate script |
| `SeductiveFeed` | 54 | action/dialogue surface |
| `SetAnimation` | 20 | action/dialogue surface |
| `StartSchedule` | 2 | immediate script |
| `SetGesture` | 2 | action/dialogue surface |
| `SetModel` | 356 | action/dialogue surface |

The immediate `SetRelationship` sites comprise 123 dialogue-column-5 calls and 211 Python calls,
including two survey-tool sites. The prevalence of `SetDisposition` in dialogue is strong evidence
for its emotional presentation role; the smaller but still substantial `SetRelationship` surface
is the deliberate native combat-AI relation mutation seam.

AI-relevant immediate calls in the corpus include:

| Call | Immediate sites |
|---|---:|
| `SetRelationship` | 334 |
| `WillTalk` | 79 |
| `BeginSequence` | 68 |
| `FleeAndDie` | 19 |
| `SetupPatrolType` | 16 |
| `FollowPatrolPath` | 16 |
| `CancelSequence` | 8 |
| `SetFollowerBoss` | 7 |
| `TweakParam` | 6 |
| `ChangeSchedule` | 5 |
| `StartSchedule` | 2 |
| `SetInvestigateMode` | 1 |
| `SetInvestigateModeCombat` | 1 |

Immediate and scheduled counts are intentionally distinguished. A scan that looks only for direct
calls undercounts deferred state changes.

### Script examples

`vamputil.BefriendAnimal` demonstrates how scripts compose several independent AI knobs. It sets a
like relationship, uses `TweakParam` to eliminate hearing and vision, clears the player
investigation/criminal/supernatural thresholds, and sets normal and combat investigation modes to
zero. The dog reaction path first establishes neutral and changes to `SCHED_VDOG_SNARL`; the
friend path changes to `SCHED_VDOG_MADEFRIEND`. The relationship row, perception policy,
thresholds, and schedule are all intentionally separate writes.

Other representative script patterns include:

- `warehouse.fearThugs()` calls `FleeAndDie` across an encounter population;
- `hateSabbat()` installs targeted hostility between groups;
- `santamonica.mercurioDialog()` can make Mercurio hate the player from quest state; and
- `StartSchedule` turns Mercurio around for authored staging.

The scripts use native names such as `SCHED_VDOG_SNARL` because schedule identity is a stable
control seam. They do not encode the per-frame path following, motor updates, or model sequence
choice themselves.

## Scripted control and authority

There are three materially different ways authored content takes control of an NPC. They must not
be collapsed into one generic "play script" operation.

### `scripted_sequence` and `aiscripted_sequence`

A scripted sequence claims the NPC body for a specific animation/cinematic action, with movement
to its mark and interruption/release policy controlled by the entity's inputs and spawn flags. It
is animation/choreography ownership. Generic contracts, including `BeginSequence`, cancellation,
completion, and body release, are documented in [entity_io.md](entity_io.md).

While script ownership suppresses the ordinary condition-gathering path, the AI does not cease to
exist: state, movement ownership, completion/failure, and restoration still have to be coherent.
Cancel and map teardown must release the claim exactly once.

### `aiscripted_schedule`

The corpus contains 13 `aiscripted_schedule` entities. Unlike a scripted sequence, this entity
pushes an AI policy and goal rather than claiming the body for one exact animation.

| Map/use | Mode | Force state | Goal |
|---|---:|---:|---|
| Apartment, turn Mercurio around | 4 | 0 | named entity |
| Diner, four assassins | 3 | 3 | `!player` |
| Santa Monica hub, blueblood alley | 1 | 0 | named entity |
| Medical, four actions | 2 or 5 | 3 | mixed goals |
| Warehouse, three retreats | 2 | 2 | `!player` |

Across all rows, schedule modes 2, 3, 1, 4, and 5 occur six, four, one, one, and one times
respectively. Force-state values 3, 2, and 0 occur eight, three, and two times. Seven goals are
`!player`; six are named entities.

The spawn validator is at `0x101a9730` and the executor at `0x101a98c0`. Force state has the exact
authored-to-native mapping:

| Authored `forcestate` | Native state |
|---:|---|
| 0 | no forced state |
| 1 | idle (1) |
| 2 | alert (3) |
| 3 | combat (2) |

The non-identical numbering is load-bearing. Treating the keyvalue as the native enum would swap
combat and alert.

Recovered schedule modes are:

- modes 1 and 2 call variants of scheduled move-to-goal-entity;
- mode 3 assigns the goal entity as enemy, copies its target position, and injects native
  condition `0x54`;
- modes 4 and 5 call variants of scheduled follow-path;
- the move/follow variants use internal schedule IDs 9 or 19, while a special NPC-type branch uses
  `0x22`.

The exact gait or policy label distinguishing 1 from 2 and 4 from 5 is not yet proven. A missing
goal logs and stops. Spawn warns when neither a schedule nor forced state is supplied; spawn flag
`0x800` suppresses the route-failure warning.

### Direct schedule changes

`ChangeSchedule` and `StartSchedule` name native schedules explicitly. They are policy-level
commands: the named schedule still executes normal tasks, failures, interrupts, motor work, and
activity translation. `BeginSequence`, by contrast, establishes sequence ownership. A rebuild
needs distinct interfaces for these operations so cancellation and save/restore preserve the
correct owner.

## Reaction beyond immediate combat

### Dialogue and talk eligibility

`WillTalk` is independently scripted at 79 immediate sites. Damage can disable it, as in Bertram's
encounter, without that Boolean being the hostile relationship itself. Dialogue begin/end outputs
are also common (19 and 89 rows). A conversational NPC therefore combines at least dialogue
identity, talk eligibility, emotional disposition, current AI/script ownership, and combat
relationship.

### Fear, flight, and civilian behavior

Fear is a first-class native relation (`D_FR`), and the schedule corpus contains flee and cower
families plus interrupts such as `SEE_FEAR`. Civilian map outputs begin cower sequences on combat
sound. Scripts can call `FleeAndDie`. These are different tools:

- a fear relationship affects native target categorization;
- a flee/cower schedule decides ongoing behavior;
- a scripted cower sequence guarantees authored presentation; and
- `FleeAndDie` is a high-level scripted encounter verb with its own lifecycle consequence.

Implementing all four as "run away" would lose interruption, quest, and persistence semantics.

### Followers, patrols, and loitering

The script corpus uses `SetFollowerBoss`, `SetupPatrolType`, and `FollowPatrolPath`. The native
schedule library contains follow and patrol families, while map rows provide squad and ambient
place groups. These systems share navigation/motor infrastructure but differ in ownership:

- a patrol follows an authored route policy;
- a follower maintains a relationship/formation to a leader;
- an interesting-place user selects ambient opportunities; and
- a scripted schedule temporarily installs a goal or path.

The navigator must consequently identify its current owner and support clean transfer, failure,
and restoration rather than exposing one unqualified destination vector.

### Incapacitation, feeding, grapple, and death

The output surface distinguishes damage, half health, incapacitation start/end, grapple begin/end,
feeding begin/end, and death. These are lifecycle states with different gameplay and scripting
effects. Damage arithmetic and the health commit boundary are documented in
[combat-and-damage.md](combat-and-damage.md); AI must consume the committed outcome and select the
appropriate state/schedule/output without reimplementing the damage resolver.

The ordinary NPC damage-to-AI transaction is now recovered. `CAI_BaseNPC::OnTakeDamageAlive`
(`0x10265ed0`) first calls the shared combat-character health commit. On success it fires
`OnDamaged`; when projected Source `m_iHealth` (`+0x210`) is no greater than half
`m_iMaxHealth` (`+0x208`), it also offers `OnHalfHealth`. It records the attack position and
attacker, updates enemy memory, and asks the class's light/heavy classifiers to set
`LIGHT_DAMAGE` (`0x4c`) and `HEAVY_DAMAGE` (`0x4d`). Damage at `+0x5d94` is accumulated for a
one-second window rooted at `+0x5d98`; exceeding 15 percent of Source max health sets
`REPEATED_DAMAGE` (`0x4e`). The Troika alive override (`0x102beda0`) preserves the full incoming
damage packet at `+0x660c`, gives a surviving attacker-memory record a five-second lifetime, and
notifies the active schedule. A special NPC flag can force the death path; its authored semantic
name remains unresolved.

`OnDamaged` and `OnHalfHealth` are normal entity outputs: `FireOutput` enqueues their actions in
`CEventQueue`. They do not recursively run a map-authored `SetRelationship` or Python payload in
the middle of the damage body. Zero-delay actions are serviced in the queue pass after entity
thinks, FIFO behind the equal-time cohort already queued; the native damage memory/conditions are
therefore committed before an authored hostility consequence is observed by a later AI pass.

Damage condition is not animation. The base idle/combat selectors may choose `SMALL_FLINCH`
(`0x14`): remember the flinched state, stop, and execute `TASK_SMALL_FLINCH`. The alert selector
chooses `TAKE_COVER_FROM_ORIGIN` (`0x19`) when the attack origin lies within its recovered facing
test, otherwise `ALERT_SMALL_FLINCH` (`0x07`) when a usable flinch sequence exists, or merely
`ALERT_FACE`. Separately, `CBaseCombatCharacter::DamageFlinch` (`0x103229d0`) randomly selects the
head or torso hit activity, derives `hit_yaw` from the incoming vector relative to actor yaw, adds
a random `[-30,+30]` degrees, and starts the gesture/layer with 0.1/0.3 fade values. Its complete
firearm caller chain remains open.

These reactions must not be collapsed:

- melee **block stagger** is the opposed-roll heavy-block band and selects `ACT_BLOCK_HEAVY`;
- a normal **hit/knockback** is the stronger unblocked melee outcome and can add impulse;
- **light/heavy/repeated damage** are AI conditions that may interrupt and select a schedule;
- **generic damage flinch** is a head/torso gesture/activity with hit direction; and
- **incapacitation** is a derived-class lifecycle with its own outputs, not the half-health test or
  a synonym for dying.

After the alive commit and NPC response, `CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`)
compares RPG `Health` damage against `Max_Health`. `Health < Max_Health` survives; at or above the
pool it calls `Event_Killed`. The shared death body (`0x1032b9b0`) enters life state 1 (dying),
cleans weapon/effect/ownership state, constructs the ragdoll-force envelope, and notifies killer
and game rules. `CAI_BaseNPC::Event_Killed` (`0x10265ad0`) then:

1. refuses the kill outright while the current schedule (`+0x5c38`) is `GetScheduleOfType(0x3a)`
   — schedule type **`NPC_FREEZE`**, not a death schedule. A frozen NPC does not die;
2. defers death while a **started** scripted sequence owns it, otherwise cancels script ownership;
3. cleans navigation, marks current/ideal NPC state 7 (dead), vacates strategy and squad state;
4. fires `OnDeath` once through its `+0x5bd4` guard and notifies the AI death path; and
5. emits the carcass sound or starts the corpse fade, then leaves the death schedule to the
   dead-state selector.

The Troika override (`0x102bf340`) composes game-specific cleanup around that base transaction:
release the hint/claims and feed-related ownership, notify owning/maker systems, run Python
`MarkAsDead('<targetname>')`, and update special partner/owner memory. The player uses a different
outer path (`0x10163af0`): it ends conversations/controllers/grapples and active weapon state,
notifies game rules, enters the player death action/screen (`vdata/Signs/death.txt`), and then uses
the same combat-character death cleanup. NPC and player can therefore kill each other through one
health threshold while retaining different AI, I/O and presentation consequences.

#### The dead-state schedule

`CAI_BaseNPC::SelectSchedule` (`0x1028a380`) is the only producer of either death schedule, and it
decides on the **ragdoll's availability**, not on the damage:

```
case NPC_STATE_DEAD (7):
    BecomeClientRagdoll(vec3_origin, forceBone = -1, 0)
        ? SCHED_DIE_RAGDOLL (0x2c)
        : SCHED_DIE         (0x2b)
```

`BecomeClientRagdoll` returns false when the model carries no ragdoll collide, so a model without
one animates its death instead.

| Schedule | Type id | Tasks | Interrupts |
|---|---|---|---|
| `DIE` | `0x2b` | `TASK_STOP_MOVING 0`, `TASK_SOUND_DIE 0`, `TASK_DIE 0` | none |
| `SCHED_DIE_RAGDOLL` | `0x2c` | `TASK_STOP_MOVING 0`, `TASK_SOUND_DIE 0` | none |

Every argument is zero; neither schedule carries a non-zero task argument, and neither declares an
interrupt condition. Task ids are `TASK_STOP_MOVING` `0x69`, `TASK_SOUND_DIE` `0x49`, `TASK_DIE`
`0x5f`; the death family also registers `TASK_DIE_IF_PLAYER_CANT_SEE` `0xdc`, `TASK_DIE_IMMEDIATE`
`0xdf`, `TASK_DIE_GIB` `0xe9`, `TASK_DIE_EXPLODE_GIB` `0xea` and `TASK_DIE_DUE_TO_PLAYER` `0xeb`.
Named Troika and per-NPC death schedules — `SCHED_TROIKA_D_VISION_OF_DEATH`,
`SCHED_TROIKA_D_SUICIDE`, `SCHED_TROIKA_DO_BLOODBOIL_DEATH_ACTIVITY`,
`SCHED_TROIKA_DO_INTERESTING_PLACE_DEATH`, `SCHED_TROIKA_FLEE_AND_DIE`, `SCHED_VGARGOYLE_DEATH`,
`SCHED_VANDREIBLOOD_DEATH`, the five `SCHED_VMING_XIAO_*_DIE` and `SCHED_VZOMBIE_ANIMATED_DEATH` —
are reached by their own selectors, not by the dead-state branch.

#### Death deferred under a scripted sequence

The deferral test in `CAI_BaseNPC::Event_Killed` reads the owning sequence, not an interruptibility
verb:

```
if (m_NPCState == NPC_STATE_SCRIPT && m_hCine != NULL) {
    if (m_hCine->m_sequenceStarted            // CCineNPC +0x5f91
        && (m_hCine->m_spawnflags & 0x2080) != 0x80)
    {
        copy the 0x4c-byte damage packet to CAI_BaseNPC +0x1a48; return;
    }
    CancelScript(m_hCine);                    // 0x101a8c30
    ...re-enable physics motion...
}
```

**Deferral is the default.** Once a sequence has started, the only combination that lets the kill
proceed immediately is spawnflag bit `0x80` set with bit `0x2000` clear. Bit `0x80` is Source's
`SF_SCRIPT_DONT_TELEPORT_AT_END`; bit `0x2000` is Troika-added, and its only two consumers are this
test and `CineCleanup`'s branch that snaps the entity onto its `Bip01` bone origin and angles. The
authored name of `0x2000` is not present in the image.

**The stashed packet is a dead store.** `CAI_BaseNPC +0x1a48 … +0x1a92` is written only by this
branch, appears in no datamap, and is read nowhere in `vampire.dll` or `client.dll`. The deferred
death resumes instead through `CineCleanup` (`0x1027d170`), whose tail reads
`if (m_iHealth < 1) { SetIdealState(NPC_STATE_DEAD); SetCondition(LIGHT_DAMAGE 0x4c); }`. A kill
that lands mid-sequence is therefore re-derived from the health counter when the script releases
the actor, and every field of the original damage packet — attacker, force, damage type — is lost.

`CineCleanup`'s other branch handles an NPC that did reach life state 1 (dying) inside the script:
health is forced to 0, `FSOLID_NOT_SOLID` is added, state becomes 7 with life state 2 (dead), the
hull collapses to `mins.z + 2.0`, and the corpse fades unless the sequence sets
`SF_SCRIPT_LEAVECORPSE` (`0x8`), in which case use/think/touch are merely cleared.

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
* **2 (`NPC_STATE_ALERT`), 3 (`NPC_STATE_COMBAT`), 11** — `GetActiveWeapon()->Unhide()` (vtable
  `+0x10c`, `CBaseEntity::Unhide` `0x1009d380`), then chain;
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
