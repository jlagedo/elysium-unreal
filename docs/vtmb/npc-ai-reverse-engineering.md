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
fields, relationship token decoding, the AI update loop, the state-switch cases, schedule/task
registrations, and the `aiscripted_schedule` execution modes. Confidence is lower for the exact
meaning of several numeric map keyfields, the human-readable distinction between the two move and
two follow modes of `aiscripted_schedule`, and the frame-exact ordering of reactions during a live
aggression incident. Those open points need controlled retail capture rather than further naming
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
performed for this survey. Native code proves the control structures and the authored data proves
what maps request, but static recovery alone does not prove the precise frame on which two outputs
observed during the same incident are delivered. Numeric fields whose names are suggestive but
whose consumers have not yet been decoded are reported as fields and distributions, not assigned
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
| 2 | alert |
| 3 | combat |
| 4 | scripted |
| 6 | prone |
| 7 | dead |
| 12 | class-specific/fallback idle-like branch; exact semantic label unresolved |

The combat branch further tests enemy state, damage, attack capability, range/occlusion, and other
conditions before choosing a schedule. Derived VtMB NPC classes override or extend the selection
and task machinery, which is why the native class is a load-bearing part of authored NPC identity.

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

Map fields provide per-actor sensory tuning. `stealth.txt` supplies light-to-visual-stealth
scalars and view-cone scalars. `sound_volume_table.txt` classifies sound levels and occlusion:

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
corpus includes zero and 99. The exact arbitration rule between competing rows of equal/different
priority remains to be decoded and should not be guessed from the diagnostic alone.

`IRelationType` at `0x10333340` resolves an entity override before a class override and otherwise
returns neutral, with additional special-flag/oblivious branches. Self and null relations resolve
neutral. The proven ordinary-table precedence is therefore:

```text
exact entity relationship
  -> class relationship
  -> neutral default
```

The exact placement and meaning of the additional special branches remain open.

The map's `player_reaction` is an initial authored input to this combat relationship domain. A
later `SetRelationship` call can deliberately alter it.

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
visible" into "forgotten and neutral."

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
| 2 | combat (3) |
| 3 | alert (2) |

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
explicit remaster delta. It also prevents a visually convincing animation graph from becoming the
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

The general native senses/memory, relationship priority arbitration, relationship consumers,
high-level state/condition loop, combat schedule/task graph, class-specific combat policy, full
follower policy, and RPG reaction-score model are not yet reconstructed.
`aiscripted_schedule.StartSchedule` is presently a stub. Existing
patrol, dialogue, sequence, animation, damage, and entity-I/O foundations should be extended at
their existing ownership seams rather than replaced by a parallel NPC runtime.

This section is a scope boundary, not a second status tracker. Project status and priority remain
in the declared roadmaps; source remains the as-built record.

## Open questions and targeted capture programme

### Native questions still requiring recovery

- What is the exact priority arbitration when multiple relationship rows are applicable, including
  equal priority and the corpus's out-of-diagnostic-range values?
- What do all numeric values of `npc_perception`, investigation modes, and player conduct
  thresholds mean at their native consumers?
- Which special flags and oblivious branches alter `IRelationType` before ordinary table lookup?
- What is the semantic name of NPC-state selector case 12?
- Which derived classes add each dialogue/incapacitation/transform output, and what are their exact
  firing conditions?
- What is the precise movement/gait distinction between `aiscripted_schedule` modes 1/2 and 4/5?
- What memory expiry and search rules govern lost LOS, lost enemy, and return to idle?
- How do squad relation knowledge and `SQUAD_SEE_ENEMY` propagate and expire?

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
