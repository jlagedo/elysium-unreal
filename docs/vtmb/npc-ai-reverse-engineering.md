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

`CAI_Squad` is held at `+0x5da4`, with `m_iSquadDisconnected` at `+0x5bb0`, leave at `0x10316700`
and rejoin at `0x10009601`, alongside `D_DISCONNECT_SQUAD`, the `SQUAD_SEE_ENEMY`/`SQUAD_NEW_ENEMY`
producers and `IGNORE_SQUAD_SEE_ENEMY`. The `squadslot` namespace resolves through a table at
`0x10920484`.

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

A separate generic flag applier pair, `0x101de6e1`/`0x101def10`, reads its mask from
`[obj+0xa8]`; it has zero recovered callers.

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
| `DONT_INVESTIGATE` (26) | **one**: `0x102b3270`, whose first line rejects on `DONT_INVESTIGATE \| IN_FLEE_SCHED` | the per-candidate interest predicate ("The interest predicate" below), asked by the see-unknown sweep, the sound sweep and the vision producer before they raise `COND_INVESTIGATE_SIGHT`/`_SOUND`. It does NOT gate `SEE_HATE`/`SEE_FEAR`, which the sense pass raises and `m_iIsOblivious` gates |

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

A retail refcount leak, **bounded**: virtual slot 448 is **`CAI_BaseNPC::TaskFail(const char*)`**
(`0x10273fc0` — the `"TaskFail -> %s"` DevMsg and `SetCondition(COND_TASK_FAILED)`; Troika override
`0x1029adb0`). The override applies `m_bfAINPCFlags2 &= 0x7fffe24f` (listing `0x1029aeb2`),
clearing `MADE_OBLIVIOUS` without decrementing `m_iIsOblivious`, where its sibling
`OnScheduleChange` uses `0x77fff14f` and decrements on the kept bit. Every writer of `+0x5bb4` is
the inc/dec pair; the field is `FTYPEDESC_SAVE` (`0x105c9cf0`), so a leak survives a save. Verified
by two independent adversarial recoveries. **Reachability is one corner:** of every task following
`TASK_MAKE_OBLIVIOUS` in the 35 shipped schedules, only `TASK_STOP_MOVING` can fail in practice —
`FAIL_STUCK_ONTOP` at `0x10288963`, requiring an active nav goal, `NAV_JUMP`, not on ground and
near-zero velocity — i.e. an NPC wedged mid-air at the moment a discipline forces the schedule
(`CAI_Navigator::OnNavFailed` `0x102eeae0` is a second source). `SCHED_TROIKA_MESMERIZED` carries
no `TASK_STOP_MOVING` and cannot leak. `TASK_SET_ACTIVITY` (`0x102a1c0f`) has no fail arm. A
leaked NPC looks normal (TaskFail's flags1 mask clears `D_IS_BUSY`/`NO_DIALOG`/`DONT_INVESTIGATE`)
but never senses again and is stealth-killable face-on — a silent failure, consistent with the
shipped game. `[VtMB] [script/data]`

#### `TaskFail` and stopped special navigation, walked (2026-09-08)

Slot 448 is `CAI_BaseNPCTroika::TaskFail` `0x1029adb0`, chaining to the base
`0x10273fc0`. The integer reasons occupy the first 42 entries of the pointer-shaped argument:
`FUN_10316fa0` resolves values `< 0x2a` through the pointer table at `0x106152b0`; larger values
are already text pointers. A raw PE read confirms all 42 strings, including the literal holes
`FAIL_CODE_10`, `FAIL_CODE_20`, `FAIL_CODE_30`, and `FAIL_CODE_40`. Selected identities: `0x05`
"Schedule not found", `0x06` "Don't have an enemy", `0x0c` "Don't have a route", `0x17`
"No player", `0x1c` "Stuck on top of something", `0x29` "NPC had no follower boss".

The override's complete transaction, before the base failure condition:

1. `0x102b53d0` releases the interesting-place visit. Keep `PRESERVE_PATH` only when the
   navigator currently reports `NAV_CLIMB (3)` or `NAV_JUMP (1)`; this step never sets the bit.
2. Motor `+0x1c := 180.0` through `0x102e0a60`; desired move yaw zero; all four next-think
   stamps equal current time. Goal tolerance, both squared interrupt distances and interrupt time
   zero; clear the move-target handle.
3. When the kick-prop handle resolves, write its `m_bNpcKickable (+0x788) := false`, then
   invalidate the handle. This consumes an authored kickable prop; it is not a balanced claim.
   An already invalid handle takes no branch.
4. Clear memory bit `0x2000`; apply `flags2 &= 0x7fffe24f`, `memory &= 0x0fffffff`,
   `flags1 &= 0xa3f40178`. `MADE_OBLIVIOUS` is lost without calling `UnOblivious`: neither
   obliviousness nor squad-disconnect refcount is decremented.
5. If the old flags2 word carried `SLEEP_BOUNDING_BOX`, call slot 15, **SetAttackExtents**
   (`0x1009af40`), with `m_vecSavedAttackExtents`; set that saved vector to `(-1,-1,-1)` and
   clear the sleep bit. `SetAttackExtents` updates the attack partition through collision's
   `0x100dc220` and entity `+0x50..58`; it does not set the movement hull. The vector is an
   **additive margin**, not an absolute half-size: engine `CSpatialPartition::vfunc0`
   `0x20040fc0` stores it at each partition record's `+0x28..30`; attack-aware box/ray
   enumeration (`0x200426e0`, `0x20042b70`) tests `[collisionMins-margin, collisionMaxs+margin]`.
   Zero components are valid. `SetAbsoluteAttackExtents` `0x1009b060` explicitly subtracts
   the collision half-size before calling this setter.
6. Clear `m_fSavePositionWalk`; `ClearHintNode(5.0)` (`0x10295ab0`) only acts when a hint exists.
   The hint's owner test (`0x102d1450`) gates its unlock and `nextUse := now+5`
   (`0x102d1420`). The NPC then loses its hint pointer, failed-cover-LOS count and
   `AT_COVER_HINT`, and resets saved attack extents to `(-1,-1,-1)`.
7. Clear motor `+0x28`, NPC `+0x6300`, `+0x659c`, and `m_bPatrolPathUseHint`. The base then
   clears `m_bShouldMove`, writes the supplied reason to `+0x5c50`, and sets
   `COND_TASK_FAILED (0x5c)`. `MaintainSchedule` zeros the reason when the next task starts.

**`TASK_STOP_MOVING` is two distinct arms.** Base StartTask's two-table dispatch maps id `0x69`
to `0x10282d71` (`0x10287138[id-1]`, then `0x10286f8c`). No active goal means clear
`m_bShouldMove` and complete. An active goal calls navigator ClearGoal (`0x102ee270`), resets
the `move_yaw` pose parameter if present, and remains running; ClearGoal does not change the
navigator's type. MaintainSchedule can invoke RunTask immediately in that same iteration.

RunTask's arm at `0x102888d4` reads navigation type independently of the cleared goal. Jump on
ground becomes Ground. Jump in the air with speed `> 0.01` Source units/s keeps running; at
`<= 0.01`, it **sets Ground before TaskFail(0x1c)** (`0x10288940..63`). Climb keeps running.
The remaining path selects the arrival activity, clears `m_bShouldMove`, and calls
TaskComplete(false), which cannot overwrite an already set TASK_FAILED condition. Therefore this
particular stuck-jump failure clears `PRESERVE_PATH`; a navigator-delivered TaskFail while its
type is still Jump/Climb retains the bit. The active-goal requirement is at StartTask admission,
not at each RunTask probe.

`NextScheduledTask` `0x10280f40` raises `COND_SCHEDULE_DONE (0x5d)` when incrementing beyond the
last task. `MaintainSchedule`'s ten-iteration bound exits through `0x102821ae`, retaining task
position and setting `m_bDidMaintainSchedule`; it does not discard a program at the bound.
`SetSchedule` `0x10280e50` invokes the outgoing schedule-change virtual before replacing its
task state, so callbacks can still inspect and complete the outgoing program.

The sibling `OnScheduleChange` `0x102a0940` sets flags2 `|= 0x80000004`, then performs its
conditional movement/mask/refcount/extents cleanup under `!PRESERVE_PATH`. Only afterwards it
tests the surviving `ACTIVITY_COPY_PROP_CLEAN (0x40000000)`, interrupts discipline effects
when `DAT_10739a64` permits, removes `activity_copy_prop` rows whose owner handle at `+0x730`
resolves to this NPC (`0x1018e910`), and clears invincibility. Finally it applies unconditional
`flags2 &= 0x3fffffff`, `flags1 &= 0xd7ffffff`, and clears memory bit `0x2000`.
`UnOblivious` (`0x1026d160`) always calls ReconnectToSquad after its clamped decrement;
Reconnect (`0x1026d0c0`) decrements with a floor at zero and clears D_DISCONNECT_SQUAD.

#### The three cached downcasts

`CAI_BaseNPC`'s constructor (`0x1027c300`) writes `this` at `+0x94`; `CAI_BaseNPCTroika`'s
(`0x1028d230`) at `+0x98`; `CBaseCombatCharacter`'s (`0x10326de0`) at `+0x9c`. None is a datamap
member. So the `+0x98` that `FeedInterrupt`, `IsScheduleValid`, `MaintainSchedule` and
`CStealthKillRules::FindVictim` dereference is the entity's own `CAI_BaseNPCTroika*` — null for
anything that is not a Troika NPC, and `this` for one. `IsScheduleValid`'s two arms through it are
therefore self-writes: during `NAV_CLIMB`/`NAV_JUMP` the interrupt mask is bypassed outright and a
done/failed task sets `PRESERVE_PATH | FINISH_SPECIAL_NAV`; otherwise a set `CHOOSE_NEW_SCHEDULE`
(flags2 `0x02000000`) is consumed and the schedule invalidated — an external "reselect now" request
bit. `[VtMB]`

#### `BuildScheduleTestBits` — the per-NPC interrupt overlay, decoded

`CAI_BaseNPCTroika::BuildScheduleTestBits` (`0x102ad140`), run by `CacheInterruptConditions` every
think on top of the schedule's authored mask:

```c
CAI_BaseNPC::BuildScheduleTestBits();                       // 0x10280fb0, empty
if (!(m_bfAINPCFlags & (DONT_INVESTIGATE | IN_FLEE_SCHED))) {
    if (!IsBusyWithDiscipline() && !(m_bfAINPCFlags2 & D_POSSESSED)) {
        if (!m_pHintNode || m_pHintNode->type != 0x2774) {
            add COND_INVESTIGATE_LEVEL (0x1e), COND_CRIMINAL_FLEE_LEVEL (0x1f),
                COND_SUPERNATURAL_FLEE_LEVEL (0x21);
        }
        if (GetEnemy() == NULL) {
            if (m_bfNPCStateFlags & 0x10) add COND_HEAR_FLINCH (0x72);
            if (m_bfNPCStateFlags & 0x20) add COND_CRIMINAL_ATTACK_LEVEL (0x20),
                                              COND_SUPERNATURAL_ATTACK_LEVEL (0x22);
        }
        if (!(m_bfAINPCFlags & COWERING)) add COND_COMFORT (0x27);
    }
}
if (m_bfAINPCFlags2 & IGNORE_SQUAD_SEE_ENEMY) remove COND_SQUAD_SEE_ENEMY (0x31);
// CacheInterruptConditions itself then always adds COND_NPC_FREEZE (0x75).
```

`m_bfNPCStateFlags` is a per-state capability byte written on every state change by `0x1026e3e0`:
idle `0x31`, alert `0x39` (bits 4 and 5 set), combat `0x8f` (bits 4 and 5 clear), script `0x8`,
the two flee states `0x85`/`0x7f`. So **in idle and alert with no enemy, all four law conditions
plus `HEAR_FLINCH` are interrupts on every schedule; in combat only the two flee levels.** That is
the decoded rule behind the four law conditions the port's idle mask used to carry with a CHOSEN
mark; the overlay is now ported on the runner (`FElysiumNpc::BuildScheduleTestBits`) and the
chosen entries are gone — with one correction the decode brought, that the attack levels are gated
on "no committed enemy". It also shows the overlay is suppressed by the very flags
`SCHED_TROIKA_MESMERIZED` sets, so that program's effective mask is exactly its authored one. `[VtMB]`

Condition ordinals named on the way, from the registrar `0x102c8ce0` (global space via
`thunk_FUN_102ea130`, `CAI_BaseNPC`-local via `thunk_FUN_102beae0`): `COND_INVESTIGATE_LEVEL` 0x1e,
`COND_CRIMINAL_FLEE_LEVEL` 0x1f, `COND_CRIMINAL_ATTACK_LEVEL` 0x20, `COND_SUPERNATURAL_FLEE_LEVEL`
0x21, `COND_SUPERNATURAL_ATTACK_LEVEL` 0x22, `COND_COMFORT` 0x27, `COND_SQUAD_SEE_ENEMY` 0x31,
`COND_TASK_FAILED` 0x5c, `COND_SCHEDULE_DONE` 0x5d, `COND_HEAR_FLINCH` 0x72, `COND_NPC_FREEZE`
0x75. `[VtMB]`

#### `NO_DIALOG_PERSISTENT`'s producers

Two schedules and no code: `SCHED_TROIKA_D_AFRAID` and `SCHED_TROIKA_D_POSSESSION` set it with
`TASK_SET_NPC_FLAG`. `NPCFlag:D_IS_BUSY` is authored by 20 schedules, every one a `D_*` discipline
effect or `SWAT_INSECTS`/`MESMERIZED` — the bit is literally "busy with a discipline effect", which
is what `IsBusyWithDiscipline`'s name says. `[script/data]`

### `MaintainSchedule`, walked

`CAI_BaseNPC::MaintainSchedule` (`0x102817c0`) has three `ret` sites and **one** store of
`m_bDidMaintainSchedule` (`mov byte [esi+0x5bb8], 1` at `0x10282342`, on the common exit
`LAB_102821ae`). The two exits that skip it are harmless: the task-complete early return at
`0x10282269` is gated on the `ai_step` console mode bit (`DAT_1092053c & 2`, set only by
`0x10085830`) and unreachable in a shipping session; the `"ERROR: Missing or invalid schedule!"`
return at `0x10282336` forces `SetActivity(ACT_IDLE)` (slot 310) and is reachable only after the
loop's own `GetNewSchedule`/`SetSchedule` has already re-armed the flag or left no schedule at all.
So the one-think `DELAY_INTERRUPTS` window never silently extends. `[VtMB]`

The loop bound at `0x1028190e` is **10** when the `bool` argument is 0 and **1** when it is set.
The loop continues only while tasks keep completing (`cmp [esi+0x5c44], 4` at `0x1028212e`) — it
is a cap on task completions per think, not a spin guard — and also exits on `TaskIsRunning()`
false, `COND_TASK_FAILED`, or an RDTSC time budget. `IsScheduleValid` is called **inside** the loop
at `0x102819d5`, once per iteration, its argument recomputed as `!m_bDidMaintainSchedule` each time;
its `false`, or `m_NPCState != m_IdealNPCState`, drops into the reselect block. `[VtMB]`

**A reduced-think mode, previously unrecorded.** `CAI_BaseNPC::RunAI(bool)` (`0x1026f110`)
forwards that argument: when set it **skips `GatherConditions` entirely** (slot 433), passes the
bound of 1, and skips the end-of-pass clear of `COND_LIGHT_DAMAGE`, `COND_HEAVY_DAMAGE` and
`COND_WAS_BUMPED`. `CAI_BaseNPC::NPCThink` always passes 0; `CAI_BaseNPCTroika::NPCThink`
(`0x10292de0`) passes `!(m_flNextAIThink − curtime < frametime)` — so a Troika NPC whose
`m_flNextAIThink` (`+0x6250`) is not yet due still thinks, but gathers nothing and maintains one
task. Sibling cadences: `m_flNextUpdateThink` `+0x6244`, `m_flNextNormalThink` `+0x6248`. The port
does not model this mode. `[VtMB]`

### Two Troika virtuals, identified

**Slot 314 (`+0x4e8`) is `UpdatePoseParameters(float flInterval)`** — the authored name is a
ScopeTrace literal at `0x1053ede0` on the base `CBaseCombatCharacter::UpdatePoseParameters`
(`0x10054060`), and `CAI_BaseNPCTroika::0x102bf070` is its override, tail-calling the base. Target =
`m_hShootTargetOverride`, else slot 167; no target **or `m_iIsOblivious > 0`** → the null arm
(`m_bAimWeaponAtTarget = 0`, yaw/pitch zeroed); otherwise the aim offset to the target's eye
position, own angles subtracted, clamped to ±45°, then approached into the pose parameters with the
shaky-hands offset. `[VtMB]`

**Slot 587 (`+0x92c`) has no Source ancestor**: `vftable_CAI_BaseNPC` ends at slot 582, and the
`CAI_BaseNPCTroika` and `CAI_BaseHumanoid` tables extend past it independently, so their slot-587
entries are different virtuals sharing an offset. The Troika body (`0x1028ef20`) has one consumer:
`FUN_10181be0`, a `CBasePlayer` method armed only for the `Player_Nosferatu` template, which
enumerates NPCs in a 512-unit sphere and records the nearest one for which this returns true at
`player+0x1ED0` — networked for the client's Masquerade warning. The body's clauses are the
predicate half of the player-law transaction `0x1028efc0`: reject Kindred, `m_iDialog != 0`,
oblivious, `m_bfNPCFrenziedFlags & 0x10`, `IsBusyWithDiscipline`, then true only when a supernatural
threshold is below 3. Project name `CanWitnessSupernatural()`; the authored spelling is
unrecoverable from this image. TVs, animals, bosses, placeholders and makers all override it to
`return 0`. `[VtMB]`

### `m_hFollowerBoss` — the follower controller

`CAI_BaseNPCTroika+0x647c` is the resolved handle for the `follower_boss` keyfield
(`m_sFollowerBoss`, `+0x6478`; `follower_type` is `+0x6480`). Exactly two writers: the constructor
(`0xffffffff`) and **`SetFollowerBoss(const char*)`** (`0x102c44e0`), which resolves the name through
slot 559 (the `!player`/`!self`/`!enemy`/… resolver), refuses `this`, **`Error`s on a squad member**
("Followers can not be in squads. This functionality not implemented."), and on success sets
`m_bfNPCFrenziedFlags |= 0x3008`. Its arming paths: `Activate` and `OnRestore` from the keyfield,
the `SetFollowerBoss` entity input (`0x102c3350`), `CNPC_VPedestrian::Activate` clearing it, and the
discipline-effect applier's mind-control path (`0x101dfc20` → `0x102c51a0`). No Python setter. `[VtMB]`

**The controller** is virtual slot 607 (`+0x97c`, `0x102b93c0`, Troika-only, 64 tables), with one
dispatch site: `SelectSchedule` (`0x102af660`) case 1, where a non-zero return pre-empts patrol,
interesting places, the alert lookaround and `m_bReturnToInitialPos`. Its body is the boss distance
against three radii `FUN_102c4680` fills from Rules.txt `Npc_Follower_Info` per `follower_type`,
clamped `walkTo ≥ backAway + overlap`, `runTo ≥ walkTo + overlap`:

| Boss distance² | Schedule |
|---|---|
| handle dead | none (fall through) |
| `< FollowerDistanceBackAway²` (`+0x6484`) | `0x10c` `FOLLOWER_BACKAWAY`; writes `m_vSavePosition` = boss origin |
| `> FollowerDistanceRunTo²` (`+0x648c`) | `0x113` `FOLLOWER_FOLLOW_RUN`; `SetTarget(boss)` |
| `> FollowerDistanceWalkTo²` (`+0x6488`) | `0x112` `FOLLOWER_FOLLOW_WALK`; `SetTarget(boss)` |
| otherwise | `0x115` `FOLLOWER_WAIT`; `SetTarget(boss)` |

The `_F` in `COND_INSIDE/OUTSIDE_INTERRUPT_DIST_F` is Follow: those programs re-evaluate the band
continuously. The tasks `0x86`–`0x88` `TASK_FIND_FOLLOWER_BACKAWAY_{SIMPLE,NODE,ASTAR}` read the
handle and fail with `"NPC had no follower boss"` when it is dead. The other readers: `IRelationType`
(D_LI toward the boss, inherit the boss's relations, D_HT toward anyone whose boss I hate),
`GetFollowerBoss()` (slot 293), `CNPC_VHuman::SelectIdealState` (`0x103851e0`) (a follower whose
enemy is gone goes to alert, a non-follower to the hunt state `0xb` when the ConVar at
`DAT_1092447c` is on), the interest predicate (never investigate the boss), and
`CBasePlayer::UpdateClientActionState` (a follower reads as an ally on the target HUD). `[VtMB]`

A second, unrecovered ConVar sits beside it at `DAT_10924f74`.

### The three `GatherConditions` sweeps and the interest predicate

`CAI_BaseNPCTroika::GatherConditions` (`0x102b27f0`) makes **three** consecutive calls, and neither
of the two previously named iterates a list:

- **`0x102b15c0` — sight**, but the *see-unknown* channel only: one entity, `m_hBestSeeUnknown`
  (`+0x6088`), player-only by construction (requires the cached `CBasePlayer*` at `+0xa8`). Not
  seeing → the 1.5 s grace timer (`+0x6084`), then `COND_LOST_UNKNOWN` and `m_vecLastSeeUnknownPos`
  (`+0x6090`). Seeing → a concealment test, a one-shot `MADE_INITIAL_RESPONSE` roll setting
  `ATTACK_UNKNOWN` or `IGNORE_UNKNOWN` off `m_iSeeUnknownRepeatSightings` (`+0x60a4`) and
  `m_bFullInvestigate` (`+0x6340`), then a 2-D closing-speed classification against `20.0f` into
  `COND_UNKNOWN_ADVANCING/HOLDING/RETREATING` and `COND_INVESTIGATE_SIGHT` (0x26). Retail quirk:
  `HOLDING` needs exact float equality and is effectively dead.
- **`0x102b1a20` — the comfort list**, idle only, rate-limited 0.2–0.4 s: walks the global
  `AddToComfortList`/`RemoveFromComfortList` array (`0x10323630`/`0x10323770`), nearest within 1024
  units, at most 3 comforters per target, sets `COND_COMFORT` (0x27).
- **`0x102b1cd0` — sound**: the six fixed `CSound` records (`m_LastSoundWorld` `+0x61e4`,
  `PhysicsDanger` `+0x6134`, `Danger` `+0x6108`, `Player` `+0x61b8`, `BulletImpact` `+0x618c`,
  `Combat` `+0x6160`), gated on `m_flNextInvestigateSoundTime` (`+0x623c`). Each arm:
  `HasCondition(HEAR_X) && (schedule already interrupts on HEAR_X || ShouldInvestigate(owner, b))`
  → `COND_INVESTIGATE_SOUND` (0x25); `HEAR_DANGER` skips the predicate. Last wins: combat > bullet
  impact > player > danger > physics danger > world. Then `COND_HEAR_FLANK_SOUND` (0x33) and a
  `COND_SEE_SOUND_SOURCE` (0x2d) tail. Walked whole below.

#### The sound sweep `0x102b1cd0`, walked

Read off the decompilation while porting story 10a; it corrects the summary above in three places.

**Entry.** `ClearCondition(INVESTIGATE_SOUND 0x25)` and `ClearCondition(HEAR_FLANK_SOUND 0x33)`,
unconditionally, before the gate. `GatherConditions` itself clears neither, so the sweep owns them.
`SEE_SOUND_SOURCE` is *not* cleared here — its tail has two arms that leave it standing.

**Gate.** The whole six-arm body runs only when `m_flNextInvestigateSoundTime (+0x623c) <= curtime`.
The gate is never re-armed inside this function; all six writers are in `SelectSchedule` and the
sound selectors.

**The arms.** In evaluation order, each claiming the winner, so the last one to pass wins:

| # | Condition | Record | `bCombatMode` |
|---|---|---|---:|
| 1 | `HEAR_WORLD` 0x6e | `+0x61e4` | 0 |
| 2 | `HEAR_PHYSICS_DANGER` 0x71 | `+0x6134` | 0 |
| 3 | `HEAR_DANGER` 0x6a | `+0x6108` | predicate skipped |
| 4 | `HEAR_PLAYER` 0x6f | `+0x61b8` | 0 |
| 5 | `HEAR_BULLET_IMPACT` 0x70 | `+0x618c` | 1 |
| 6 | `HEAR_COMBAT` 0x6d | `+0x6160` | 1 |

**Correction 1 — there is no Flinch arm.** The `Flinch` record `+0x6210` is a real seventh record
and `CommitBestSound` ranks it third, but `0x102b1cd0` never touches it: `HEAR_FLINCH` cannot
produce `INVESTIGATE_SOUND`. It reaches a decision only through `FUN_102b9060`'s own
`HasInterruptCondition(HEAR_FLINCH)`. The "six records" and "seven records" statements elsewhere in
this document are each half right; this is the whole of it.

**Correction 2 — the mask tester is `0x10269c70`, not `HasInterruptCondition`.** Three distinct
testers exist and the sweep uses the third:

| Address | Needs a schedule | Reads `+0x5c5c` | Reads the mask |
|---|:-:|:-:|---|
| `0x10269aa0` `HasCondition` | no | yes | — |
| `0x10269d30` `HasInterruptCondition` | yes | yes | `+0x5c74` |
| `0x10269c70` (the sweep's) | yes | **no** | `+0x5c74` **OR** `+0x5c8c` |

Inside the six arms the difference is invisible — the arm already `&&`s with `HasCondition`. For the
`0x33` and `0x2d` gates it is the whole rule: they are pure "does the running program list this"
tests, satisfied with the condition unset, and they accept the second mask word too.

**`HEAR_FLANK_SOUND` 0x33.** All of: the mask lists `0x33`; there is a winner; `GetEnemy()` is
non-null and equals the winner's owner (a resolved-pointer comparison, not a handle one); and
`dot(soundAt - GetAbsOrigin(), m_vecForward (+0x6290)) < 0.0f` (`_DAT_104454c4` = `0.0f`, confirmed
by its use as the `0.0f <` threshold on the interrupt distances in `0x102b27f0`). Measured from
`GetAbsOrigin` (slot 217), not the eye. No consumer is recovered.

`soundAt` is **not** simply the record's stored origin. It is `FUN_101b99d0(record)`:

```c
if ((record[1] == 0x10 || record[1] == 0x400) && record[0] /*owner*/ resolves)
    return owner->GetAbsOrigin();   // vfunc 0x364
return record + 0x20;               // the stored origin
```

`record+0x04` is the raw CSound type word, so for `BULLET_IMPACT` (`0x10`) and `PHYSICS_DANGER`
(`0x400`) with a live owner the test measures to the owner's CURRENT position, and only for the
other four arms to where the sound was made. Since the gate has already established the owner is
the committed enemy, those two arms ask "is my enemy behind me now" rather than "is the bullet hole
behind me".

**The `SEE_SOUND_SOURCE` 0x2d tail.** Outside the gate. Gated on the mask listing `0x2d`; if it does
not, fall straight through to `ClearCondition(0x2d)`. The subject is `m_hBestSoundSource (+0x5b78)`
— the source the last `CommitBestSound` chose, not this sweep's winner. Comparisons are between
*resolved pointers* throughout.

1. `FUN_102b8cd0(this, source)`: true only when `source != NULL` **and**
   `IRelationType(source)` is neither `D_HT` (1) nor `D_FR` (2). False → `return`, leaving `0x2d`
   untouched. The polarity reads backwards and is worth stating: the tail runs only for a source
   the NPC neither hates nor fears.
2. A first-match chain; the match demands its own sight condition, and a match without it (or no
   match) falls to `ClearCondition(0x2d)`:

   | `source ==` | requires | status |
   |---|---|---|
   | `m_hClosestPlayer` `+0x628c` | `SEE_PLAYER` 0x5a | live |
   | `GetEnemy()` | `SEE_ENEMY` 0x46 | live, but only for a `D_LI`/`D_NU` enemy after step 1 |
   | `m_hLastSeenHateEnt` `+0x5b68` | `SEE_HATE` 0x43 | live; needs the relation to have left `D_HT` |
   | `m_hLastSeenFearEnt` `+0x5b6c` | `SEE_FEAR` 0x44 | live; needs the relation to have left `D_FR` |
   | `m_hLastSeenDislikeEnt` `+0x5b70` | `SEE_DISLIKE` 0x45 | live; same `D_HT` caveat |
   | `m_hLastSeenNemesisEnt` `+0x5b74` | `SEE_NEMESIS` 0x5b | live; same `D_HT` caveat |

3. Otherwise the stranger arm. `curtime < +0x6418` → `return` (sticky). Else
   `FInViewCone(source)` `&& FVisible(source, 0x2804091)` (slot 201, Troika `0x102b4630`)
   — both dispatched virtually, so on a Troika NPC the cone test is the slot-363 override
   `0x102b4540` (which adds two dev-global arms and an unconditional accept when `GetTarget()` is
   `m_hClosestPlayer` with the target's `+0x6279` byte set) and NOT the base
   `CBaseCombatCharacter::FInViewCone` `0x10326750` → `SetCondition(0x2d)`, else `ClearCondition(0x2d)`; then
   `+0x6418 = curtime + 0.5f` (`_DAT_104454d0`).

**Correction 3 — `+0x6418`.** It is the stranger arm's own rate limit, `0.5 s`; this sweep is its
only reader and only writer. It appears nowhere else in this document.

**The four `m_hLastSeen*Ent` handles are live.** `CAI_BaseNPC::OnLooked` (`0x1026a2c0`, the base of
Troika slot 469) writes one of them per assessed sighting: the `D_HT` case splits on
`IRelationPriority` into `m_hLastSeenDislikeEnt` (`< 0`, condition 0x45),
`m_hLastSeenHateEnt` (`< 0xb`, 0x43) and `m_hLastSeenNemesisEnt` (else, 0x5b); the `D_FR` case
writes `m_hLastSeenFearEnt` (0x44). Both cases sit behind `m_bfAINPCFlags2 & 0x10000` (`D_CALM`):
the `D_HT` arm jumps to the `case 2` label when it is set, and `case 2` re-tests the same flag and
does nothing — so a sighting under `D_CALM` writes no handle AND raises no condition. `FUN_1027c300`
resets all four to `0xffffffff`.

A method note, because this was got wrong once during the 10a port and corrected here: a
`vtmb_grep` for the decompiler's untyped dword rendering (`param_1[0x16da]`) finds only the reset
and this sweep, and `vtmb_readers` scoped to `CAI_BaseNPCTroika` reports zero accesses — the writer
renders as a NAMED FIELD on the base class, `CAI_BaseNPC::m_hLastSeenHateEnt`. Query the ledger at
the class that declares the field, not the one that reads it, before calling storage dead.

The practical reachability of those three `D_HT`-derived rungs is still narrow: step 1 rejects a
source the NPC currently hates or fears, so the rung fires only when the relation has changed since
the sighting that wrote the handle. Narrow is not dead.

Constants, read out of retail `vampire.dll`'s `.rdata` at file offsets `0x4454c4` / `0x4454d0` (image base `0x10000000`): **`_DAT_104454c4 = 0.0f`** (`00 00 00 00`) and **`_DAT_104454d0 = 0.5f`** (`00 00 00 3f`). Both are shared pool constants with hundreds of readers, so neither can be pinned from the corpus alone — the corpus exposes referrers, not `.rdata` values. `m_vecForward = +0x6290` (cached by
`NPCThink` and `GatherConditions`). The `(**(*DAT_10924a6c + 4))()` call preceding every
`SetCondition` is an AI trace hook with its result discarded; it has no gameplay effect. `[VtMB]`

**The interest predicate `0x102b3270`** (`ShouldInvestigate(candidate, bCombatMode)`), in order:
`m_bfAINPCFlags & (DONT_INVESTIGATE | IN_FLEE_SCHED)` → false; `stay_entrenched` → false; null →
false; candidate is `m_hFollowerBoss` → false; candidate is the committed enemy → **true**; then a
switch on `investigate_mode` (`+0x6338`) or, when `bCombatMode`, `investigate_mode_combat`
(`+0x633c`) — the sight sweep, the world/physics-danger/player sound arms and the vision producer
pass 0, the bullet-impact and combat sound arms pass 1. The modes are a product: 0 never; 1 players
I hate; 2 players not neutral; 3 any player; 4 anything I hate; 5 anything not neutral; 6 anything;
else `DevWarning("Hey FOO!!!  I don't recognize your investigate mode!")` and false. **The shipped
default, 4 on 378 of 426 rows, is "anything I hate" — not "the player".** `bCombatMode` also
unlocks a third-party-brawl proximity override (256 units 2-D / 80 vertical, when the candidate's
enemy is someone I hate). `[VtMB] [script/data]`

### The base condition table

`FUN_102c8ce0` registers one dense namespace, `0x00`–`0x76`, 119 entries; `thunk_FUN_102beae0` is a
wrapper onto the same table (`0x1090ff08 + 0x30`). Roughly 20 derived-class tables carry ids above
`0x76` and were not dumped.

```
00 NONE                      1a INTERRUPT_TIME            34 HIT_BY_DOOR            4e REPEATED_DAMAGE          68 GIVE_WAY
01 SEE_UNKNOWN               1b HAVE_ENEMY_THROW_LOS      35 SHOULD_CHARGE          4f CAN_RANGE_ATTACK1        69 WAY_CLEAR
02 LOST_UNKNOWN              1c CLAW_HINT_INVALID         36 FLYING_WALL_HIT        50 CAN_RANGE_ATTACK2        6a HEAR_DANGER
03 IGNORE_UNKNOWN            1d CLAW_HINT_SPECIAL_INVALID 37 FLYING_NPC_HIT         51 CAN_MELEE_ATTACK1        6b HEAR_THUMPER
04 UNKNOWN_RUN_TIMER         1e INVESTIGATE_LEVEL         38 WAS_BUMPED             52 CAN_MELEE_ATTACK2        6c HEAR_BUGBAIT
05 UNKNOWN_ADVANCING         1f CRIMINAL_FLEE_LEVEL       39 COVER_FAILURE          53 PROVOKED                 6d HEAR_COMBAT
06 UNKNOWN_HOLDING           20 CRIMINAL_ATTACK_LEVEL     3a ENEMY_BLOCKED          54 NEW_ENEMY                6e HEAR_WORLD
07 UNKNOWN_RETREATING        21 SUPERNATURAL_FLEE_LEVEL   3b PLAYER_ON_HEAD         55 ENEMY_TOO_FAR            6f HEAR_PLAYER
08 TOO_CLOSE_FOR_RANGED      22 SUPERNATURAL_ATTACK_LEVEL 3c WEAPON_THROUGH_WALL    56 ENEMY_FACING_ME          70 HEAR_BULLET_IMPACT
09 TOO_FAR_FOR_MELEE         23 CAN_POUNCE                3d SEE_CORPSE             57 BEHIND_ENEMY             71 HEAR_PHYSICS_DANGER
0a BEING_ATTACKED            24 PASS_OUT                  3e SEE_CORPSE_FRIEND      58 ENEMY_DEAD               72 HEAR_FLINCH
0b DETECTED_ATTACK           25 INVESTIGATE_SOUND         3f LOW_PRIMARY_AMMO       59 ENEMY_UNREACHABLE        73 FLOATING_OFF_GROUND
0c SHOULD_DODGE              26 INVESTIGATE_SIGHT         40 NO_PRIMARY_AMMO        5a SEE_PLAYER               74 PLAYER_PUSHING
0d SHOULD_BLOCK              27 COMFORT                   41 NO_SECONDARY_AMMO      5b SEE_NEMESIS              75 NPC_FREEZE
0e SHOULD_STEPBACK           28 KNOCKBACK                 42 NO_WEAPON              5c TASK_FAILED              76 NPC_UNFREEZE
0f SHOULD_KICK               29 HINT_INVALID              43 SEE_HATE               5d SCHEDULE_DONE
10 SHOULD_INTERACT           2a KICK_PROP_INVALID         44 SEE_FEAR               5e SMELL
11 SHOULD_LOITER             2b PLAYER_SNARL_RANGE        45 SEE_DISLIKE            5f TOO_CLOSE_TO_ATTACK
12 CROSSWALK_WALK            2c STOP_BACKUP               46 SEE_ENEMY              60 TOO_FAR_TO_ATTACK
13 CROSSWALK_DONTWALK        2d SEE_SOUND_SOURCE          47 LOST_ENEMY             61 NOT_FACING_ATTACK
14 OUTSIDE_INTERRUPT_DIST    2e EXTENDED_BLOCKED_BY_FRIEND 48 ENEMY_OCCLUDED        62 WEAPON_HAS_LOS
15 INSIDE_INTERRUPT_DIST     2f WAITING_ATTACK_TIME       49 TARGET_OCCLUDED        63 WEAPON_BLOCKED_BY_FRIEND
16 OUTSIDE_INTERRUPT_DIST_E  30 ON_FIRE                   4a HAVE_ENEMY_LOS         64 WEAPON_PLAYER_IN_SPREAD
17 INSIDE_INTERRUPT_DIST_E   31 SQUAD_SEE_ENEMY           4b HAVE_TARGET_LOS        65 WEAPON_PLAYER_NEAR_TARGET
18 OUTSIDE_INTERRUPT_DIST_F  32 SQUAD_LOS_ENEMY           4c LIGHT_DAMAGE           66 WEAPON_SIGHT_OCCLUDED
19 INSIDE_INTERRUPT_DIST_F   33 HEAR_FLANK_SOUND          4d HEAVY_DAMAGE           67 BETTER_WEAPON_AVAILABLE
```

Every identity the port already carried agrees with this dump; its seven placeholders (`SEE_ENEMY`,
`SEE_FEAR`, `HEAR_COMBAT/PLAYER/WORLD/DANGER`, `INVESTIGATE_LEVEL`) are now the registered numbers.
`[VtMB]`

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

## The sense pass for a hated player, walked (2026-09-08)

Recovered against `sp_tutorial_1`'s `thug_1` (`npc_VVampire` via `npc_maker`, `vision 540`,
`hearing 1.00`, `npc_perception 3`, `player_reaction "D_HT 5"`). The chain is
`CAI_BaseNPC::GatherConditions` (`0x1026ec30`) → `PerformSensing` (`0x1026e4f0`, gated on
`m_iIsOblivious < 1`) → `CAI_Senses::PerformSensing` (`0x10310710`, gated on `senses+0x80
m_bCanPerformSenses`) → `Look(m_LookDist)` (`0x1030ff10`) then `Listen()` (`0x1030f940`).

**Look.** `CAI_Senses` is built in `0x1027cc10` with **`m_LookDist = 3072.0`** (`+0x10`, datamap,
save-restored, no other writer) and three cadences: **players every 0.15 s** (`+0x68/+0x6c`), NPCs
0.25 s, objects 0.45 s. `LookForPlayers` (`0x1030fff0`) walks client slots with a `3072²`
distance prefilter; `LookForNPCs` (`0x10310130`) walks the global AI list the same way. Per
candidate (`0x1030ffa0`): not already seen (`0x1030fb90`); `0x1030fa00` — alive (slot 158),
not `FL_NOTARGET` (0x8000), not `spawnflags & 1`, `+0x1480` targetable, not hidden
(`0x100b5190`), then **`QuerySeeEntity` slot 468 = `0x102b38b0`**; then **`FInViewCone` slot
363 = `0x102b4540`** and **`FVisible` slot 201 = `0x102b4630`** (trace mask `0x2804091`); then
`0x1030fc50` dispatches **slot 472 (`0x102b3e00`, the attention path)** and appends to the seen
list. `QuerySeeEntity` admits **any player** regardless of relation (`ent+0xa8 != 0`) and
non-players only at `D_HT`/`D_FR`; rejected by `DAT_10924fba` (all-blind), `DAT_10924fb9`
(`ai_ignoreplayers`) and the frenzy friend-player.

**Cone.** `CBaseCombatCharacter::FInViewCone` (`0x10326750`) → `FinViewCone3dNew`
(`0x103264d0`; the 2-D variant when the cvar object at `0x10936f74` reads 2). Inputs:
`m_flFieldOfView`, the candidate position (slot 192) and the candidate-side cone scalar (slot 29,
`CHL2_Player` → `m_flStealthVisionCone +0x1c74`). Anything strictly behind (`dot < 0`) is out.
**`CAI_BaseNPCTroika` spawn (`0x10298d30`) sets `m_flFieldOfView = 0.2`** — ±78.5°, ≈157°
total, for every VtMB humanoid. Player 0.5; `CNPC_Crow` −1.0; `CNPC_VMingXiao`/`VTzimisce` −0.5.
UNRECOVERED: the body-offset term inside `0x103264d0` (register-garbled decompile).

**The Troika cone override `0x102b4540` (slot 363), walked — and NOT PORTED.** Every sight
admission and the sound sweep's `SEE_SOUND_SOURCE` stranger arm dispatch the cone test virtually,
so on a VtMB NPC the function that actually runs is this override, not the base:

1. `target == NULL` → false.
2. `DAT_10924fba` (all-blind) → false.
3. `DAT_10924fb9` (`ai_ignoreplayers`) and `target+0xa8` (the target is a player) → false.
4. `GetTarget()` (slot 293) non-null, `GetTarget() == m_hClosestPlayer` (+0x628c), `target+0x98`
   non-null and the byte `*(target->+0x98 + 0x6279)` set → **return true, skipping the cone
   entirely**.
5. Otherwise the base `CBaseCombatCharacter::FInViewCone` `0x10326750`.

`FElysiumNpcSenses::IsInViewCone` ports step 5 only. Arms 2 and 3 are the two dev globals, which
also gate `QuerySeeEntity` above and are off in shipped play, so the observable gap is **arm 4**: an
NPC whose current target is the closest player accepts that player at ANY angle when the flag byte
is set. Reproducing it needs `CAI_BaseNPC+0x98`'s entity and `+0x6279`, both still UNRECOVERED —
`+0x98` is also read by `OnLooked` (`0x1026a2c0`) through slot `0x928`, which is the lead to follow.
This is a story-6 gap surfaced while porting 10a, which is a new consumer of the same test; it is
recorded here rather than silently ported around.

**Admission body `0x102b4760` (slot 594), exactly.** `+0x6081` (outer-band byte, no datamap
name) is cleared per call. `vecMe = EyePosition()`, `vecHim = target EyePosition()`. The range
test runs only when `(m_NPCState != 2 || m_bEnemyWentOccluded(+0x5bc5) != 0) && curtime >=
m_flStealthVisionOverrideTime(+0x6604)`: `d = |vecHim − vecMe|` (true 3-D);
`radius = target->vtable[+0x70]() /*m_flStealthVisionScalar*/ × m_flSeekDistInspection(+0x63b8)`;
`d > radius` → false (strict); `d > 0.7·radius` → `+0x6081 = 1`. Then the concealment test
`0x10146b20(targetCC, this)` (Obfuscate stats `0xe`/`1` > 0, or beyond the per-observer obfuscate
radius from `DAT_10738d10` `+0xac`) → false. **Two bypasses:** a COMBAT-state NPC with a
non-occluded enemy admits every candidate on range out to the 3072 prefilter (the only unbounded
sight path in retail); and `+0x6604` (`m_flStealthVisionOverrideTime`), which spawn
(`0x1029a0b0`) clears and the Troika damage override (`0x102beda0`) extends through
`0x1028e8b0` → `0x1028e940` by five seconds after a surviving hit with an attacker. It bypasses
only the ordinary range test; it does not alter `IRelationType` or itself create an enemy-memory
actor record. `CAI_BaseNPCTroika::FVisible` (`0x102b4630`) = slot 594 →
`HasStatusEffect(Dominate_BrainWipe)` (`0x1033d2f0`) → `CBaseEntity::FVisible` trace.

**`OnLooked` and the memory write.** Troika `0x102b39a0` (slot 469) adds only
`if (HasCondition(NEW_ENEMY 0x54)) m_iEnemySightings++` and chains to `CAI_BaseNPC::OnLooked`
(`0x1026a2c0`): clear the SEE family (table `0x105c979c`, 6); compute the one skipped entity
(`GetBestSeeUnknown` slot 586 → `m_hBestSeeUnknown +0x6088`, or `m_hLastSeeUnknown +0x608c`
under `IGNORE_UNKNOWN`); for each seen entity `rel = IRelationType(ent)`:
- player (`ent+0xa8`) → **`SetCondition(0x5a COND_SEE_PLAYER)`** (`0x5a`, not `0x6f` — `0x6f` is
  `HEAR_PLAYER`), plus the player-side observer publication `0x1017ff40` when `m_bIsBCCTargetable`;
- `rel != D_NU`: `ent == GetEnemy()` → `SEE_ENEMY 0x46`; **`D_HT`**: `p = IRelationPriority(ent)`
  — `p < 0` → `m_hLastSeenDislikeEnt`, `SEE_DISLIKE 0x45`; `0 ≤ p < 11` → `m_hLastSeenHateEnt`,
  **`SEE_HATE 0x43`**; `p ≥ 11` → `m_hLastSeenNemesisEnt`, `SEE_NEMESIS 0x5b`; then
  **`UpdateEnemyMemory(ent, origin)` slot 544**; **`D_FR`** → `m_hLastSeenFearEnt`,
  `UpdateEnemyMemory`, `SEE_FEAR 0x44`; `D_LI`/`D_NU` nothing. (`flags2 & 0x10000` on a `D_HT`
  target diverts into the FEAR arm.) So the shipped `player_reaction "D_HT 99"` rows raise
  `SEE_NEMESIS`, not `SEE_HATE`; `"D_HT 5"` raises `SEE_HATE`. This also closes `COND_SEE_DISLIKE`'s
  disposition: it is `D_HT` with a negative `IRelationPriority`, not a separate `Disposition_t`.

**The attention path `0x102b3e00` (slot 472).** For every candidate that passed cone + trace:
`if (!(flags2 & 0x4000000 NO_UNKNOWN_VISION) && +0x6081 && ShouldInvestigate(ent, false))`: a
player passing `0x101671a0` (visible, or in a grapple with `+0x1540 == 3`, or `FL_DUCKING` and
`!0x101672d0`) → `SetCondition(0x01 SEE_UNKNOWN)`; if `m_hBestSeeUnknown != ent`: set it,
`m_iEnemySightings(+0x60a8)++`; if it equals `m_hLastSeeUnknown` → `+0x60a4
m_iSeeUnknownRepeatSightings++`, clear `IGNORE_UNKNOWN|MADE_INITIAL_RESPONSE`; else fire
**`OnUnknownVisionPlayer`**, `m_hLastSeeUnknown = ent`, `m_vecLastSeeUnknownPos(+0x6090)`,
repeat = 0, `m_flSeeUnknownRunTimer = curtime + RandomFloat(10, 20)`, `m_flSeeUnknownStartTimer
= curtime + RandomFloat(5, 10)`, clear `LOOKED_AT_UNKNOWN|IGNORE_UNKNOWN|MADE_INITIAL_RESPONSE`.
A non-player in the band sets `ATTACK_UNKNOWN` (flags1 `0x800000`). Outside the band:
`m_hBestSeeUnknown` cleared if it was this entity, the three flags cleared. The band arm and the
`SEE_HATE` arm are not exclusive — one candidate can carry `SEE_PLAYER + SEE_HATE + memory` and
`SEE_UNKNOWN` in one pass.

**The numbers for `thug_1`.** `vision 540` is not the sentinel → `+0x63b8 = 540`, perception
inert (it would have given `Inspection_Vision_Distances[3] = 440`). `StealthVisionScalarTable`
spans `0.14 … 1.00`. Effective radius **75.6 … 540 units**; band `0.7×`; cone `0.2` × player cone
scalar `0.50 … 1.00`; LOS mask `0x2804091` eye to eye. At 2380 units no retail sight path admits
the player.

## The enemy memory — `CAI_Memory` (2026-09-08)

`UpdateEnemyMemory` (`CAI_BaseNPC::FUN_102709c0`, slot 544, **no override in any of the 77
classes**): `GetMemory()` (slot 541, `0x10273e10`); squad dedupe on `ent+0x94` (inert for a
player); `IsEluded` (`0x102e0210`: walk `mem+0xc`, match entry `+0x24`, return `+0x35`) → slot
494; then `CAI_Memory::UpdateMemory` `0x102df700(mem, navigator+0x2c, ent, pos, vel)`.

Record (list head **`mem+0xc`**, stride from `0x102df130`):

| off | meaning |
|---|---|
| `+0x00` | last known position |
| `+0x0c` | anchor position (re-latched when moved further than `[0x10497c80]` = 0.0 — always) |
| `+0x18` | last known velocity |
| `+0x24` | **actor EHANDLE** (`-1` for a position-only record) |
| `+0x28` | last-seen `curtime` |
| `+0x2c/+0x30` | nearest nav node ids (`0x102f41b0`), `-1` with no network |
| `+0x34` | position-only byte |
| `+0x35` | **eluded byte — 0 on every create and every refresh** |
| `+0x38` | next |

`CAI_Memory::RefreshMemories` (`0x102df320`) drops an entry only when its handle dies or its
NPC's state (slot 464) is 7 (dead); otherwise it re-copies the target's origin while `curtime <
lastSeen + m_flFreeKnowledgeDuration`. **No time-based expiry**: once sensed, the player is a
permanent `BestEnemy` candidate until eluded or the map ends. `BestEnemy` (`0x102743c0`) walks
this list and nothing else — gates: handle resolves, `!FL_NOTARGET`, `+0x1480`, `!= this`,
`IsAlive`, `IsValidEnemy` (slot 479 = `0x101a6820`, `return 1`, **no override anywhere**),
relation ∈ {D_HT, D_FR}, `!eluded`; ranking reachability (`+0x848`) > `IRelationPriority` >
integer distance, visibility = `senses->DidSeeEntity` (`0x1030fb10`) or `FVisible`.

**Port consequence.** This runtime's `BestEnemy` walked the world entity list gated on the
relationship table (`ElysiumNpcEnemy.cpp`), so a `D_HT` player anywhere on the map was a
candidate the moment an NPC spawned; verified live on `sp_tutorial_1` 2026-09-08 (`thug_1`
committed the player at ~2380 units with 0 sightings and 10 failed LOS checks, and ran to him).
The record store above is the missing subsystem; see `docs/specs/0005-park-stealth/spec.md`.

## Dialogue does not gate bystanders (2026-09-08)

`CBasePlayer::StartPlayerDialog` (`0x10178280`) writes the player's own `m_hDialogPartner`
(`SetDialogPartner` `0x10107050`), `m_bIsImmobilized (+0x19f7) = 1` (a SendProp with **zero
server-side readers**), forces `item_w_unarmed`, opens the UI and rebuilds the transmit list
(`0x100826b0`). The NPC side, `CAI_BaseNPCTroika::StartTalking` (`0x102c0270`), sets its own
partner. **Every one of the 25 readers of `m_hDialogPartner` reads `this->`**; likewise
`m_bInChoreoScene` (`+0x5bc4`, 8 self accesses). The one AI effect is in `CAI_BaseNPC::RunAI`
(`0x1026f110`): `if (!scriptOwner && !m_hDialogPartner) GatherConditions();` — **the dialogue
partner itself stops sensing, remembering and choosing enemies but still maintains its
schedule.** No other NPC can see the player's dialogue state; `IsValidEnemy`, `BestEnemy`,
`ShouldChooseNewEnemy`, `GatherEnemyConditions`, both `SelectIdealState`s and `SelectSchedule`
carry no dialogue term; `COND_NPC_FREEZE` (`0x75`) has one setter (`0x1027c1f0`, a command body
with no recovered caller). A hated NPC inside its effective radius acquires and chases the player
mid-conversation.

## Hearing, walked (2026-09-08)

**Insertion.** `CSoundEnt::InsertSound` `0x101bac90` (thunk `0x1000bca8`) `(iType, origin,
iVolume, flDuration, bOccludable, pOwner)`. Records are stride `0x2c` at `CSoundEnt
(DAT_1072c464) + 0x464`: `+0x00` owner EHANDLE, `+0x04` type bitmask, `+0x08` volume/radius
(**int**), `+0x0c` insertion `curtime`, `+0x10` expire, `+0x14` occludable, `+0x18/+0x1c` links,
`+0x20` origin. Types (DevMsg switch `0x101badab`): `1 COMBAT`, `2 WORLD`, `4 PLAYER`,
`8 DANGER`, `0x10 BULLET_IMPACT`, `0x20 CARCASS`, `0x40` unnamed, `0x80 GARBAGE`,
`0x100 THUMPER`, `0x200 BUGBAIT`, `0x400 PHYSICS_DANGER`, `0x800 FLINCH`. A player-owned insert
at the "loud" row (`VolumeLevels[3]`, 1200) also calls the masquerade hook
`0x10227a30(&DAT_10750cb4, player, 6)`. **Player footsteps never call `InsertSound`**: the
player's one reserved `CSound` is rewritten every think by `UpdatePlayerSound` (`0x1016b480`;
`footsteps.md` §2.5) — type 4, radius from `sound_volume_table.txt` (sneak 180, walk/run 240,
jump 240, land soft 180, land hard 240), `m_flTime = curtime`, so the freshness gate below
always passes. The only type-4 `InsertSound` is the **door** (`0x100ee560`: `DOOR_STEALTH` 250 /
`DOOR_NORMAL` 500, duration 2.0, owner = the activator, skipped on `spawnflags & 0x1000`).

**Listen** (`CAI_Senses::Listen` `0x1030f940`): `mask = GetSoundInterests()` (slot 473; VVampire
`0x103846e0`); for each active sound with `type & mask` and `CanHearSound` (`0x1030f7b0`) link it;
`OnListened` (slot 470); `senses+0x84 = curtime`. `CanHearSound`, asm-exact: owner hidden
(`0x10014da8`) or not targetable (`+0x1480 == 0`) → reject; owner == me → reject; **`sound.time
<= senses+0x84` (not inserted since my last Listen) → reject**; `d = |origin − EarPosition()|`
(slot 196, 3-D); **`radius = HearingSensitivity() × (int)volume`** — slot 476 `0x101aa5f0` returns
`+0x63c0` verbatim, so `hearing` is a **multiplier on the sound's radius**, never on distance;
`AdjustSoundDistForStealth` (`0x1009d850`, in place, any owner with the stealth surface,
`StealthHearingDistTable` 0…80 units); occlusion `0x102703f0` when occludable; `d > radius` →
reject; then `QueryHearSound` (slot 467, Troika `0x102b35b0`): `DAT_10924fba`; `ai_ignoreplayers`
for a player owner; frenzy friend; owner == self; the same concealment test as sight
(`0x10146b20`) — **Obfuscate silences as well as hides**; **type 4 from a player →
`CStealthKillRules::InDeafZone(&DAT_1072c540, player, this)` ⇒ false** (the deaf arc suppresses
the player's movement sound outright); `flags1 & 0x20400` (COWERING bit 10, SLEEPING bit 17)
→ a second test at **`HearingSensitivity × volume × 0.25`** (`[0x1044bef8]`).

**`OnListened`** (`CAI_BaseNPC` `0x1026a5e0`): clear the HEAR family (table `0x105c97b4`, 10),
zero the six-dword heard-type field at `+0x5ca8`, map type → condition: `1→0x6d HEAR_COMBAT`,
`2→0x6e HEAR_WORLD`, `4→0x6f HEAR_PLAYER`, `8→0x6a HEAR_DANGER`, `0x10→0x70 HEAR_BULLET_IMPACT`,
`0x100→0x6b HEAR_THUMPER`, `0x200→0x6c HEAR_BUGBAIT`, `0x400→0x71 HEAR_PHYSICS_DANGER`,
`0x800→0x72 HEAR_FLINCH`, others DevMsg and dropped; `0x101b9920` false → `0x5e SMELL`. **The
condition is not set immediately** — it is queued on `m_DelayedConditionList` with delay slot 471
(`0x1026a8a0` = `RandomFloat(0.2, 0.9)`; `HEAR_FLINCH` `RandomFloat(0, 0.5)`), then `0x102cc760`
promotes what is due. Outputs: `OnHearWorld` on `0x6e`, **`OnHearPlayer` on `0x6f`**,
`OnHearCombat` on `0x6d || 0x70 || 0x6a`. The Troika override `0x102b39e0` then snapshots one
`CSound` per heard type into **seven** records — `m_LastSoundWorld +0x61e4`, `PhysicsDanger
+0x6134`, `Danger +0x6108`, `Player +0x61b8`, `BulletImpact +0x618c`, `Combat +0x6160`, and
**`Flinch +0x6210`** (a seventh, missing from the sweep section above) — and calls
`0x1028e8b0(this, owner, 1.0)` for `HEAR_COMBAT`/`HEAR_BULLET_IMPACT` only (forwarded to
`0x1028e940` only when the owner is or shares my enemy).

**Hearing cannot acquire an enemy.** Nothing in the hear path writes `CAI_Memory`.
`HEAR_PLAYER` yields the delayed condition, the `m_LastSoundPlayer` copy (on the NPC, not in
memory), `OnHearPlayer`, the idle→alert promotion in `CAI_BaseNPC::SelectIdealState`
(`0x1026f660` case 1, no `m_bNoAlertState` test), and via the sound sweep + `ShouldInvestigate`
`COND_INVESTIGATE_SOUND 0x25`. An NPC turns hostile from a noise only by walking to it and then
seeing the player there.

**`CommitBestSound` `0x102b4090`** (the sweep→task seam): copies the winning record into
`m_BestSound +0x60b0`, `+0x5b78 = owner`, mirrors to `+0x60dc`. Priority, first match:
`HEAR_COMBAT` (+0x6160) > `HEAR_BULLET_IMPACT` (+0x618c) > **`HEAR_FLINCH` (+0x6210)** >
`HEAR_PLAYER` (+0x61b8) > `HEAR_DANGER` (+0x6108) > `HEAR_PHYSICS_DANGER` (+0x6134) >
`HEAR_WORLD` (+0x61e4). `CAI_BaseNPCTroika::GetBestSound` (slot 474, `0x102b4520`) returns
`&m_BestSound` unconditionally, so the "no best sound → `TaskFail`" arms of
`TASK_ALERT_LOOK_AT_BEST_SOUND` / `TASK_GET_PATH_TO_BESTSOUND` are dead on every Troika NPC.

**NOT PORTED: the two records are one.** `CommitBestSound` writes the winner to `m_BestSound`
(`+0x60b0`) and then mirrors it to `m_InvestigateSound` (`+0x60dc`); the runtime carries a single
`FElysiumNpcMemory::BestSound`. They are distinguishable in retail — `+0x60b0` is the volatile
task-facing record `GetBestSound` (slot 474) hands out and every commit overwrites, while `+0x60dc`
is the sticky saved decision copy that `ShouldInvestigate`, `GetSchedule` (`0x102ae920`),
`0x102993c0` and `0x10299700` read, and which `FUN_102b9060`'s `HEAR_WORLD` arm writes WITHOUT
going through `CommitBestSound` (so that arm updates `+0x60dc` and leaves `+0x5b78` stale). None of
those readers exists yet, so the collapse is not observable today; the story that builds the sound
selectors must split the field before wiring `FUN_102b9060`.

**Port R6 state boundary (2026-09-08).** `FElysiumNpcSenses` keeps actual `Look` candidates
separate from the two-second closest-player/PVS/LOS cache: 3072-unit prefilter, then player
0.15 s, NPC 0.25 s, object 0.45 s cadence, full 3-D apex/cone/scalar test, admission, and trace.
Only an actual D_HT/D_FR observation writes `FElysiumNpcEnemyMemory`; cached player LOS never
replays that write. The senses record also retains the slot-472 unknown-vision handles/position,
repeat/timers and the seven raw sound snapshots. `m_flStealthVisionOverrideTime` is saved as the
live five-second range-only deadline. BrainWipe/Obfuscate ownership remains an explicit target
status seam until its existing discipline state has a stable substrate accessor; no guessed flag is
used as a substitute.

### R6 integration corrections from raw bodies (2026-09-08)

- `FinViewCone3dNew` disassembly `0x103265af` rejects a negative front-plane dot before the
  apex offset; `0x1032669c` multiplies the normalized viewing cosine by the **target scalar**,
  then compares against the observer's FOV. A smaller scalar narrows the cone. The unnamed
  `0x10937a8c` ConVar's default remains unrecovered; `ViewConeBodyOffsetCm` is its explicit
  zero-answer seam, not a claim that retail's default is zero.
- `FVisible` `0x102b4630` calls `HasStatusEffect(Dominate_BrainWipe)` on **this observer**.
  `0x10146b20(target, observer)` returns permission to perceive: no active cloak, observer
  active stat `0xe` or `1`, or the ready observer detection record admitting the distance.
  Those observer stats do not cause concealment; the earlier summary inverted this branch.
  The port reads the active sheet and tracked BrainWipe effect. The cloak `+0x14dc` and
  detection record `+0x97/+0xac` are explicit fields whose effect producers remain in 0007.
- `OnLooked` `0x1026a2c0` skips the current best unknown (or last unknown under
  `IGNORE_UNKNOWN`) before the player and relationship arms. All three D_HT priority branches
  write enemy memory. `D_CALM` is on the **observer**; its D_HT diversion reaches the D_FR arm
  whose second D_CALM test suppresses that arm as well.
- `OnListened` `0x1026a5e0` queues the exact raw type's condition, calls delayed promotion,
  then fires `OnHearWorld`/`OnHearPlayer`/`OnHearCombat` with **self as activator**. Both the
  condition and output are delayed. A combined raw type is not expanded into multiple sounds.
  `0x102cc6c0` has eight pending entries; the capacity check precedes duplicate lookup.
  `0x102cc590` keeps the earlier deadline when another sound of that type arrives.
- `GetClosestSound` `0x103105d0` prefers the current enemy's sound, otherwise the nearest
  sound of that type in the current Listen. Retained snapshots are separate from delayed
  conditions. `CommitBestSound` chooses only among conditions currently raised, preventing an
  old combat snapshot from permanently outranking fresh footsteps.
- Sound interests (slot 473): Troika/humanoids/cop `0x81f` (`0x102b4070`, `0x103846e0`,
  `0x10387180`); animals `0x1f` (`0x1035f540`); cameras zero (`0x103692a0`); pedestrians
  `0x81d` (`0x103a28f0`); zombies `0x17` (`0x103df260`).
- The V2 `vdata/system/stealthkillrules.glb` source member SHA-256
  `6cf156a97285ca81f4015a01aab4b4038bf29774aa0ba9cf9b226443cc03e4ef` authors arc keys **1..10**,
  a 95-unit maximum distance and a 2.5 maximum hearing scalar. Those override loader defaults
  70 and 3.0. `InDeafZone` `0x101be710` suppresses the player's type-4 sound only when eligible,
  strictly inside the rear arc and **farther than** the minimum approach depth.

## `ambient_generic` as an AI sound source (2026-09-08)

Datamap: `+0x450 m_radius` (`radius`, audio only), `+0x4cc m_nSoundEvent` (**`sound_event`**),
`+0x4d0 m_nSoundEventLevel` (**`sound_event_level`**), `+0x4d4 m_iszSoundEventOwner`
(`sound_event_owner`). The one consumer is the Use/Toggle handler `0x101ad470`, in the branch
that starts playback, after `EmitSound`:
`if (m_nSoundEvent != 0)`: resolve the owner (`0x101ad9a0`); a null owner is legal only for
`CARCASS 0x20` / `FLINCH 0x800`, otherwise `Warning("… invalid NPC sound event …")` and nothing is
inserted; `lvl` outside `1..3` warns and clamps; `dur = GetSoundDuration(wav)` floored at 1.0;
**`InsertSound(m_nSoundEvent /*raw type bitmask*/, owner origin, VolumeLevels[lvl], dur,
bOccludable = 0, owner)`** — **once per activation, never per think**. Level 1 = 180, 2 = 240,
3 = 1200 (`0x1072bccc + lvl*4`). `radius` plays no part.

**Every `ambient_generic` in `sp_tutorial_1` carries `sound_event "0"`** — `sound_combat_1..4`,
`sound_jackflash`, `sound_window_break`, `sound_fire_2`, `sound_thugs_w_guns`, `sound_howl_2` —
so the tutorial's "gunfire diversion" (`trig_diversion → logic_gunfire`) is **audio only**: it
inserts no AI sound and no NPC hears it. `sound_event_level 2` is dead data on all of them.

## Interesting places: the selector, the programs, the wait (2026-09-08)

**Selector** (`CAI_BaseNPCTroika::SelectSchedule` case 1 step 4, asm `0x102af6eb`–`0x102af7f3`):
```
if (m_bUseInteresting +0x63d9) {
  if (navigator state (0x100037e2) == 8 || m_hInterestingPlace +0x62ec != 0) {
    if (HasCondition(0x13 CROSSWALK_DONTWALK)) return 0x102;
    if (HasCondition(0x10 SHOULD_INTERACT))    return 0x106;
    if (HasCondition(0x11 SHOULD_LOITER))      return 0x105;
    return 0x100;
  }
  return 0xff;
}
```
Producers: `0x12/0x13` from `UpdatePedestrianInfo` (`0x102a0d20`); `0x10` from `0x102a0cb0`
(sets it on both NPCs; **zero recovered callers**); **`0x11 SHOULD_LOITER` has no setter in the
image** — `SCHED_TROIKA_LOITER` is unreachable from this selector in retail.

**Blobs** (byte-scanned; registrar `FUN_102b9810`):
- `0xff SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP` (`0x105e6aa8`): `SET_FAIL_SCHEDULE
  Idle_Stand; FIND_INTERESTING_PLACE; SET_PRESERVE_PATH 1; SET_SCHEDULE 0x100`. Interrupts
  `NEW_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT
  IGNORE_UNKNOWN DETECTED_ATTACK PLAYER_ON_HEAD`.
- `0x100 SCHED_TROIKA_WALK_TO_INTERESTING_PLACE` (`0x105e67b8`): `SET_FAIL_SCHEDULE
  …_FAILED; GET_PATH_TO_INTERESTING_PLACE; WALK_PATH; WAIT_FOR_MOVEMENT; STOP_MOVING;
  FACE_INTEREST; SET_SCHEDULE SCHED_TROIKA_DO_INTEREST_ACTIVITY`. Interrupts add `GIVE_WAY
  SHOULD_INTERACT SHOULD_LOITER CROSSWALK_DONTWALK`.
- `SCHED_TROIKA_DO_INTEREST_ACTIVITY` (`0x105e6130`): `SET_FAIL_SCHEDULE Idle_Stand;
  SET_PRESERVE_PATH 0; DO_INTEREST_ACTIVITY; WAIT_PVS`. `…_FAILED` (`0x105e6520`):
  `SET_ACTIVITY ACT_IDLE; WAIT 5; WAIT_RANDOM 5; SET_SCHEDULE 0xff`.
- `0x105 SCHED_TROIKA_LOITER` (`0x105e5eb8`): `SET_FAIL_SCHEDULE Idle_Stand; STOP_MOVING;
  DO_LOITER_ACTIVITY; WAIT_PVS`. `0x106 SCHED_TROIKA_INTERACT` (`0x105e5d10`): `…;
  DO_INTERACT_ACTIVITY; WAIT_PVS`. `0x102 SCHED_TROIKA_WAIT_AT_CROSSWALK` (`0x105e6308`):
  `…; PAUSE_MOVING; FACE_NEXT_NODE; WAIT_INDEFINITE`, interrupt `CROSSWALK_WALK`.
None of these carries `SEE_ENEMY` or any `HEAR_*` — an interest program is broken by
`NEW_ENEMY` (the enemy actually replaced), not by merely seeing.

**Eligibility** (`0x102dad60`, restating with the field names): `m_bEnabled +0x57c` (key
`enabled`), `+0x57d == 0` (dead: only the constructor writes it, to 0), `max_npcs +0x584 −
+0x58c − +0x588 > 0`, `(m_iGroupID +0x574 & m_iInterestingPlaceGroups +0x62dc) != 0`, `|place −
npc|² ≤ 1.0e8`. **Both sides are bitmasks** (corrected 2026-09-08): `CAI_InterestingPlace::Spawn`
(`0x102d9c20`) converts the place's `group_id` `1..32` to `1 << (id−1)` and anything else to
`1` (group 1), and clamps `rating` to `0..5`; the NPC's `interesting_place_groups` is a
space-separated list of 1-based indices → mask (`FUN_10298910`; empty or `"0"` → 0, no place
ever matches). So a place matches only an NPC that lists its group. On `sp_tutorial_1` `thug_1`
(`"2"`) matches `pt1`, `pt2`, `pt3` (all `group_id 2`) and nothing else; with `pt2`/`pt3`
authored `enabled 0` his pool at spawn is **`pt1` alone**, 30–60 s per visit. `m_bEnabled`'s only
writers are `InputEnable` (`0x102db420`) and `InputDisable` (`0x102db440`).

**The wait.** `0x102a9f40(npc, place, activityIdx, bInto)` — shared by
`TASK_DO_INTEREST_ACTIVITY` / `_LOITER_` / `_INTERACT_` (both call sites in Troika `StartTask`):
`ClaimMarker` (`0x102da7c0`); fire `npc+0x5f74`; if the type has an INTO activity
(`0x102dae70`) → `flags1 |= 0x20000000 INTERESTING_INTO`, `+0x6304 = 1`, `SetActivity(into)`,
`+0x63d4 = -1`; else `+0x6304 = 2`, `SetActivity(idle)`, `+0x63d4 = curtime + RandomFloat(2,
10)`; **`m_flWaitFinished (+0x5db4) = curtime + RandomFloat(m_fMinStayTime +0x568, m_fMaxStayTime
+0x56c)`** — the same field every `TASK_WAIT*` uses, no clamp, no swap (`ip_20`'s authored
`10/5` is a shipped inversion); `match_orientation +0x570` sets the yaw from the marker.

## The `INVESTIGATE` family, decoded (2026-09-08)

691 schedule blobs in the image; **31** carry `INVESTIGAT` (the doc's earlier count of 32 included
`SCHED_INVESTIGATE_SOUND`, a Source-SDK name at `0x105d3754` that lives only in the stale debug
name array `0x105d1488` — no blob, no id; never use that array for numbers). Registration in
`FUN_102b9810` is `mov [esp+0x10], name; mov [esp+0x14], id; call 0x1001528a` immediately before
the blob's `call 0x1000ce8c`. **No `INVESTIGATE` program declares `Flags`** (no
`DELAY_INTERRUPTS`).

| id | name | blob |
|---|---|---|
| 0x50 | SCHED_TROIKA_INVESTIGATE_SOUND_FLINCH | 0x105ff320 |
| 0x51 | SCHED_TROIKA_INVESTIGATE_SOUND | 0x105fef58 |
| 0x52 | SCHED_TROIKA_INVESTIGATE_OTHER_SOUND | 0x105feca8 |
| 0x53 | SCHED_TROIKA_INVESTIGATE_SOUND_FLINCH_NO | 0x105feb38 |
| 0x54 | SCHED_TROIKA_INVESTIGATE_SOUND_NO | 0x105fe860 |
| 0x58 | SCHED_TROIKA_ALERT_INVESTIGATE_UNKNOWN_ATTACKER | 0x105fdb78 |
| 0x59 | SCHED_TROIKA_INVESTIGATE_UNKNOWN | 0x105fd948 |
| 0x5a | SCHED_TROIKA_INVESTIGATE_UNKNOWN_QUICK | 0x105fd728 |
| 0x5b | SCHED_TROIKA_INVESTIGATE_UNKNOWN_ATTACK | 0x105fd5c0 |
| 0x5c | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER | 0x105fd298 |
| 0x5d | SCHED_TROIKA_INVESTIGATE_UNKNOWN_LOST | 0x105fcfb8 |
| 0x5e | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER_RUN | 0x105fcc80 |
| 0x5f | SCHED_TROIKA_INVESTIGATE_UNKNOWN_LOST_RUN | 0x105fc960 |
| 0x60 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_IGNORE | 0x105fc698 |
| 0x61 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER_IGNORE | 0x105fc3b0 |
| 0x62 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_RETURN | 0x105fbf50 |
| 0x63 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_RETURN_GIVEUP | 0x105fbae8 |
| 0x64 / 0x66 / 0x68 | SCHED_TROIKA_INVESTIGATE_NODE / _WALK / _HUNT | 0x105fb858 / 0x105fb360 / 0x105faea0 |
| 0x7f | SCHED_TROIKA_HUNT_INVESTIGATE_FLINCH | 0x105f87b0 |
| 0x80 | SCHED_TROIKA_HUNT_INVESTIGATE | 0x105f8510 |
| 0x81 | SCHED_TROIKA_HUNT_INVESTIGATE_UNKNOWN | 0x105f8240 |
| 0x82 | SCHED_TROIKA_HUNT_INVESTIGATE_UNKNOWN_LOST | 0x105f7fa8 |

Species tables (own id spaces): `0x15a VHENGEYOKAI_INVESTIGATE_UNKNOWN_ATTACK` (`0x1063eeb0`),
`0x15b/0x15c VFRENZYSHADOW_HUNT_INVESTIGATE(_UNKNOWN)` (`0x106388f8`/`0x106386a0`),
`0x15c/0x15d VTZIMISCE_HUNT_INVESTIGATE(_UNKNOWN)` (`0x1065b7e0`/`0x1065b568`),
`0x15d VGARGOYLE_INVESTIGATE_UNKNOWN_ATTACK` (`0x10639db0`, `TASK_LOOK_AT_PLAYER`),
`0x168 VZOMBIE_IGNORE_INVESTIGATE_UNKNOWN_ATTACK` (`0x10665120`).

**The programs, verbatim.**

`0x51 INVESTIGATE_SOUND`: `STOP_MOVING; STORE_LASTPOSITION; REMEMBER MEMORY:INVESTIGATING;
ALERT_LOOK_AT_BEST_SOUND; WAIT_RANDOM 0.5; PLAY_SOUND SOUND:Target_Suspect; WAIT_RANDOM 1.5;
SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND; SET_NPC_FLAG IGNORE_DOOR_FAILURE;
SET_FAIL_SCHEDULE SCHED_TROIKA_ALERT_LOOK_AROUND; WALK_RUN_PATH_COMBAT_SOUND; WAIT_FOR_MOVEMENT;
SET_SCHEDULE SCHED_TROIKA_ALERT_LOOK_AROUND`. Interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY
SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND SEE_SOUND_SOURCE PLAYER_ON_HEAD`.
`0x52 INVESTIGATE_OTHER_SOUND`: `PLAY_SOUND Target_Suspect; SET_TOLERANCE_DISTANCE 5;
GET_PATH_TO_BESTSOUND; SET_NPC_FLAG IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE ALERT_LOOK_AROUND;
WALK_RUN_PATH_COMBAT_SOUND; WAIT_FOR_MOVEMENT; SET_SCHEDULE ALERT_LOOK_AROUND`; interrupts as
0x51 minus `SEE_SOUND_SOURCE`. `0x54 _SOUND_NO`: tasks as 0x52; interrupts with
`SEE_SOUND_SOURCE`, without `INVESTIGATE_SOUND`. `0x50 _SOUND_FLINCH`: `ADD_EVENT_EXPRESSION
EXPRESSION:FLINCH; PLAY_COWER ACT_COWER_INTO; SET_SCHEDULE 0x51`; interrupts `LIGHT_DAMAGE
HEAVY_DAMAGE`. `0x53 _FLINCH_NO`: identical, also transfers to **0x51** (retail asymmetry).
`0x58 ALERT_INVESTIGATE_UNKNOWN_ATTACKER`: as 0x51 with `ALERT_LOOK_AT_UNKNOWN_ATTACKER` in
place of the best-sound look; no `SEE_SOUND_SOURCE`.

`0x59 INVESTIGATE_UNKNOWN`: `STOP_MOVING; STORE_LASTPOSITION; REMEMBER INVESTIGATING;
LOOK_AT_BEST_UNKNOWN; WAIT_RANDOM 0.5; PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5; SET_NPC_FLAG
LOOKED_AT_UNKNOWN`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE PLAYER_ON_HEAD`. `0x5a _QUICK`: same minus the first two tasks.
`0x5b _ATTACK`: `SET_ACTIVITY ACT_IDLE; WAIT 0.25; PLAY_SOUND Target_Acquired; WAIT_RANDOM
0.25`; interrupts `NEW_ENEMY LIGHT_DAMAGE HEAVY_DAMAGE`.
`0x5c _OTHER`: `REMEMBER INVESTIGATING; SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTUNKNOWN;
SET_PRESERVE_PATH 1; SET_NPC_FLAG IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE 0x62; WALK_PATH_HUNT;
WAIT_FOR_MOVEMENT; SET_SCHEDULE 0x62`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR
LIGHT_DAMAGE HEAVY_DAMAGE LOST_UNKNOWN UNKNOWN_RUN_TIMER PLAYER_ON_HEAD`. `0x5e _OTHER_RUN`:
adds `SET_NPC_FLAG FORCE_RELAXED_ANIMS`, `RUN_PATH` for `WALK_PATH_HUNT`; interrupts end at
`LOST_UNKNOWN`. `0x5d _LOST`: `REMEMBER INVESTIGATING; SET_PRESERVE_PATH 0; SET_NPC_FLAG
IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE 0x62; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; SET_SCHEDULE
0x62`; interrupts `… HEAVY_DAMAGE INVESTIGATE_SIGHT DETECTED_ATTACK PLAYER_ON_HEAD`.
`0x5f _LOST_RUN`: plus `FORCE_RELAXED_ANIMS`, `RUN_PATH`.
`0x60 _IGNORE`: `STOP_MOVING; SET_NPC_FLAG NO_UNKNOWN_ATTACK; LOOK_AT_BEST_UNKNOWN; WAIT_RANDOM
0.5; PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5; UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK;
SET_NPC_FLAG FINISHED_IGNORE_UNKNOWN`; interrupts `… HEAVY_DAMAGE UNKNOWN_ADVANCING
PLAYER_ON_HEAD`. `0x61 _OTHER_IGNORE`: `SET_PRESERVE_PATH 0; SET_NPC_FLAG NO_UNKNOWN_ATTACK;
LOOK_AT_BEST_UNKNOWN; WAIT 1.0; WAIT_RANDOM 1.0; UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK;
SET_NPC_FLAG FINISHED_IGNORE_UNKNOWN; SET_SCHEDULE 0x52`.
`0x62 _RETURN`: `REMEMBER INVESTIGATING; PLAY_SEQUENCE ACT_IDLE; SET_ACTIVITY
ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 5; PLAY_SOUND SUSPECT_GIVEUP; WAIT_RANDOM 1; SET_ACTIVITY
ACT_IDLE; WAIT_RANDOM 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; WAIT_RANDOM 1; SET_TOLERANCE_DISTANCE
5; GET_PATH_TO_LASTPOSITION; WALK_PATH; WAIT_FOR_MOVEMENT; FACE_LASTANGLE; CLEAR_LASTPOSITION;
FORGET INVESTIGATING; CLEAR_NPC_FLAG LOOKED_AT_UNKNOWN`; interrupts `… HEAVY_DAMAGE
INVESTIGATE_SOUND INVESTIGATE_SIGHT DETECTED_ATTACK`. `0x63 _RETURN_GIVEUP`: `STOP_MOVING;
SET_PRESERVE_PATH 0; REMEMBER INVESTIGATING; PLAY_SOUND Target_Lost; SET_ACTIVITY
ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 4; WAIT_RANDOM 2; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 1;
SET_NPC_FLAG FORCE_RELAXED_ANIMS; SET_TOLERANCE_DISTANCE 5; GET_PATH_TO_LASTPOSITION; WALK_PATH;
WAIT_FOR_MOVEMENT; FORGET INVESTIGATING; FACE_LASTANGLE; CLEAR_LASTPOSITION; CLEAR_NPC_FLAG
LOOKED_AT_UNKNOWN` (forget before facing, unlike 0x62); interrupts add `IGNORE_UNKNOWN`.
`0x64 INVESTIGATE_NODE`: `SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_PATROL_POINT; SET_NPC_FLAG
FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; FACE_PATROL_INTEREST;
DO_PATROL_INTEREST_ACTIVITY; NEXT_PATROL_POINT`; interrupts `NEW_ENEMY SEE_ENEMY
SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT
IGNORE_UNKNOWN DETECTED_ATTACK PLAYER_ON_HEAD`; `0x66 _WALK` uses `WALK_PATH` without the flag;
`0x68 _HUNT` uses `WALK_PATH_HUNT`. These three are **patrol** programs: `SelectSchedule` case 1
returns `*(uint*)(m_sppPatrolPath(+0x6590) + 4)`, the path object's own id, one of the three
`INVESTIGATE_NODE` or three `FOLLOW_PATROL_PATH` (0x65/0x67/0x69) ids.
`0x80 HUNT_INVESTIGATE`: `SET_FAIL_SCHEDULE SCHED_TROIKA_HUNT_LOOK_AROUND; STOP_MOVING;
SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND; FACE_IDEAL; SET_TOLERANCE_DISTANCE 5;
WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; SET_SCHEDULE HUNT_LOOK_AROUND`; interrupts `NEW_ENEMY
LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT SEE_SOUND_SOURCE IGNORE_UNKNOWN
DETECTED_ATTACK`. `0x81 _UNKNOWN`: `…; GET_PATH_TO_BESTUNKNOWN; SET_PRESERVE_PATH 1; FACE_IDEAL;
SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SET_PRESERVE_PATH 0; SET_SCHEDULE
HUNT_LOOK_AROUND`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE LOST_UNKNOWN`. `0x82 _UNKNOWN_LOST`: `SET_FAIL_SCHEDULE HUNT_LOOK_AROUND;
SET_PRESERVE_PATH 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SET_SCHEDULE
HUNT_LOOK_AROUND`. `0x7f HUNT_INVESTIGATE_FLINCH`: as 0x50, transferring to 0x80.

**Selection.** Two testers, and the distinction is load-bearing: `HasCondition` (`0x10269aa0`)
reads the condition set only; **`HasInterruptCondition` (`0x10269d30`)** requires an installed
schedule, translates through the class id-space (slot `+0x910`), and needs the bit in **both**
the condition set (`+0x5c5c`) and the cached interrupt mask (`+0x5c74`). The driving stimulus is
honoured only when the running program lists it; refinements use plain `HasCondition`.

`CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`) **case 3 (ALERT)**, in order: (1)
`FUN_102b8a60`, the see-unknown selector; (2) `FUN_102b8c40`: `HasInterruptCondition(LIGHT_
DAMAGE 0x4c || HEAVY_DAMAGE 0x4d)` → `m_vSavePosition = +0x5b9c`, `0x8a SCHED_TROIKA_SHOT_BY_
UNKNOWN`; (3) `HasCondition(DETECTED_ATTACK 0x0b)` → `0x56 SCHED_TROIKA_ALERT_TURN_TO_DETECTED_
ATTACK`; (4) `FUN_102b7370` door obstruction; (5) **`FUN_102b9060`, the sound-investigate
selector**; (6) `m_bGoToIdleState = 1; m_bForceStateChange = 1;` → `0x4b SCHED_TROIKA_ALERT_WAIT`.

`FUN_102b9060`:
```
if (HasInterruptCondition(INVESTIGATE_SOUND 0x25)) {
  if (HasCondition(HEAR_COMBAT 0x6d) || HasCondition(HEAR_BULLET_IMPACT 0x70)) {
    m_flNextInvestigateSoundTime(+0x623c) = curtime + DAT_10452dc4;  CommitBestSound();
    m_eAlertLevel(+0x63f4) = 3;
    if (m_afMemory & 0x8000000 INVESTIGATING) return 0x52;
    if (!(m_bfNPCFrenziedFlags(+0x5b84) & 0x10000) && RandomInt(0,99) < 100) return 0x50;  // roll is dead: always
    return 0x51;
  }
  if (HasCondition(HEAR_WORLD 0x6e)) { m_BestSound = m_LastSoundWorld; copy to +0x60dc; return 0x51; }
  if (HasCondition(HEAR_PLAYER 0x6f) || HasCondition(HEAR_DANGER 0x6a)) { CommitBestSound(); return FUN_102b8980(); }
}
if (HasCondition(SEE_SOUND_SOURCE 0x2d) && (r = FUN_102b8d20(this, 0x89, 0x73))) return r;
if (!(m_bfNPCFrenziedFlags & 0x10000) && HasInterruptCondition(HEAR_FLINCH 0x72)) { CommitBestSound(); return 0x50; }
return 0;
```
`FUN_102b8980`, the alert ladder (what `full_investigate` short-circuits): `if
(m_bFullInvestigate +0x6340) m_eAlertLevel = 3; switch (m_eAlertLevel +0x63f4) { 0: level = 1,
return 0x4c SCHED_TROIKA_ALERT_TURN_TO_SOUND; 1: level = 2, return 0x4d _ALERT_STEP_TOWARDS_
SOUND; 2/3: level = 3, return 0x51 + (INVESTIGATING ? 1 : 0) }`. Turn → step → walk/run.
`m_eAlertLevel` is written only by `FUN_102b5dc0` (values 1/2/3) and read only here;
UNRECOVERED: what ever resets it to 0. `FUN_102b8d20(this, hitSched, fearSched)` — the
`SEE_SOUND_SOURCE` third-party tail: `src = m_hBestSoundSource(+0x5b78)`, `foe = src->GetEnemy()`;
`IRelationType(foe)`: D_HT → compute `m_vSavePosition (+0x5dd0)`, return `hitSched` (alert
`0x89 RUN_TO_SAVED`, hunt `0x84 HUNT_RUN_TO_SAVED`); D_FR → `fearSched` (`0x73 FLEE_AND_COWER`);
else bump `m_flNextInvestigateSoundTime` by `DAT_1044eb0c`, 0.

`FUN_102b8a60`, the see-unknown selector: `if (HasInterruptCondition(IGNORE_UNKNOWN 0x03) &&
!INVESTIGATING) return 0x60; if (HasInterruptCondition(SEE_UNKNOWN 0x01) ||
HasInterruptCondition(UNKNOWN_ADVANCING 0x05) || HasCondition(INVESTIGATE_SIGHT 0x26)) {
m_eAlertLevel = 3; if (!INVESTIGATING) return 0x59; if (flags1 & 0x200000 LOOKED_AT_UNKNOWN)
return HasCondition(UNKNOWN_RUN_TIMER 0x04) ? 0x5e : 0x5c; return 0x5a; } if
(HasInterruptCondition(LOST_UNKNOWN 0x02)) return (INVESTIGATING && HasCondition(0x04)) ? 0x5f :
0x5d; if (flags1 & LOOKED_AT_UNKNOWN) return 0x5c; return 0;`. Note `INVESTIGATE_SIGHT` is a
plain `HasCondition` — it need not be in the mask.

**Case 0xb (HUNT)**, in order: `SEE_UNKNOWN` or `INVESTIGATE_SIGHT` (interrupt) → `0x81`;
`FUN_102b8c40` damage; `LOST_UNKNOWN` (interrupt) → `0x82`; any of `INVESTIGATE_SOUND 0x25,
HEAR_DANGER 0x6a, HEAR_COMBAT 0x6d, HEAR_WORLD 0x6e, HEAR_BULLET_IMPACT 0x70, HEAR_PLAYER 0x6f`
(interrupt) → re-arm `+0x623c`, `CommitBestSound`, `(HEAR_COMBAT || HEAR_BULLET_IMPACT)` → `0x7f`
else `0x80`; `HEAR_FLINCH` (interrupt) → `0x50`; `SEE_SOUND_SOURCE && FUN_102b8d20(0x84, 0x73)`;
then the hunt-expiry chain (`0x85/0x7c/0x7d/0x7e`). **The hunt state accepts raw `HEAR_*`
conditions; alert reaches sounds only through `INVESTIGATE_SOUND`, i.e. through the interest
predicate.**

Other entries: `0x5b _ATTACK` from `CAI_BaseNPCTroika::GetSchedule` (`0x102ae920`), combat state
only: `if (flags1 & 0x800000 ATTACK_UNKNOWN) { clear; if (!(flags2 & 0x80 NO_UNKNOWN_ATTACK) &&
!(frenzied & 0x80)) return 0x5b; }`. `0x62` and `0x58` are pure data (reached by
`TASK_SET_SCHEDULE`/`_FAIL_SCHEDULE` only). **Dead in code: `0x53`, `0x54`, `0x61`, `0x63`** — no
immediate in `0x102a0000–0x102c9000`, no blob names them; reachable only by name through
`ChangeSchedule`/`aiscripted_schedule`. UNRECOVERED: the `m_bfNPCFrenziedFlags` bit `0x10000`
that gates the flinch arms; `DAT_10452dc4` (investigate-sound re-arm) and `DAT_1044eb0c`.

**`m_afMemory` bits** (token table `FUN_1030c800`): `PROVOKED 1, INCOVER 2, SUSPICIOUS 4,
PATH_FAILED 0x20, FLINCHED 0x40, TOURGUIDE 0x100, LOCKED_HINT 0x400, TURNING 0x2000, TURNHACK
0x4000, HAD_ENEMY 0x8000, HAD_PLAYER 0x10000, HAD_LOS 0x20000, INVESTIGATING 0x8000000, CUSTOM4
0x10000000, CUSTOM3 0x20000000, CUSTOM2 0x40000000, CUSTOM1 0x80000000`. Three selectors read
`INVESTIGATING` to choose the first-response program over the `_OTHER` one.

**Tasks the port lacks, with arms** (shared registrar ids 0–0x114, Troika 0x115–0x149; `StartTask`
/ `RunTask`): `STORE_LASTPOSITION 0x17` (base `0x10282afd`: `m_vecLastPosition +0x5db8 =
origin; m_angLastAngle +0x5dc4 = angles`), `CLEAR_LASTPOSITION 0x18` (`0x10282b5b`),
`GET_PATH_TO_LASTPOSITION 0x1c` (`0x10285bb0`), `GET_PATH_TO_BESTSOUND 0x20` (`0x10285df8`:
`AI_NavGoal_t{type 4 LOCATION, dest = m_BestSound+0x20, tolerance = [0x1049a160] overriding the
schedule's, flags −1}` → navigator `SetGoal`), `WALK_PATH 0x23` (`0x10286438`), `FACE_IDEAL 0x2b`
(`0x10283cd5` / Troika run `0x102aae43`), `PLAY_SEQUENCE 0x52` (`0x10282dde`/`0x102891c8`),
`FORGET 0x6d` (`0x102829e9`), `GET_PATH_TO_BESTUNKNOWN 0x79` (Troika `0x102a3599`),
`GET_PATH_TO_PATROL_POINT 0x7a` (`0x102a39be`/`0x102ab00f`), `NEXT_PATROL_POINT 0x7d`
(`0x102a3b91`), `FACE_PATROL_INTEREST 0xb3` (`0x102a63bd`/`0x102ab974`),
`DO_PATROL_INTEREST_ACTIVITY 0xb5` (`0x102a64a6`/`0x102aba6c`), `ADD_EVENT_EXPRESSION 0xbe`
(`0x102a4a07`), `SET_PRESERVE_PATH 0xc4` (`0x102a3cfa`), `WALK_RUN_PATH_COMBAT_SOUND 0xd7`
(`0x102a4bd8`: `m_BestSound.type == 1 COMBAT || 0x10 BULLET_IMPACT` → `ACT_RUN` if the model has
it, else `ACT_WALK`; `SetMovementSequence`; clear `MEMORY:INCOVER`), `PLAY_COWER 0xe6`
(`0x102a5125`/`0x102ab83c`), `ALERT_LOOK_AT_BEST_SOUND 0xf9` (`0x102a5515`; shared run
`0x102ab76a`: look at the point, complete when `m_flLookTimer +0x5db4` elapses or the head-turn
virtual reports done, clearing `PLAYING_FACE_ANIM 0x08000000`), `LOOK_AT_PLAYER 0xfb`
(`0x102a5599`), `LOOK_AT_BEST_UNKNOWN 0xfc` (`0x102a55e3`), `UNLOOK_AT 0xfe` (`0x102a56a2`),
`CLEAR_NPC_FLAG 0x101` (`0x102a58ae` — the port's "no program clears a flag" premise is false:
0x60/0x61 clear `NO_UNKNOWN_ATTACK`, 0x62/0x63 clear `LOOKED_AT_UNKNOWN`), `WALK_PATH_HUNT 0x104`
(`0x102a5932`), `FACE_LASTANGLE 0x11d` (`0x102a6fd8`/`0x102ab900`), `PLAY_SOUND 0x11e`
(`0x102a7000`: `g_VSoundTable(0x1073dc28)->PlayNPCSound(this, idx, 2, 1.0, 1.25)` via
`0x101f5950`, start-only), `ALERT_LOOK_AT_UNKNOWN_ATTACKER 0x148` (`0x102a7722`, handle `+0x5b7c`,
`TaskFail(0x21)` when dead).

## Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload (2026-09-08)

**The HitGroup record.** A discipline's `HitGroupList` entry (`0x374` bytes; loader `0x101e0080`)
holds five `0xac`-byte `HitInfo` blocks (instant `+0x08`, `OnEnd +0xb4`, `OnCallback +0x160`,
`OnInterrupt +0x20c`, `OnInterruptSchedule +0x2b8`), `Duration +0x364`, the trait effect `+0x36c`,
and two bytes parsed by `0x101dfa00`: **`+0x370` = key `DoPossession`** (`0x105a2870`), **`+0x371`
= key `DoFrenzy`** (`0x105a2864`), both default 0, copied verbatim by `InheritFrom` (`0x101df340`).
The earlier "make follower vs calm" reading was wrong: `+0x371` is frenzy; `D_CALM` is written only
by `SCHED_TROIKA_CALMED`'s `TASK_SET_NPC_FLAG`. The sole reader is `0x101dfc20` (DevMsg
`"Discipline<%s>: HitGroup: <%s> Hit Triggered on %s (%s)"`): apply the instant `HitInfo`
(`0x101de660`), then on the target's `+0x98` Troika pointer `DoPossession` first, else `DoFrenzy`,
then the trait effect. Chain: cast `0x101e2f50` → per target `0x101e3730` (Affects table picks the
HitGroup) → `0x101e3850` (or `CDisciplineProjectile::vfunc266` `0x101da020` on impact) →
`0x101dfc20`. The HitGroup's `AI_Schedule` is installed by `0x101de660` **before** either arm.

**`DoPossession` arm `0x102c51a0`** (the `SetFollowerBoss` path): `AddMiscFlag(0x800)` when the
enemy or the caster is hated; squad disconnect `0x1026d050`; `0x102b52a0` (`SetEnemy(NULL)`,
`SetTarget(NULL)`, `+0x5d8c &= 0xf7fc7fff`, clear hint); slot 304; `flags2 |= D_POSSESSED |
D_DISCONNECT_SQUAD`; caster a player → `0x10273790(this, "player D_LI 99")`;
**`SetFollowerBoss(ent)` `0x102c4470`** (`"!player"` for a player caster, else the caster's name)
→ `SetFollowerBoss(name)` `0x102c44e0`; **`SetFollowerType("Combat")` `0x102c4640`** → radii from
`Npc_Follower_Info` (`0x102c4680`); ideal state 1; `m_hTargetEnt (+0x5ce4)` and `m_hFriendPlayer
(+0x60ac)` = caster; slot 614; `m_bfNPCFrenziedFlags (+0x5b84) = 0x3b1c`; slot `0x94c` =
`0x102b4cc0` sweeps a 1024×1024×128 box and `SetEnemy`s the nearest hated entity.
**`DoFrenzy` arm `0x102c5310`**: the same first four steps, the hate acquisition, then `flags2 |=
D_INSANE | D_DISCONNECT_SQUAD`, the `D_LI 99` write, ideal state `0xb` (hunt),
`investigate_mode` and `_combat` = 6, `m_hFriendPlayer`, `frenziedFlags = 0x9fbd`; no follower.
Shipped setters (`disciplinetgt_001/002.txt`): `DoFrenzy` — `Dementation_Berserk` and
`Dementation_Bedlam`; `DoPossession` — `Dominate_Possession`. Presence, Animalism, Thaumaturgy,
Dominate 1/2/3/5 set neither.

**The `AI_NPCFlag` payload — a non-task writer of the flag words.** `HitInfo+0xa8` is the key
`"AI_NPCFlag"` (`0x105a26b4`), parsed by `0x101ddfb0` through the same 62-name resolver
`0x1030cbd0` as `TASK_SET_NPC_FLAG` (sign bit = word two). `0x101de6e1` is not a function but the
set arm inside `0x101de660` (`|=` on `+0x14b8`/`+0x14bc` of the target's Troika pointer);
`0x101def10` is the matching clear (`&= ~mask`, plus `RemoveFromComfortList` and a schedule
teardown), reached through the effect-expiry table. The sibling key `"MiscFlag"` → `+0xa4` via
`0x1033cb00`. This corrects the earlier claim that the task vocabulary is the only writer of the
words: a HitGroup sets flags on the target with no task, and `OnScheduleChange`'s masks will
clear them like any other.

**Apply/expiry implementation detail (2026-09-08, spec 0005 requirement 8).** Re-reading the
whole bodies corrects two shorthand descriptions above. `0x101de660` writes `MiscFlag` first
(`AddMiscFlag`, `0x1033c6b0`, plain OR), then the NPC mask, before health and schedule channels.
`0x101def10` clears the original NPC mask with `&= ~mask`, including the word-two routing bit;
it **does not clear MiscFlag**. The UP `Thaumaturgy` HitGroup
`Hit_Supernatural_BloodGuardian` explicitly calls `Forced_BloodShield` a permanent visual effect.
The misc resolver `0x1033cb00` compares all 22 names case insensitively and returns zero for an
unknown name. Loader `0x101ddfb0` ORs an authored misc mask into inherited `+0xa4`, whereas
`AI_NPCFlag +0xa8` replaces its inherited mask. Multiple live effects that write the same NPC bit
do not reference-count it: the first cleanup clears it even when another effect remains.

`AddToComfortList` (`0x10323630`) appends a handle **without deduplication** and zeroes
`m_iComfortingCount`. `RemoveFromComfortList` (`0x10323770`) removes the **first** matching
handle, preserving the rest of the array's order, and zeroes the count even when no match exists.
The original HitInfo's nonzero `AddToComfort` byte causes one removal during cleanup.
`Event_Killed` (`0x1032b9b0`) and `UpdateOnRemove` (`0x10327790`) each independently call this
same removal after their discipline-visual/presence cleanup; duplicates are not collapsed there.

The `AI_Schedule` cleanup is a **task completion request**, not a wholesale schedule clear:
after flag and comfort cleanup, an originally nonempty schedule channel checks the current
`D_DISCONNECT_SQUAD` bit and reconnects (`0x10009601`: decrement `+0x5bb0`, rejoin the squad's
memory at zero, `flags2 &= 0x7f7fffff`). It then reads the current schedule, and only local IDs
`0xe1`/`0xe3` call `TaskComplete(false)` (`0x10273e80`): if condition `0x5c` is clear, write task
status `+0x5c44 = 4`. It does not invoke `OnScheduleChange`. `0x101dfe80` removes the trait
effect, runs this original-HitInfo cleanup, then directly applies `OnInterrupt +0x20c` or
`OnEnd +0xb4`; these callbacks are HitInfo calls and create no new HitGroup timer. Conversely,
`0x102a0940`'s `ACTIVITY_COPY_PROP_CLEAN` arm interrupts targeted effects before the unconditional
flag tail. `DAT_10739a64` suppresses that interruption while HitInfo installs its own schedule.

The port stores resolved cleanup masks and channel-presence bits on every flag/comfort/schedule
effect, including effects without trait modifiers, and persists them with the shared discipline
block. The common misc word and comforting count persist on both player and NPC; the ordered
comfort registry persists on the map with handle rebasing. Expiry, explicit clear, interruptions,
and exhausted-effect reconciliation use the same cleanup. Possession/frenzy and shared squad
memory remain the separate requirements 16/17; no substitute state is inferred from a flag.
The original caster handle also rebases when an NPC's discipline block loads: the direct
`OnEnd`/`OnInterrupt` HitInfo dispatch from `0x101dfe80` still receives that caster after restore.

**The two `TriggerAISound` producers (2026-09-08, requirement-6 integration).** The parser's
record byte `+0x35` (`0x101e06a0`) controls both. Source activation `0x101e3560`, called once by
`0x101e2f50` before its target loop, checks the same record's active status, adds a 0.1-second
source status if absent, sets misc `Fired_Gun 0x200000`, and inserts **COMBAT `1`** at the caster,
owned by the caster, for **0.2 seconds** using `DAT_1072bc40`/`DAT_1072bcb0` (the gunshot row).
The HitGroup prelude `0x101dfc20`, after target `AddDiscFlag` and before `0x101de660`, inserts
**BULLET_IMPACT `0x10`** at and owned by the target, also for **0.2 seconds**, using
`DAT_1072bc58`/`DAT_1072bcb6` (`NPC_DISCIPLINE_ALERT`). These are two real insertions; neither
replaces the other. `0x10` is not DANGER (`0x8`). Direct OnEnd/OnInterrupt HitInfo callbacks do
not repeat the HitGroup prelude. The source status guard is record-wide, so a live targeted status
for the same record also suppresses repeated source activation.

**Derived condition tables, tutorial classes.** Registrar helper `0x102ea130` =
`CAI_ClassScheduleIdSpace::AddSymbol` (63 callers); derived local ids start at `0x78`.
`CNPC_VVampire` (`0x103c4ab0`), `CNPC_VHuman` (`0x10384230`), `CNPC_VHumanCombatant`
(`0x10386cb0`), `CNPC_VPedestrian` (`0x103a1fd0`) register **no** condition above `0x76`;
`CNPC_VRat` has no registrar and inherits `CNPC_VScurrying`'s (`0x103abd70`): **`0x78
COND_VSCURRYING_PLAYER_TOOCLOSE`** (its global id is load-order assigned). The 17 classes that do
add conditions: Crow, VAndreiBlood, VBach, VBatSwarm, VChangBros, VDog, VFrenzyShadow,
VGhoulCroucher, VMingXiao, VMingXiaoTentacle, VSabbatLeader, VScurrying, VSheriffMan,
VSheriffSwarm, VTzimisce, VWerewolf, VZombie.

**`stay_entrenched`** is a `CAI_BaseNPCTroika` datamap keyfield: `m_bStayEntrenched +0x6435`,
`FIELD_BOOLEAN`, builder `0x1028cd70`; plus the input `StayEntrenched` → `0x102c2bd0` (writes 0
on any non-boolean variant). Readers: `0x102ae920` (combat state only: an entrenched NPC that
passes slot 592 and gets a schedule from `0x102b7690` returns it and skips the ordinary tail) and
`NPCThink` `0x10292de0`. UNRECOVERED: `0x102b7690`'s arms, slot 592, the `NPCThink` arm.
UNRECOVERED elsewhere in this section: the `m_bfNPCFrenziedFlags` vocabulary (`0x3008` from
`SetFollowerBoss`, `0x3b1c`/`0x9fbd` from the arms, `& 0x80` in `0x102ae920`), slots 304 and 614 (misc-flag bit 11 is `Was_Hateful`, see below).

## Sense and investigate leftovers, closed (2026-09-08)

Method note: the field ledger and name-based grep miss accesses the decompiler renders as a
dword index (`param_1[0x16e1]` for `+0x5b84`, `[0x188f]` for `+0x623c`); cross-check with a grep
on `0x<offset/4>]`.

- **`m_eAlertLevel` (+0x63f4)** is `FIELD_INTEGER`, `FTYPEDESC_SAVE` (flags `0x2`; `0x4` = KEY,
  calibrated on `m_iIsOblivious`/`m_bNoAlertState`). Writers: the slot-420 NPCInit
  (`0x1029a0b0`, VCamera `0x103692c0`) → 0, `FUN_102b8980` → 1/2/3, `FUN_102b8a60` and
  `FUN_102b9060` → 3. Slot 420 is dispatched only from `Spawn` bodies (and `CNPC_VPedestrian`'s
  level-reset respawn). **Nothing resets it: the turn → step → walk ladder runs once per life and
  survives saves**; every later `HEAR_PLAYER`/`HEAR_DANGER` goes straight to `0x51`/`0x52`.
- **`debug_allow_npc_hunting`** (object `0x10924478`, ctor `0x1028c840`, default **`"0"`**, flags
  0, help "Set this to 1 to allow NPCs to do their 'scouring the area' hunting state") is the
  ConVar behind `DAT_1092447c`, read only by `CNPC_VHuman::SelectIdealState` (`0x103851e0`).
  **In retail no `CNPC_VHuman` enters the HUNT state from `SelectIdealState`**; the case-0xb ladder
  and `SCHED_TROIKA_HUNT_*` are reachable only by `aiscripted_schedule`/`ChangeSchedule` or with
  the cvar flipped. `debug_allow_move_facing` (`0x10924f70`, ctor `0x1028c720`, default `"1"`,
  "If this is on, NPCs will move facing the NPC when they run for cover") is `DAT_10924f74`;
  readers `0x10278cb0/d20/d90` (facing forwarders), `NPCThink`, `0x102b93c0`,
  `CNPC_VAndreiBlood::PreTranslate_Human`.
- **`m_bfNPCFrenziedFlags` (+0x5b84)**: `FIELD_INTEGER`, saved, no external name, **no name
  table anywhere** (only `NPCFlag:` `0x1030cbd0` and `MiscFlag:` `0x1030d850` parsers exist) —
  UNRECOVERED authored names. Writers: base init `0x10273390` → 0 (VCamera/VNewscaster/
  VPlayerController likewise); `CNPC_VFrenzyShadow::vfunc420` → `0x5ddf`; `CNPC_VScurrying::
  Spawn` `0x103ac430` → `|= 0x10000`; `VTzimisceHeadClaw`/`VTzimisceRunner` Spawn → `|= 0x80`;
  `SetFollowerBoss` `0x102c44e0` → `|= 0x3008`; `DoPossession` arm → `= 0x3b1c`; `DoFrenzy` arm →
  `= 0x9fbd`. Bit meanings from their readers: `0x8` always-PVS/LOS bookkeeping (`0x10290b60`,
  `0x10291230`, `SetPlayerLOS`); **`0x10` "does not witness"** — first gate of the player-law
  sweep `0x1028efc0` and a reject rung of slot 587; `0x80` suppresses the unknown-attacker
  response (`0x102ae920` ×3); `0x800` the frenzy friend-player reject in `QuerySeeEntity`/
  `QueryHearSound`; `0x2000` ally banter permission (one reader `0x102b7cf0`: `SEE_ENEMY`,
  cooldown `+0x65a4`, `!D_INSANE`, `!stay_entrenched` → `0x8c`/`0x8d`, re-arm 10–20 s);
  **`0x10000` no-flinch** — set only by `CNPC_VScurrying::Spawn`, read only by `FUN_102b9060`.
  Further readers: `0x2` (`FUN_102b5900`, `0x10385d70`), `0x400` (`FUN_102b2570`), `0x4000`
  (`CWeaponMelee::RequestActivity` `0x103e9e00`). The bits have no name table anywhere in the
  image (settled; the `Frenzied` string at `0x1061ab20` belongs to an unreferenced 10-entry mood
  table at `0x10619f24`: `Diablerist_, Kindred_, Afraid, Angry, Calm, Confused, Frenzied,
  Innocent, Obfuscated, Suspicious`, consumer unrecovered).
- **The misc-flag name table** (`0x10619ec8`, 22 pointers; name→index `0x1030d390` for the
  `MiscFlag:` parser `0x1030d850`, name→mask `0x1033cb00`, mask→name `0x1033cb50`), bit order:
  `Unconscious, D_Targeted, Allow_Fort_Soak, Allow_Thaum_Exp, Gave_Fighting_Wpns,
  Allow_Discipline_Fx, Update_Auto_Leveling, Picked_Up_Item, Obf_Bumped_Object,
  Has_Special_Dmg_Mod, Has_Special_Hit_Mod, **Was_Hateful (0x800)**, Double_Humanity_Mods,
  Feed_Bonus_Opp_Gender, Feed_Bonus_Tramps, Increased_Rat_Feed, Cannot_Rat_Feed,
  Forced_BloodShield, No_Resist_Feeding, No_Ragdoll_Death, Gain_Stealth_Atk_Bonus, Fired_Gun`.
  So the possession/frenzy arms' `AddMiscFlag(0x800)` mark the target `Was_Hateful`, read by
  `0x1033d580` ("counts as an ordinary killable human").
- **`CAI_InterestingPlace+0x57d`**: written only by the constructor `0x102d99d0` (= 0); never set
  to 1. The predicate arm is dead; only `m_bEnabled` disables a node. `+0x57e` likewise.
- **`COND_SHOULD_INTERACT`**: `FUN_102a0cb0` has zero references in the image (full `E8`/`E9`/
  absolute-dword scan; only its own uncalled link thunk `0x10013039`). `m_flNextPedInteractTime`
  is zeroed at spawn and read by nobody; `UpdatePedestrianInfo` clears `0x10` every navigating
  think. **`SCHED_TROIKA_INTERACT` (0x106) is unreachable in retail**, like `LOITER` (0x105).
- **`m_flNextInvestigateSoundTime` (+0x623c)**, `FIELD_TIME`, saved. Writers: the slot-420 init
  → 0; `SelectSchedule` at `0x102afb8d` (hunt) and `0x102b0349` (the `INVESTIGATE_SOUND`
  interrupt arm that also clears `COWER_PATH 0x200`, commits the best sound and returns `0x48`),
  `FUN_102b9060`, `CNPC_VPedestrian::vfunc438` (`0x103a29f0`), `CNPC_VTzimisce::vfunc438`
  (`0x103bb7c0`) — all `curtime + 2.0`; `FUN_102b8d20` → `curtime + 20.0`. One reader:
  the sound sweep `0x102b1cd0`, gating its whole body. **`DAT_10452dc4 = 2.0f`,
  `DAT_1044eb0c = 20.0f`**; also `[0x1049a160] = −1.0f` (the best-sound path tolerance
  override), `[0x1044bef8] = 0.25f`.
- **`+0x60dc` is `m_InvestigateSound`** (`FIELD_EMBEDDED`, saved), the `CSound` immediately after
  `m_BestSound` (`+0x60b0 + 0x2c`); `+0x60e0` its `m_iType`. Written by `CommitBestSound` and by
  `FUN_102b9060`'s `HEAR_WORLD` arm; read by `ShouldInvestigate` `0x102b3270` (the type test for
  1/0x10), `GetSchedule` `0x102ae920`, `0x102993c0`, `0x10299700`. Two copies because
  `m_BestSound` is the volatile task-facing record `GetBestSound` hands out and re-commits, while
  `m_InvestigateSound` is the sticky decision-layer copy that outlives it across saves.
- **`m_bfAINPCFlags` bit 21 = `LOOKED_AT_UNKNOWN` (0x200000), bit 25 = `FINISHED_IGNORE_UNKNOWN`
  (0x2000000)**, both set/cleared only by `TASK_SET/CLEAR_NPC_FLAG` from `0x59`/`0x62`/`0x63` and
  `0x60`/`0x61`. Bit 21 is read only by `FUN_102b8a60`; **bit 25 has zero readers** — bookkeeping
  like `MADE_OBLIVIOUS`, cleared by `OnScheduleChange`'s `&= 0xd7ffffff`.
- **`+0x6081`**: no datamap entry, not saved. Writer `0x102b4760` (`= 1` when `d > 0.7 ×
  radius`, `DAT_10457f54 = 0.7f`); readers `0x102b3e00` and the two slot-472 overrides
  `CNPC_VCop::vfunc472` (`0x10371ae0`), `CNPC_VHunter::vfunc472` (`0x103887d0`).
- **Slots.** 434 = `PrescheduleThink()` (`0x101a6560`, empty; proved by `CNPC_VSabbatLeader::
  PrescheduleThink` `0x103a7650`; called from `RunAI`; Troika does not override it). 436 =
  `OnStartSchedule(int)` (`0x101a6580`, empty; `MaintainSchedule` calls it on `m_iCurTask == 0`
  with the local id). 445 = `StartTaskOverlay()` (`0x10288710`, the move-and-shoot overlay;
  twin `RunTaskOverlay` `0x10289c90`, non-virtual). 447 = `GetLocalScheduleId(int)`
  (`0x101a6620`, `GetClassScheduleIdSpace()->ScheduleGlobalToLocal`). None of the four is
  overridden in the Troika hierarchy. `MaintainSchedule` = `0x102817c0` (VProf
  `"CAI_BaseNPC::RunAI::MaintainSchedule"`, `StartTask` slot 442, `RunTask` slot 444).
  **Slot 587** (`0x1028ef20`; overrides on `CAI_BaseHumanoid`, `VAnimal`, `VMingXiao`,
  `CNPCMaker`, `VNewscaster`, `VPlaceholder`): `IsKindred()` → false; `m_iDialog != 0` → false;
  `m_iIsOblivious > 0` → false; `frenzied & 0x10` → false; `IsBusyWithDiscipline()` → false;
  else `(pl_supernatural_flee < 3) || (pl_supernatural_attack < 3)`. Dispatched from
  `CAI_BaseHumanoid::vfunc333` and the Nosferatu player think `0x10181be0` (template
  `Player_Nosferatu`, 512-unit sphere, nearest passer → SendProp **`m_idxNosferatuRadarNPC`**
  at player `+0x1ED0`, consumed by `hud/Context_Icons/Nosferatu_Warning`). Its authored name is
  UNRECOVERED; `CanWitnessSupernatural()` stays the project name.

## The think cadence, decoded (2026-09-08)

All eight stamps are `CAI_BaseNPCTroika`-only: `m_flNextUpdateThink +0x6244`, `NextNormal
+0x6248`, `NextMove +0x624c`, `NextAI +0x6250`, and `m_flLast{Update,Normal,Move,AI}Think`
`+0x6254..+0x6260`. Inputs: `m_flPlayerDist +0x6264`, `m_hClosestPlayer +0x628c`,
`m_bInPlayerPVS +0x6278`, `m_bInPlayerLOS +0x6279`, `m_flLastInPlayerLOS +0x6280`,
`m_flNextPlayerLOS +0x6284`. Names from VProf literals: `CalcNextUpdateThink 0x10290720`,
`CalcNextNormalThink 0x10290b60`, `CalcNextMoveThink 0x10290fc0`, `CalcNextAIThink 0x10291230`,
`SetClosestPlayer 0x10293a80`, `SetPlayerLOS 0x10291610`, `UpdateCharacter 0x10298070` (slot
312). `0x1029bd40` is the `ai` debug distance overlay, not a writer.

**Due test** `IsThinkDue(stamp)` `0x10290660`: `(stamp − curtime) ≤ frametime` (equality is due;
`FCOMP` + `TEST AH,0x41`). The move gate `0x102906e0` is `return true`.

**Writers.** `NPCInit` `0x1029a0b0` sets all eight to `curtime` and seeds `PVS = LOS = 1`
(a fresh NPC is due on every clock). The four `Calc*`. **`TaskFail` `0x1029adb0`** sets the four
`Next` stamps to `curtime` (not the `Last` ones): a failure forces a full think next frame.
**`0x102c23f0` = slot 614 `ResetThinkTimers()`** (all four `Next` + `m_flNextThink` := curtime),
dispatched virtually by `FeedInterrupt` before the trance and by the possession arm — the effect
takes hold on the same frame. Subclass writers of `+0x6250`:
`CNPC_VCamera` `0x10369120`/`0x103692c0`, `CNPC_VNewscaster::vfunc431` `0x103a05b0`
(UNRECOVERED arithmetic).

**The interval laws** (constants from the DLL; epilogue for each: `*out = Next − Last; Last =
Next; Next = max(Next, curtime) + i`):
- Update: no closest player → `0.03`; else `v = (m_flPlayerDist − 512) / 704`, `v > 8 → 8 +
  RandomFloat(0, 0.8)`, `v < 0.03 → 0.03`, `v == 8 → v + RandomFloat(0, 0.8)`; then `!PVS → ×10
  cap 16`, `!LOS → ×5 cap 12`; `ShouldThinkFrequently()` → `0.03`.
- Normal: `ShouldThinkFrequently()` → `0.01`; `frenzied & 0x8` or `flags2 & 0x4
  SCHEDULE_CHANGED` or `LOS` → `0.1` (no PVS scaling); else no player → `0.1`, else `v =
  (dist − 2048) × 3/4096`, `v > 3 → 3 + RandomFloat(0, 0.3)`, `v < 0.1 → 0.1`; `!PVS → ×10 cap 16`,
  `!LOS → ×3 cap 6`.
- AI: `frenzied & 0x8` or `SCHEDULE_CHANGED` or `LOS` or no player → `0.1`; else `v = (dist −
  512) / 896`, `v > 4 → 4 + RandomFloat(0, 0.4)`, `v < 0.1 → 0.1`. No PVS scaling, no
  `ShouldThinkFrequently`.
- Move: `curtime + 0.001`, always.
`ShouldThinkFrequently()` `0x102c2430` = `IsInDialog()` (`0x102c1170`: `m_bIsTalking +0x64c0`,
`m_szDialogQue[0] +0x64ec`, `m_hDialogPartner`, handle `+0x6554`) ∥ `m_scriptState +0x5d70 ∈
{4,5,6}` ∥ (`curtime > m_flTeleportMoveTimer +0x65dc ? m_bForceFrequentThink +0x63f0 : true`).
Note the asymmetry: out of LOS lengthens the update think, while being in LOS pins normal and AI
to 0.1 s. `m_NPCState` enters none of the laws; state changes the rate only through LOS,
`SCHEDULE_CHANGED` (set by `SetSchedule`, cleared at the top of every think), `TaskFail`, and
dialogue/script. There are no `ai_think_*` cadence cvars.

**`SetClosestPlayer`**: nearest player by 3-D distance, seed `20000.0`, → `m_hClosestPlayer`,
`m_flPlayerDist`. **`SetPlayerLOS`**: forced `PVS = LOS = 1` when `m_bfNPCStateFlags & 0x8`
(UNRECOVERED name), `frenzied & 0x8`, or no player; else at most every 2.0 s: `PVS = engine
PVS test 0x101d1a90`; in PVS and `dist < 512` → `LOS = 1` without a trace; else eye-to-eye trace
mask `0x4091`; then `if (!LOS && PVS && curtime − m_flLastInPlayerLOS < 8.0) LOS = 1`.

**One `NPCThink` (`0x10292de0`), in order**: `NPCThinkDebugPre`; `flags2 &= ~SCHEDULE_CHANGED`;
`if (m_bDisableAI +0x6080) return`; `normalDue = IsThinkDue(NextNormal)` — the master gate; when
due: `SetClosestPlayer`; enemy distance/height/last-known (`+0x6268/+0x626c/+0x6270`, `5000.0`
when none) and the `MOVE_FACE_ENEMY` facing under `debug_allow_move_facing`; `AngleVectors` →
`+0x6290/+0x629c`; `SetPlayerLOS`; `CacheInterruptConditions`; `AutoMovement` under
`ANIM_MOVEMENT 0x4000`; hint upkeep (`m_flOccludedDelay` from cover/normal; invalid hint, or
`!stay_entrenched && m_hHintCoverObject == enemy && (0x2e || 0x48)` → `ClearHintNode(5.0)` +
`TaskFail(0x29)`); shoot-target override; a 1 % `"Scream_Death"` under `frenzied & 0x8000`;
`updateDue`; `ResolveStandingOnHead`; fall-to-ground unless `DONT_FALL_TO_GROUND`; `move_yaw`
pose; `DISAPPEAR 0x20000000` removal when out of the player's PVS or unseen; the AI console gate
`0x1026c3d0` (refusal → `m_flNextThink = curtime + 0.1`, return); **`bReduced =
!IsThinkDue(NextAI)`**; `RunAlternateAI(bReduced)` (`0x1028fd80`) and, if it returns 0,
`RunAI(bReduced)` (slot 432); `PostRun` → `PerformMovement(interval)`; `CalcNextMoveThink`;
`CalcNextAIThink`. Then (whether or not normal was due): if `updateDue` → `UpdateCharacter`
(slot 312) and `FinishTalking` when `m_bIsTalking && !IsInDialog()`; `CalcNextUpdateThink`;
`CalcNextNormalThink`; **`m_flNextThink = min(NextUpdate, NextNormal)`**; `m_bJumping +0x6498` →
`curtime + 0.01`; the debug overlay.

**`RunAI(bReduced)` `0x1026f110`**: `m_bConditionsGathered = 0`; `GatherConditions` (slot 433)
only if `!bReduced && m_hDialogPartner invalid`; the head probe `0x1026ab50`; `PrescheduleThink`
(slot 434); `MaintainSchedule(this, bReduced)` — bound 10, or 1 when reduced (`0x1028190e`);
`if (!bReduced)` clear `LIGHT_DAMAGE 0x4c`, `HEAVY_DAMAGE 0x4d`, `WAS_BUMPED 0x38`. The base
`CAI_BaseNPC::NPCThink` `0x1026ca80` is a flat `curtime + 0.1` with `RunAI(0)`; no Troika NPC
runs it.

## Squads, decoded (2026-09-08)

**`CAI_Squad`** (0x78 bytes; ctor `0x103164c0`, link `0x103165f0`, dtor `0x10316440`; list head
`g_pSquadList 0x10936c68`): `+0x00 next`, `+0x04 name`, **`+0x08 m_memory` — an embedded
`AI_Enemies`**, `+0x1c EHANDLE m_hMembers[16]`, `+0x5c m_nNumMembers`, `+0x60
m_flSquadSoundWaitTime`, `+0x64 m_squadSlotsUsed` (32-bit `CVarBitVec`), `+0x70 m_hFocusEntity`,
`+0x74 m_flFocusExpireTimer`. `datamap_CAI_Squad` (`0x10315610`) holds the last five only; the
member array and name are not saved. NPC side: `m_pSquad +0x5da4` (not in the datamap),
`m_iSquadDisconnected +0x5bb0` (datamap, an int refcount like `m_iIsOblivious`), `m_SquadName
+0x5da8` (key **`squadname`**), `m_pEnemies +0x5d88`.

**The whole coupling is the shared memory.** `CAI_BaseNPC::GetEnemies()` (slot 541,
`0x10273e10`) = `m_iSquadDisconnected < 1 ? m_pEnemies : g_DisconnectedEnemies` (the global
`AI_Enemies` at `0x109203f0`). Joining (`InitSquad` `0x10273d30`: `m_pSquad == NULL &&
CapabilitiesGet() & bits_CAP_SQUAD 0x4000000`, then `FindCreateSquad(name)` `0x10315800` and
`SetSquadEnemies` slot 542 `0x10273dd0` — delete the private memory, point `m_pEnemies` at
`squad+8`; or the `SQUAD` tweak param → `CAI_BaseNPCTroika::SetSquad` `0x1029a930`) swaps the
NPC's enemy memory for the squad's. `FindCreateSquad`: `strcmpi` walk; a 17th recruit DevMsgs
`"Squad %s is too big"` and overwrites the 16th. `GetMember(i)` `0x103160c0` returns NULL for
every index when member 0 is disconnected. `RemoveFromSquad` `0x103158f0` (callers:
`Event_Killed` `0x10265ad0`, `0x1027ca30`, `SetSquad`, `CNPC_VCamera`) compacts the array and
calls slot 578 (UNRECOVERED name) on each survivor. Squads are freed only by `DeleteAllSquads`
`0x103162d0` at level shutdown. `SetSquadFocus` `0x10316660` / `GetSquadFocus` `0x103166b0`
(15 s expiry). No `GetLeader` exists; `CNPC_VChangBros` and `logic_squad_condition`
(`0x10135930`, `squad_name`, `SquadSeesPlayer`) walk `NumMembers`/`GetMember`.
**`LeaveSquad` `0x10316700` is `RET 4` — an empty stub.**

**Disconnect / reconnect.** `DisconnectFromSquad` `0x1026d050`: if not already disconnected and in
a squad, `LeaveSquad` (no-op) then **`g_DisconnectedEnemies->ClearMemory()`** (`0x102dfc10`) —
one global scratch memory shared by every disconnected NPC, wiped on each disconnect; then
`++m_iSquadDisconnected`. `ReconnectToSquad` `0x1026d0c0`: `--count`; at 0 → `squad->
AddSelfToSquadMemory(this)` `0x10316720`; `flags2 &= ~D_DISCONNECT_SQUAD`.
`TASK_DISCONNECT_FROM_SQUAD` is task `0xf5` (the only task of `SCHED_TROIKA_D_BRAINWIPE`);
other callers: `TASK_MAKE_OBLIVIOUS`'s `0x1026d130`, the possession and frenzy arms.

**Conditions.** `COND_SQUAD_SEE_ENEMY 0x31` producer `0x102b2730` (from Troika
`GatherConditions`): `m_iSquadDisconnected < 1 && m_pSquad && enemy && AI_Enemies::LastTimeSeen
(GetEnemies(), enemy) + 0.2 ≥ curtime` → set 0x31, and `COND_SQUAD_LOS_ENEMY 0x32` when
`HAVE_ENEMY_LOS 0x4a` — "someone sharing my memory saw him in the last 0.2 s", no broadcast.
`TASK_SQUAD_NEW_ENEMY` = task `0x138` → `CAI_Squad::SquadNewEnemy` `0x103161a0`: for each
member not disconnected, not already on this enemy, without `SEE_ENEMY`, still sharing the
memory → `SetEnemy`, `m_flLastAttackTime +0x5d9c = 0`, `NEW_ENEMY`. Other callers `0x1026f590`
(slot 460: idle/alert, `NEW_ENEMY`, enemy set → broadcast, or set `flags2 |= SQUAD_NEW_ENEMY` on
the entity at `CAI_BaseNPC+0x98`, UNRECOVERED), `0x102ae920`, `0x10374e50`, `0x10397380`.
**`SQUAD_NEW_ENEMY` (flags2 bit 13) and `IGNORE_SQUAD_SEE_ENEMY` (bit 6) have zero readers in
the image** — write-only bookkeeping; the port's clear of `SquadSeeEnemy` under
`IGNORE_SQUAD_SEE_ENEMY` is a divergence to remove.

**Saving** (`0x1030bfd0`): squad count, each squad's five datamap fields, then the private
`CAI_Memory` of every NPC that is disconnected or squadless. Membership is rebuilt on restore
from each NPC's `squadname` through `InitSquad`; order is not preserved; `m_pSquad` itself is
never serialized.

**Strategy slots ship dead.** The `squadslot` namespace (`0x10920484`) receives exactly two
names, `SQUAD_SLOT_ATTACK1/2` (`0x10316e80`, ids `0x3b9aca00/01`), with no consumer; every class
registers zero squadslots; there is no `OccupyStrategySlot`/`VacateStrategySlot`;
`m_squadSlotsUsed` is touched only by ctor, dtor and save. Do not build them.

UNRECOVERED: `CAI_BaseNPC+0x98`'s entity; slot 578; slot 168 (`+0x2a0`, the `GetEnemy` variant
the producer uses); `m_iMySquadSlot`'s offset (zero readers); `m_bfNPCStateFlags` bit 3;
`CAI_Squad` `0x10316890/ab0/bc0/ec0/fa0/fd0` (memory-forwarding wrappers).

## The flee state and the cower, disoriented and lost programs (2026-09-08)

Blob census: 691 blobs; 62 carry bare names without `SCHED_` (the base table's `COWER`,
`STANDOFF`, `DIE`…). The registration pair comes in three codegens (`mov [esp+0x10/+0x14]`,
other displacements, and `push id; push name`); the nearest preceding pair self-verifies on 617
of 627 named blobs.

| id | name | blob |
|---|---|---|
| 0x70 / 0x71 | SCHED_TROIKA_FLEE_AND_COWER_TURN_TO_PLAYER / _NEAR | 0x105fa230 / 0x105fa0a0 |
| 0x72 | SCHED_TROIKA_FLEE_AND_COWER_SCREAM | 0x105f9f60 |
| 0x73 | SCHED_TROIKA_FLEE_AND_COWER | 0x105f9d28 |
| 0x74 / 0x75 | _STALL / _STALL_FAILED | 0x105f9b28 / 0x105f9978 |
| 0x76 | _NO_ENEMY | 0x105f9740 |
| 0x77 / 0x78 | SCHED_TROIKA_COWER / _HINT | 0x105f9598 / 0x105f9410 |
| 0x109 / 0x10a / 0x10b | SCHED_TROIKA_COWER_SIMPLE / _HINT / _NOSEE | 0x105e5788 / 0x105e5590 / 0x105e5398 |
| 0x12d / 0x12e | SCHED_TROIKA_DISORIENTED / SCHED_TROIKA_LOST | 0x105dfe28 / 0x105dfca0 |

Neighbours: `0x6f FLEE_AND_DIE`, `0x107 FLEE`, `0x108 FLEE_RANDOM`, `0x12f COMFORT`, `0x130
CALMED`, `0x132 LAUGHING`. The base-class `COWER` (`0x10604f98`: `STOP_MOVING; PLAY_SEQUENCE
ACT_COWER`) is Source's `SCHED_COWER`; its base id is UNRECOVERED (compiled-in enum).

**Programs.** `0x12d DISORIENTED`: `SET_NPC_FLAG DONT_INVESTIGATE; SET_NPC_FLAG NO_DIALOG;
SET_FAIL_SCHEDULE Idle_Stand; SET_PRESERVE_PATH 0; STOP_MOVING; PLAY_SEQUENCE ACT_DISORIENTED;
WAIT_PVS`; no interrupts; `DELAY_INTERRUPTS`. `0x12e LOST` identical with `ACT_LOST`. Their
effective mask is `{NPC_FREEZE}` alone (`DONT_INVESTIGATE` suppresses the overlay) and
`WAIT_PVS` holds them until the body enters the player's PVS.
`0x109 COWER_SIMPLE`: `CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_NPC_FLAG COWERING; SET_NPC_FLAG
ONE_HIT_KILL; PLAY_COWER ACT_COWER_INTO; SET_COWER ACT_COWER; WAIT 10; WAIT_RANDOM 20;
SUGGEST_STATE STATE:IDLE`; interrupts `SEE_ENEMY SEE_FEAR SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE
REPEATED_DAMAGE INVESTIGATE_SOUND` (`SEE_FEAR` twice, verbatim). `0x10a`/`0x10b`: same tasks,
empty interrupts. `0x77 COWER`: `SET_FAIL_SCHEDULE 0x109; FLIP_NEXT_IDEAL_YAW 1; FACE_ENEMY;
FLIP_NEXT_IDEAL_YAW 0; SET_SCHEDULE 0x109` (turn the back to the enemy); interrupts damage +
`DETECTED_ATTACK`. `0x78 COWER_HINT`: `SET_FAIL_SCHEDULE 0x10a; FACE_HINTNODE; SET_SCHEDULE
0x10a`. `0x73 FLEE_AND_COWER`: `SET_NPC_FLAG IN_FLEE_SCHED; SET_FAIL_SCHEDULE 0x74;
SET_TOLERANCE_DISTANCE 12; GET_PATH_TO_COWER_NODE; RUN_PATH_FLEE; WAIT_FOR_MOVEMENT;
SET_PRESERVE_PATH 0; CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_SCHEDULE 0x77`; interrupt
`COVER_FAILURE`. `0x74 _STALL`: fail `0x10b`, `GET_PATH_TO_RANDOM_NODE 1024`. `0x76 _NO_ENEMY`:
`GET_PATH_TO_COWER_NODE_SAVE_POS`, fail `0x74`. `0x75 _STALL_FAILED`: `SET_NPC_FLAG IN_FLEE_SCHED;
SET_ACTIVITY ACT_IDLE; WAIT 2; WAIT_RANDOM 1; CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_SCHEDULE 0x77`.
`0x72 _SCREAM`: `SET_NPC_FLAG IN_FLEE_SCHED; STOP_MOVING; WAIT_RANDOM 0.2; SET_PRESERVE_PATH 1;
SET_SCHEDULE 0x73`. `0x70/0x71 _TURN_TO_PLAYER(_NEAR)`: `SET_NPC_FLAG IN_FLEE_SCHED;
PAUSE_MOVING; LOOK_AT_PLAYER 0.2 (0.1); SET_PRESERVE_PATH 1; SET_SCHEDULE 0x73`.

**Selection.** The flee state is `m_NPCState == 8`, entered only in `CAI_BaseNPCTroika::
SelectIdealState` (`0x102ad660`) from idle and alert on `COND_SUPERNATURAL_FLEE_LEVEL 0x21` or
`COND_CRIMINAL_FLEE_LEVEL 0x1f` (setting `flags1 |= 0x100 INITIAL_FLEE`); `case 8` returns 8 —
**terminal**; `SetState` writes `m_bfNPCStateFlags = 0x85` for it. `SelectSchedule` case 8, in
order: `COVER_FAILURE 0x39` → `0x73`; none of `SEE_ENEMY, SEE_FEAR, LIGHT/HEAVY/REPEATED_DAMAGE,
0x21, 0x1f` → (`DETECTED_ATTACK` → `0x56`; `INVESTIGATE_SOUND` → re-arm `+0x623c`, clear
`INITIAL_FLEE`, `CommitBestSound`, `0x48 SCHED_VTROIKA_TURN_TO_SOUND`; else `0x77 COWER`);
`INITIAL_FLEE` not set → `0x73` (every pass after the first); clear it, `m_flNextFleeSoundTime =
curtime + RandomFloat(10, 20)`, the flee vocalisation (slot `+0x7c8`); damage → `0x72`; no law
level and `SEE_FEAR` → slot `0x950`(`m_hLastSeenFearEnt`), `0x72`; supernatural branch (tested
before criminal): offender valid → slot `0x950`; closest player ≠ offender → offender valid ?
`0x73` : (`m_vSavePosition = m_vecPLSupernaturalLocation`, `0x76`); `MakeAISound(type 8, player
eye, DAT_1072bc88, 10.0, DAT_1072bcc2)`; `ReportSupernaturalAct` (`0x1017f4a0`) unless
`m_bPLSupernaturalActFleeOnly` (then `0x1017fd60` only with `SEE_PLAYER`); `m_flPlayerDist <
DAT_10483aac && RandomInt(0,99) < 80` → `0x71` else `0x70`; criminal branch mirrors it with
`m_hCriminalOffender`, `ReportCriminalAct` (`0x1017f2a0`), `m_vecPLCriminalLocation`.
`TranslateSchedule` (`0x102b12f0`): `0x77` → `0x78` when `m_pHintNode` is type `0x2774`; `1`/`0x6b`
→ `0x132 LAUGHING` under `D_MILDLY_CRAZY 0x80000`. `GetSchedule` (`0x102ae920`) idle chain:
`KNOCKBACK 0x28` → `0x14c`; `COMFORT 0x27` → `0x12f`; `D_CALM 0x10000` → `0x130`; `D_FOLLOW
0x100000` → `0x131`; `D_POSSESSED 0x40000` → `0x131`; dialogue partner → `0x6a`. `D_AFRAID` is not
a flag but `SCHED_TROIKA_D_AFRAID` (`0x105db1f8`), a discipline-installed flee leg ending in
`0x77`. **Nothing selects `0x12d` or `0x12e`**: `DISORIENTED` is the terminal `SET_SCHEDULE` of
16 programs (`D_MESMERIZE`, `D_PURGE`, `D_BLOODSHOT_BOSS`, `D_HALLUCINATION`, `D_HYSTERIA`,
`D_TRANCE`, `D_BLOODSUCKERS_COMMUNION`, `LAUGHING`, `DO_BLOODBOIL/VOMIT/THEFTOFVITAE/BLOODYEYE/
MADNESS/HAUNTING/SLEEP/MESMERIZE_ACTIVITY`) and of five discipline `HitInfo` `AI_Schedule`
keys in retail `pack101.vpk`; **`LOST` has no producer anywhere in the shipped game.** `FLEE`/
`FLEE_RANDOM` are likewise never returned by a selector. `CNPC_VHuman`/`CNPC_VPedestrian`
selectors do not touch the family.

**Activities and the feed.** `ACT_DISPOSITION_MESMERIZED 0x104e`, `ACT_DISORIENTED 0x1068`,
`ACT_LOST 0x1069`, `ACT_COWER_INTO 0x1097`, `ACT_COWER 0x1098`, `ACT_COWER2 0x109b`,
`ACT_COWER3 0x109e`. **`AttemptFeed` (`0x10168910`) reads the victim's `m_IdealActivity`
(`+0xff0`)**, not the current activity, and auto-accepts on `0x104e || 0x1068 || 0x1069 ||
0x1098` only. `TASK_PLAY_COWER` (`0x102a5125`) rolls `m_iCowerAnimOffset +0x6414 = RandomInt(0,2)
× 3` on `ACT_COWER_INTO` and `TASK_SET_COWER` (`0x102a516c`) reuses it, so the loop activity is
`ACT_COWER`/`COWER2`/`COWER3`: **a cowering NPC auto-accepts a feed one time in three**; the other
variants and the `_INTO` window go through the opposed roll. This corrects `feeding.md`'s
activity-based reading.

**Flags the family writes.** `COWERING` (bit 10): readers `BuildScheduleTestBits` (no `COMFORT`
while cowering) and the hearing quarter-radius. `ONE_HIT_KILL` (bit 30): one reader,
`OnTakeDamage` `0x102beda0` — any non-light hit kills outright (slot `0x240`). `IN_FLEE_SCHED`
(bit 7): set by every flee leg, cleared first thing by `COWER_SIMPLE*` so the interest overlay
re-enables while cowering.

**Tasks missing from the port** (Troika StartTask / base StartTask / Troika RunTask / base RunTask;
`—` = default arm): `SUGGEST_STATE 0x06` (`0x102a1b2b` / `0x10286c0d`; reads `frenzied & 1`,
routes `STATE:` into the ideal-state request), `GET_PATH_TO_RANDOM_NODE 0x1f` (— / `0x10285d7f` /
— / `0x10289718`), `FACE_HINTNODE 0x2f` (`0x102a382a` / `0x10283a36` / — / `0x10288b4c`),
`PLAY_SEQUENCE 0x52`, `GET_PATH_TO_COWER_NODE 0x84` (`0x102a2882`: threat = enemy or self, radius
= operand, node query through slot `0x688` into `+0x5ddc`), `_SAVE_POS 0x85` (`0x102a2bd8`,
anchored on `m_vSavePosition +0x5dd0`), `PAUSE_MOVING 0xa6` (`0x102a1f06`), `SET_PRESERVE_PATH
0xc4` (`0x102a3cfa`: `0` → `flags1 &= ~0x08`, else `|= 0x08`), `PLAY_COWER 0xe6` (`0x102a5125` /
run `0x102ab83c`), `SET_COWER 0xe7` (`0x102a516c` / run `0x102ab4b2`), `LOOK_AT_PLAYER 0xfb`,
`CLEAR_NPC_FLAG 0x101`, `RUN_PATH_FLEE 0x103` (`0x102a58d7`: `ACT_PANIC_RUN 0x1093`, else activity
`0x13`), `FLIP_NEXT_IDEAL_YAW 0x106` (`0x102a59eb`). **`DAT_10483aac = 512.0f`** (the "player near" distance for `0x71` vs `0x70`).
`DAT_1072bc88`/`DAT_1072bcc2` are `.data` cells filled at startup from `sound_volume_table.txt`
(the flee sound's radius row and type byte) — read them from the table, not the image.
UNRECOVERED: `+0x6364`, the authored names of hint types `0x2774`/`0x27d8`.

## Species slot-435 overrides all chain (2026-09-08)

`vtmb_slot 435`: 79 classes. `CNPC_VGargoyle` `0x10378fc0`, `CNPC_VHengeyokai` `0x10383090`,
`CNPC_VTzimisce` `0x103bf610`, `CNPC_VWerewolf::OnScheduleChange` `0x103ced10` **all call the
Troika body `0x102a0940` first**, then, gated on `PRESERVE_PATH` clear, decrement a per-species
shun counter (`m_iShunnedFindPillar` / `m_iShunnedFindFish` with `+0x6680 = 0` / `m_iShunnedFindBody`
with `m_ePathMode = 0`); the werewolf keeps a 50-entry schedule history at `+0x668c`. No
classname branch is needed in the port's single `OnScheduleChange`. Classes on the plain base
reset `0x1027a700` (no flag word): `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`,
`CAI_TestHull`, `CCineNPC`, `CCineAI`, `CCineAISchedule`, `CGenericNPC`, `CGenericSabbat_NPC`,
`CGeneric_NPC_bathack`, `CNPC_Bullseye`, `CNPC_Crow`, `CScriptedTarget`; `CGeneric_NPC`,
`CNPC_ProneDialog`, `CPayphone` and the makers carry the Troika body.

## The cover and kick chooser, and the combat leftovers (2026-09-08)

**`FUN_102b7690(bCover, bCorner, bKickOver, bKickAt)`**, callers `SelectSchedule 0x102ae920`
`(1,0,0,0)`, the dodge selector `0x102b7cf0` `(1,1,1,1)`, the melee selectors `0x10385e40` /
`0x10396050` `(0,1,0,1)`, every call gated on **slot 592 `CanSeekCover()` `0x102953e0`**:
`COND_ENEMY_OCCLUDED 0x48` → true; `m_flCanSeekCoverTimer ≤ curtime` → true; `timer − 1.0 <
curtime && !COND_CAN_RANGE_ATTACK1 0x4f` → true; else false (sole override `CNPC_VLasombra`
`0x103893c0`: true while `m_flCoverDisableOverride` is in the future).
- Arm A, kick-prop acquisition: `bKickAt && allow_kick_hint_use && !m_pHintNode && !(flags1 &
  0x800 DODGING) && no prop && curtime ≥ +0x6438` → `+0x6438 = curtime + 2.0`; `m_hKickPhysicsProp
  +0x643c = FUN_102b6650` (needs an enemy; `UTIL_EntitiesInBox` ±512/±512/±64, cap 20,
  `m_edtDerivedType & 4` PHYSICS_PROP; **`prop->m_bNpcKickable +0x788`** (key `npc_kickable`);
  nearest by 2-D distance; `0x102b62e0`).
- Arm B: a live prop → **`0xa9 KICK_PROP_AT_ENEMY`**, before any hint search.
- Arm C: `!m_pHintNode && !DODGING && no prop` → `+0x6448 = enemy`; mask `cover 1 | corner 2 |
  (kickOver && flag) 4 | (kickAt && flag) 8` → `FUN_102b7110` (search `0x102d2980` flags 8,
  distance slot 550 = 1024, one retry when `stay_entrenched`; on success `m_iPeekOutCount +0x640c
  = 0`, `m_iFailedCoverLOSChecks +0x6404 = 0`, reserve `0x102d1350`, corner lean side `+0x63fd`
  from the 2-D cross of enemy−hint with the hint yaw). Category bits: 100/101/10200 → 1,
  10300 → 4, 10301 → 8, 10400 → 0x10, 10000/10100 none; **mask bit 2 matches nothing**, so the
  melee call can only return a kick-at node.
- Arm D on `m_pHintNode->m_nHintType`: 10300 → **`0xa7 HINT_KICK_OVER`**; 10301 → **`0xa8
  HINT_KICK_AT_ENEMY`**; 100/101/10200 → the cover arm; else 0. Cover arm: not `AT_COVER_HINT
  0x2000` → clear `COVER_VS_MELEE_MODE` (flags2 `0x100`), **`0x9b TAKE_COVER_HINT`**; else
  `bMeleeThreat = !(enemy's weapon flags & 0x6000)`; slot 609 `0x102b6b50` (the shoot-at hint,
  type 10400, retry `+0x6440 = curtime + RandomFloat(2, 2.5)`) → **`0xa2 …_SHOOT_AT_HINT`**; else
  `0x102b5de0` (can I still shoot from cover): false → `m_iPeekOutCount++`; `< 5 && !0x48` →
  **`0x9d PEEK_OUT`**; else clear the mode, `stay_entrenched ? 0x9e PEEK_OUT_WAIT :
  ClearHintNode(60) + 0`; true → `peek = max(peek − 2, 0)`, `roll = RandomInt(0, 99)` (drawn
  before the branch); ranged enemy: not already in `0x9e` and mode clear → **`0xa3
  …_HINT_ATTACK`**, else clear the mode and `roll > 29 ? 0xa0 PEEK_OUT_FIRE : 0xa1
  PEEK_OUT_RETURN`; melee enemy: same guard → **`0xa4 …_VS_MELEE`**, else set the mode →
  **`0xa5 …_VS_MELEE_ATK`**. `ClearHintNode(t)` `0x10295ab0`: 60 s hint cooldown
  (`m_flNextUseTime +0x5ec`), clear `AT_COVER_HINT`, reset the attack extents.
- Programs: `0xa9`: `SET_TOLERANCE_DISTANCE 0; GET_PATH_TO_KICK_PROP; SET_NPC_FLAG
  FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SNAP_TO_KICK_PROP; KICK_PROP; CLEAR_NPC_FLAG
  FORCE_RELAXED_ANIMS`, interrupts `NEW_ENEMY, KICK_PROP_INVALID`; `0xa7` ends `FACE_HINTNODE;
  KICK_HINT`, `0xa8` `FACE_ENEMY; KICK_HINT_AT`, interrupts `NEW_ENEMY, HINT_INVALID`. `0xa4` is
  the only program that sets `COVER_VS_MELEE_MODE` by task.

**The kick predicate `0x102b62e0`**: 2-D dot of the 3-D-normalised `(prop − me)` and `(enemy −
prop)` **> cos 10°** (`0x1049ae94`), then a clear trace from `(prop.x, prop.y, prop.z + 8)` to the
enemy's slot-192 point, mask `0x200400b`, `fraction ≥ 1 && !allsolid && !startsolid`.
`m_bNpcKickable`'s only code writer is `TaskFail` `0x1029adb0` (zeroes it and drops the handle):
a kickable prop is consumed by the first task failure of its kicker. Retail authors `npc_kickable
1` on six barrels (`sm_junkyard_1` ×5, `sm_warehouse_1`). **`COND_KICK_PROP_INVALID 0x2a` has no
producer** (dead interrupt); `COND_HINT_INVALID 0x29` is set at `0x10293160`, `0x102d30b9`,
`0x1038943e`.

**The occluded selector's latches.** `flags2 & 0x40000` is NPCFlag **`D_POSSESSED`**
(`0x80040000`; `0x40000` alone on flags1 is `BOTCHED_ATTACK`), produced only by the possession
arm; `frenzied & 0x100` is produced by `CNPC_VFrenzyShadow::vfunc420` (`0x5ddf`), the possession
arm (`0x3b1c`) and the frenzy arm (`0x9fbd`). `0x103675a0` and `0x103b2550` are **jump thunks to
the base selector** owned by `CNPC_VBatSwarm` (vtable `0x104aab04`) and `CNPC_VSheriffSwarm`
(`0x104c7284`); `CNPC_VBach` (`0x10364280`) is the only real override among 64 classes.

**Hint type 10000** is never a `m_pHintNode`: no category bit, no `FValidateHintType` case
(`0x10295c20` handles `0x27d8`, 100/101, `0x2774`, `0x283c/d`, `0x28a0`; base returns 0), so both
searches reject it. Its only lookup is by **name**: `0x102d2840` walks the hint list for type
`10000 || 800` with `stricmp(hint->m_strGroup +0x5f0, name)`, `0x102d2900` returns the node id;
callers `InputFollowPatrolPath 0x1029ed90` and `InputWalkToNode 0x1029e840`, which tokenize the
input and build the `CAI_PatrolPath` at `+0x6590` from node ids. `hint_groups` does not filter
patrol points. `CNodeEnt::Spawn` `0x102d78d0` → `0x102d7d30` maps classnames to types
(`_cover_med` 100, `_cover_low` 101, `_cover_corner` 0x27d8, `_crosswalk` 11000, tzimisce claws
14000/0x36b1, `_kick_over` 0x283c, `_kick_at` 0x283d, `_shoot_at` 0x28a0, werewolf/sabbat/bach/
chang families 16000+, `_manbat_fly_to_point` 20000); 10000 is the FGD default on
`info_node_hint`/`info_node_patrol_point`. UNRECOVERED: what authors type 800.

**`FinViewCone3dNew` `0x103264d0`**, called by `FInViewCone 0x10326750` with the target's slot-192
point, its cone scalar (slot 29) and `m_flFieldOfView`: test 1 `dot(target − eye, fwd) > 0`
(strict, unnormalised); test 2 `apex = eye − k·fwd` (`k` = the float of the ConVar at
`0x10937a8c`, UNRECOVERED name, shared with `CWeaponMelee::RequestActivity`), `dot(fwd,
normalize(target − apex)) × coneScalar > m_flFieldOfView` (strict). The scalar multiplies the
cosine, not the threshold; the pulled-back apex widens the cone with proximity (the debug wedge
`0x1029c4a0` draws it the same way). The 2-D variant is taken when the ConVar at `0x10936f74`
reads 2.

**The possession arm's virtuals**: slot 304 = `CBaseCombatCharacter::GiveBaseFightingItems`
(Troika `0x102b5b20`: no melee (slot 307) and no ranged (slot 308) weapon → `GiveItem
("item_w_fists")`, `AddMiscFlag(0x10 Gave_Fighting_Wpns)`); slot 614 = `ResetThinkTimers()`
`0x102c23f0`; slot 595 = `AcquireNearestHatedTarget()` `0x102b4cc0` (box ±1024/±1024/±128, flag
mask `0x40`, targetable, alive, not hidden, `IRelationType == D_HT`, nearest → slot 596
`SetEnemy`). Possession calls 304 then 614 then 595; frenzy calls 595 first.

## The navigation and reaction keyfields (2026-09-08)

Seven `CAI_BaseNPCTroika` keys the port did not read, each with its readers and a verdict
(ROUTE COST = a term Unreal NavMesh must be given; SELECTION = schedule/task selection, ported
verbatim; ANIMATION). None appears in any vdata pack; they are map-only.

- **`bright_route_penalty`** — `m_iBrightRoutePenalty +0x6344`, int. **Unconsumed in retail**: the
  only access in the image is the copy in `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`); no
  pathfinder cost reads it (checked `+0x6344`, the index form `[0x18d1]`, and every "Penalty"
  string). Authored `0` on 1802 NPCs and `100000` on the three `npc_VLasombra` in
  `la_bradbury_2`. Verdict: nothing to port; a NavMesh light cost would be a divergence.
- **`percent_occluded_wait/_cover/_walk/_flank/_chase`** — `+0x6420/24/28/2c/30`, int.
  **Spawn normalizes them into a cumulative 0–100 ladder** (`0x10298d30`): `sum = all five; if
  (sum > 0) { chase = 100; wait = wait·100/sum; cover = wait + cover·100/sum; walk = cover +
  walk·100/sum; flank = walk + flank·100/sum }`; if `chase != 100` DevMsg and, if `chase < 1`,
  the fallback `10/40/50/70/100`. `_chase` is forced to 100 and **never compared**. `thug_1`'s
  `10/30/10/20/30` normalizes to exactly the fallback. Consumer: the occluded-enemy selector
  `FUN_102b8320` (vtable `+0x978`, slot 606), reached from `CNPC_VHuman::SelectScheduleRangedCombat`
  (`0x10386560`) and `0x103967d0` — **ranged combat only**, never from the melee selector
  `0x10385e40`. Order: `!COND_ENEMY_OCCLUDED 0x48` → 0; `COND_ENEMY_UNREACHABLE 0x59` → `0xaa
  WAIT_FOR_OCCLUDED_ENEMY`; `frenzied & 0x100` → `0xb4 CHASE_ENEMY_LKP`; `flags2 & 0x40000` →
  `roll < 70 ? 0xb6 LKP_FLANK : 0xb4`; `flags1 & 0x10000000` (cleared; set by `0x102b7cf0`'s
  `COMBAT_DODGE_WAIT` arm) → `roll < 80 ? 0xb6 : 0xb4`; else `roll = RandomInt(0, 99)` against
  the ladder — with `COND_SQUAD_SEE_ENEMY 0x31`: `< wait` `0xab`, `< cover` `0xb0`, `< walk`
  `0xb3`, else `0xb2` (the flank arm also returns `0xb2`: no squad flank variant); without:
  `< wait` `0xaa`, `< cover` `0xaf COVER_FROM_OCCLUDED_ENEMY`, `< walk` `0xb5 CHASE_ENEMY_LKP_WALK`,
  `< flank` `0xb6`, else `0xb4`. `CNPC_VBach` overrides (`0x10364280`, latches `+0x66a3`).
  Verdict: SELECTION, verbatim including the dead `_chase` and the duplicated squad arm.
  UNRECOVERED: producers of `frenzied & 0x100` and `flags2 & 0x40000`; the owners of the two
  code-folded copies `0x103675a0`/`0x103b2550` (slot 606 is past the vtable dump).
- **`hint_groups`** — `m_sHintGroups +0x62e0` → `m_iHintGroups +0x62e4`, parser `FUN_102989e0`
  (from Spawn and `ProcessTweakParam` `0x1029aa10` token `HINTGROUPS`): a **space-separated list
  of 1-based indices**, each `1 << (n−1)` for `1..32`; **empty = `0xFFFFFFFF` (all groups)**. Sole
  reader `CAI_BaseNPCTroika::FValidateHintType` (`0x10295c20`, slot 566): `(hint->m_iGroupID
  +0x470 & m_iHintGroups) == 0` → reject, then a switch on `m_nHintType`. `CAI_Hint::Spawn`
  (`0x102d0b60`) converts the hint's `group_id` `1..32` to `1 << (id−1)`, anything else to
  `0xFFFFFFFF`, and assigns a category bit at `+0x474` per type: `100 info_node_cover_med` 1,
  `101 _cover_low` 1, `10000 info_node_hint`/`info_node_patrol_point` none, **`10100 (0x2774)
  info_hint` none**, `10200 _cover_corner` 1, `10300 info_node_kick_over` 4, `10301
  info_node_kick_at` 8, `10400 info_node_shoot_at` 0x10 (with per-type angle/distance/rating
  defaults). The mask search `FUN_102d2980` (global list `DAT_10925450`, cursor `DAT_10925454`;
  flags 1 LOS, 2 nearest, 8 rating-weighted) requires a category bit, so `info_hint` is found
  only by the type-keyed search `0x102d1af0(this, 0x2774, 2, …)` in Troika `StartTask`;
  `FUN_102b7110` stores the result in `m_pHintNode +0x5ddc`. 1716 of ~1830 NPCs author the full
  `"1 … 32"`, identical to leaving it blank. Verdict: SELECTION. UNRECOVERED: the search path for
  type 10000 (no category bit, no validate case).
- **`allow_kick_hint_use`** — `m_bAllowKickHintUse +0x6436`, bool; input `AllowKickHintUse`
  (`0x102c2c10`); copied to children by `CNPCMaker::MakeNPC` (`0x1034b7b0`). Sole consumer
  `FUN_102b7690(bCover, bCorner, bKickOver, bKickAt)`: the kick-prop search (`bKickAt && flag &&
  no hint && !(flags 0x800) && no prop && curtime >= +0x6438` → timer `+2.0`, `m_hKickPhysicsProp
  +0x643c = FUN_102b6650` — needs an enemy, ±512/±512/±64 box, cap 20, prop flag `+0x788`,
  nearest passing `0x102b62e0`); a live prop → **`0xa9 SCHED_TROIKA_KICK_PROP_AT_ENEMY`** before
  any hint search; and the search mask bits `4`/`8` (kick-over 10300 → `0xa7 HINT_KICK_OVER`,
  kick-at 10301 → `0xa8 HINT_KICK_AT_ENEMY`). Callers: `0x102ae920` `(1,0,0,0)` (entrenched
  cover), the melee selector `0x10385e40` `(0,1,0,1)`, `0x102b7cf0` `(1,1,1,1)`. No hint type
  carries category bit 2, so **for a melee NPC the hint search returns only a kick-at node, and
  only under this flag**. Retail maps hold exactly one kick hint (`sm_beachhouse_1` `kick_spot`,
  10300) and no kick-at nodes; the live path is the physics-prop kick. Verdict: SELECTION.
  UNRECOVERED: `0x102b62e0`, `prop+0x788`, the producer of `COND_KICK_PROP_INVALID 0x2a`.
- **`stay_entrenched`** — seven readers, all "do not give up my cover": `NPCThink` (`0x10292de0`,
  suppresses the hint release on `m_hHintCoverObject == enemy && (0x2e || 0x48)`); the cover-hint
  evaluator `0x10296c40` (accept own hint immediately; skip the max-range reject); `SelectSchedule`
  `0x102ae920` (combat + slot 592 → `FUN_102b7690(1,0,0,0)` ahead of the ordinary tail);
  `0x102b7110` (retry the search once); `0x102b7690` (cover LOS failed and `m_iPeekOutCount
  +0x640c >= 5` or occluded: entrenched → `0x9e TAKE_COVER_PEEK_OUT_WAIT`, else `ClearHintNode(60)`);
  `0x102b7cf0` (no dodge-reposition `0x8c`/`0x8d` when entrenched); `ShouldInvestigate`.
  Authored `1` on 65 NPCs. Verdict: SELECTION.
- **`combat_start_activity`** — `m_sCombatStartActivity +0x65e0` → activity id `+0x65e4`
  (`FUN_1029f340` → `ActivityFromName 0x10412520`; absent, `"-1"` and `"ACT_INVALID"` all give
  −1). Gate at the top of `SelectSchedule` `0x102ae920`: `m_iSquadDisconnected < 1 && squad &&
  (flags2 & 0x2000)` → consume the latch; `activity != −1 && !(frenzied & 0x80)` → **`0xeb
  SCHED_TROIKA_START_COMBAT_SQUAD`** (`WAIT_FACE_ENEMY 0.2; PLAY_SOUND Target_Acquired;
  TASK_PLAY_COMBAT_START_SEQUENCE 0x137; TASK_SQUAD_NEW_ENEMY; WAIT_RANDOM 0.2`), else notify the
  squad (`0x103161a0`). `0xea START_COMBAT` (on `NEW_ENEMY && !frenzied`) has no activity task.
  Retail authors a real activity on exactly one NPC (`sec_cam_1_npc`, `la_museum_1`, no squad),
  so the animation is unobservable in shipped content. Verdict: ANIMATION plus the `0xeb` gate.
- **`interesting_place_groups`** — `+0x62d8` → `+0x62dc`, parser `FUN_10298910`: the **same
  1-based index list → mask** as `hint_groups`, but **empty or `"0"` leaves the mask `0`** (no
  place ever matches; 1005 retail NPCs author `"0"`). `CNPC_VCamera::Spawn` zeroes it. This
  corrects the "raw AND of two decimal integers" reading in the interesting-places section for
  the NPC side. UNRECOVERED: whether `CAI_InterestingPlace` converts its own `group_id` to
  `1 << (id−1)` as `CAI_Hint::Spawn` does; if it does, `thug_1`'s pool is `pt1` alone, if the
  place keeps the raw integer the seven-node pool stands — **decided: it converts**
  (`CAI_InterestingPlace::Spawn` `0x102d9c20`), so `thug_1`'s pool is `pt1` alone; the
  interesting-places section above is corrected.
