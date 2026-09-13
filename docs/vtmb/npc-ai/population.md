# NPC AI — The authored population

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

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
[player-entity.md](../player-entity.md).

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
