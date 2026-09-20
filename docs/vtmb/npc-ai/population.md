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


## The AI infrastructure census (2026-09-16, 0018 story 1)

_Computed by `uv run elysium research ai_infra_census` over 108 entity units and 100 nav-graph units under the V2 export root._

### Per map
| map | nodes | links | jump links | components | five largest | entity graph nodes | hint nodes | places | conversation places | info_node_patrol_point | makers | squad members | maker squad requests | squads (per map) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ch_cloud_1 | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| ch_dragon_1 | 38 | 53 | 1 | 4 | 22, 13, 2, 1 | 38 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 |
| ch_fishmarket_1 | 96 | 404 | 60 | 5 | 92, 1, 1, 1, 1 | 82 | 22 | 0 | 0 | 0 | 0 | 2 | 0 | 1 |
| ch_fulab_1 | 119 | 248 | 28 | 2 | 90, 29 | 100 | 21 | 0 | 0 | 0 | 0 | 24 | 0 | 2 |
| ch_glaze_1 | 142 | 408 | 46 | 16 | 124, 4, 1, 1, 1 | 69 | 85 | 34 | 0 | 0 | 0 | 22 | 0 | 6 |
| ch_hub_1 | 293 | 820 | 105 | 2 | 236, 57 | 212 | 81 | 77 | 3 | 19 | 28 | 0 | 15 | 2 |
| ch_lotus_1 | 222 | 439 | 1 | 3 | 220, 1, 1 | 196 | 26 | 12 | 2 | 3 | 4 | 15 | 6 | 3 |
| ch_ramen_1 | 25 | 45 | 0 | 1 | 25 | 17 | 8 | 2 | 0 | 0 | 0 | 6 | 0 | 2 |
| ch_shrekhub | 70 | 126 | 0 | 1 | 70 | 50 | 20 | 0 | 0 | 10 | 0 | 0 | 0 | 0 |
| ch_temple_1 | 203 | 556 | 115 | 2 | 202, 1 | 164 | 39 | 29 | 0 | 12 | 2 | 27 | 2 | 7 |
| ch_temple_2 | 226 | 466 | 1 | 3 | 224, 1, 1 | 183 | 47 | 17 | 0 | 16 | 13 | 55 | 13 | 10 |
| ch_temple_3 | 132 | 279 | 1 | 2 | 131, 1 | 60 | 72 | 6 | 0 | 6 | 6 | 27 | 2 | 5 |
| ch_temple_4 | 26 | 59 | 0 | 1 | 26 | 26 | 12 | 0 | 0 | 0 | 2 | 3 | 2 | 1 |
| ch_tsengs_1 | 7 | 7 | 0 | 3 | 5, 1, 1 | 7 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| ch_zhaos_1 | 105 | 373 | 128 | 15 | 91, 1, 1, 1, 1 | 55 | 50 | 17 | 0 | 0 | 0 | 24 | 0 | 2 |
| hw_609_1 | 89 | 199 | 2 | 3 | 39, 32, 18 | 74 | 16 | 0 | 0 | 0 | 4 | 4 | 4 | 1 |
| hw_ash_sewer_1 | 171 | 458 | 29 | 2 | 170, 1 | 126 | 45 | 0 | 0 | 1 | 0 | 21 | 0 | 5 |
| hw_asphole_1 | 49 | 74 | 2 | 2 | 46, 3 | 49 | 0 | 43 | 5 | 0 | 0 | 0 | 0 | 0 |
| hw_cemetery_1 | 210 | 623 | 117 | 2 | 209, 1 | 210 | 0 | 6 | 0 | 0 | 67 | 67 | 67 | 3 |
| hw_chateau_1 | - | - | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hw_chinese_1 | 32 | 67 | 9 | 1 | 32 | 32 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| hw_hub_1 | 361 | 1026 | 69 | 3 | 300, 55, 6 | 240 | 121 | 122 | 0 | 22 | 57 | 4 | 14 | 3 |
| hw_jewelry_1 | 27 | 44 | 0 | 4 | 16, 6, 4, 1 | 27 | 0 | 0 | 0 | 0 | 4 | 0 | 4 | 1 |
| hw_luckystar_1 | 11 | 9 | 0 | 2 | 7, 4 | 11 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| hw_metalhead_1 | 54 | 112 | 7 | 1 | 54 | 24 | 30 | 3 | 0 | 14 | 0 | 0 | 0 | 0 |
| hw_netcafe_1 | 144 | 320 | 19 | 8 | 80, 52, 7, 1, 1 | 124 | 20 | 16 | 0 | 14 | 0 | 1 | 0 | 1 |
| hw_redspot_1 | 14 | 15 | 0 | 2 | 13, 1 | 14 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 |
| hw_sinbin_1 | 47 | 74 | 0 | 1 | 47 | 47 | 0 | 10 | 0 | 0 | 0 | 0 | 0 | 0 |
| hw_tawni_1 | 26 | 35 | 0 | 1 | 26 | 26 | 0 | 6 | 0 | 0 | 0 | 2 | 0 | 1 |
| hw_vesuvius_1 | 63 | 101 | 22 | 3 | 52, 7, 4 | 63 | 0 | 26 | 2 | 0 | 0 | 0 | 0 | 0 |
| hw_warrens_1 | 106 | 171 | 31 | 8 | 66, 34, 1, 1, 1 | 106 | 0 | 47 | 0 | 0 | 2 | 5 | 2 | 2 |
| hw_warrens_2 | 174 | 354 | 10 | 6 | 109, 33, 29, 1, 1 | 174 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 1 |
| hw_warrens_2b | - | - | - | - | - | 19 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 2 |
| hw_warrens_3 | 132 | 305 | 5 | 4 | 129, 1, 1, 1 | 132 | 0 | 23 | 0 | 0 | 2 | 11 | 2 | 4 |
| hw_warrens_4 | 118 | 211 | 1 | 2 | 117, 1 | 118 | 0 | 46 | 0 | 0 | 2 | 11 | 2 | 6 |
| hw_warrens_5 | 31 | 38 | 0 | 1 | 31 | 31 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_abandoned_building_1 | 8 | 11 | 0 | 1 | 8 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_bradbury_1 | - | - | - | - | - | 24 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_bradbury_2 | 252 | 314 | 1 | 48 | 49, 42, 26, 24, 16 | 191 | 61 | 11 | 0 | 0 | 0 | 20 | 0 | 6 |
| la_bradbury_3 | 117 | 504 | 151 | 4 | 114, 1, 1, 1 | 65 | 52 | 0 | 0 | 0 | 0 | 2 | 0 | 1 |
| la_chantry_1 | 6 | 9 | 0 | 1 | 6 | 6 | 0 | 3 | 0 | 0 | 0 | 1 | 0 | 1 |
| la_confession_1 | 89 | 276 | 108 | 7 | 76, 6, 3, 1, 1 | 88 | 0 | 20 | 0 | 0 | 0 | 2 | 0 | 2 |
| la_crackhouse_1 | 192 | 398 | 2 | 6 | 97, 65, 23, 4, 2 | 178 | 14 | 19 | 0 | 0 | 27 | 13 | 27 | 3 |
| la_dane_1 | 174 | 534 | 159 | 3 | 172, 1, 1 | 130 | 44 | 28 | 3 | 0 | 7 | 25 | 7 | 7 |
| la_empire_1 | 67 | 167 | 84 | 1 | 67 | 67 | 0 | 34 | 3 | 0 | 0 | 0 | 0 | 0 |
| la_empire_2 | 114 | 246 | 1 | 1 | 114 | 64 | 50 | 4 | 1 | 4 | 0 | 9 | 0 | 1 |
| la_empire_3 | 14 | 18 | 0 | 1 | 14 | 14 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_expipe_1 | 14 | 16 | 0 | 4 | 6, 6, 1, 1 | 14 | 0 | 6 | 1 | 0 | 0 | 0 | 0 | 0 |
| la_hospital_1 | 24 | 32 | 1 | 2 | 20, 4 | 24 | 0 | 0 | 0 | 0 | 3 | 0 | 3 | 1 |
| la_hub_1 | 320 | 753 | 31 | 9 | 241, 51, 12, 8, 3 | 253 | 67 | 124 | 0 | 13 | 40 | 10 | 17 | 6 |
| la_library_1 | - | - | - | - | - | 4 | 10 | 0 | 0 | 10 | 0 | 2 | 0 | 1 |
| la_malkavian_1 | 22 | 58 | 8 | 1 | 22 | 22 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_malkavian_2 | 185 | 443 | 19 | 5 | 126, 29, 15, 12, 3 | 156 | 29 | 7 | 0 | 29 | 0 | 19 | 0 | 6 |
| la_malkavian_3 | 58 | 106 | 5 | 3 | 27, 18, 13 | 39 | 19 | 0 | 0 | 19 | 0 | 16 | 0 | 4 |
| la_malkavian_3b | - | - | - | - | - | 23 | 12 | 0 | 0 | 12 | 0 | 0 | 0 | 0 |
| la_malkavian_4 | 104 | 226 | 14 | 1 | 104 | 104 | 0 | 0 | 0 | 0 | 0 | 9 | 0 | 4 |
| la_malkavian_5 | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_museum_1 | 421 | 1011 | 36 | 7 | 413, 3, 1, 1, 1 | 281 | 160 | 21 | 0 | 1 | 7 | 22 | 7 | 3 |
| la_parkinggarage_1 | 412 | 1189 | 4 | 5 | 398, 10, 2, 1, 1 | 204 | 208 | 42 | 6 | 31 | 0 | 42 | 0 | 7 |
| la_plaguebearer_sewer_1 | 77 | 159 | 3 | 2 | 51, 26 | 77 | 0 | 23 | 0 | 0 | 13 | 0 | 0 | 0 |
| la_skyline_1 | 88 | 135 | 13 | 7 | 41, 32, 7, 5, 1 | 77 | 11 | 11 | 0 | 11 | 0 | 1 | 0 | 1 |
| la_ventruetower_1 | 85 | 197 | 17 | 3 | 62, 22, 1 | 59 | 26 | 1 | 0 | 0 | 0 | 0 | 0 | 0 |
| la_ventruetower_1b | 382 | 1068 | 214 | 5 | 275, 104, 1, 1, 1 | 219 | 163 | 30 | 0 | 29 | 7 | 22 | 4 | 4 |
| la_ventruetower_2 | 279 | 656 | 42 | 4 | 110, 109, 59, 1 | 146 | 133 | 13 | 0 | 43 | 0 | 20 | 0 | 4 |
| la_ventruetower_3 | 110 | 435 | 162 | 13 | 88, 11, 1, 1, 1 | 61 | 49 | 0 | 0 | 0 | 4 | 6 | 4 | 2 |
| sm_apartment_1 | 2 | 1 | 0 | 1 | 2 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_asylum_1 | 48 | 98 | 2 | 3 | 44, 3, 1 | 48 | 0 | 34 | 3 | 0 | 0 | 0 | 0 | 0 |
| sm_bailbonds_1 | 4 | 5 | 0 | 1 | 4 | 4 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_basement_1 | 28 | 39 | 0 | 4 | 15, 11, 1, 1 | 28 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_beachhouse_1 | 113 | 266 | 22 | 2 | 102, 11 | 104 | 36 | 23 | 1 | 5 | 0 | 7 | 0 | 1 |
| sm_coffee_1 | - | - | - | - | - | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_diner_1 | 19 | 25 | 2 | 1 | 19 | 14 | 5 | 12 | 1 | 0 | 0 | 4 | 0 | 1 |
| sm_gallery_1 | 20 | 42 | 0 | 3 | 18, 1, 1 | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_hub_1 | 578 | 1856 | 119 | 5 | 510, 64, 2, 1, 1 | 304 | 274 | 76 | 0 | 34 | 48 | 31 | 11 | 2 |
| sm_hub_2 | 488 | 1511 | 107 | 10 | 479, 1, 1, 1, 1 | 258 | 229 | 2 | 0 | 30 | 0 | 11 | 0 | 4 |
| sm_junkyard_1 | 79 | 245 | 102 | 1 | 79 | 79 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_medical_1 | 36 | 42 | 0 | 3 | 15, 13, 8 | 30 | 10 | 9 | 0 | 5 | 2 | 0 | 0 | 0 |
| sm_oceanhouse_1 | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_oceanhouse_2 | 15 | 9 | 0 | 6 | 4, 3, 3, 2, 2 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_pawnshop_1 | 5 | 6 | 0 | 1 | 5 | 5 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_pawnshop_2 | 5 | 4 | 0 | 1 | 5 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_pier_1 | 102 | 187 | 8 | 5 | 65, 26, 7, 3, 1 | 100 | 10 | 15 | 0 | 10 | 3 | 4 | 3 | 2 |
| sm_shreknet_1 | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_smoke_1 | - | - | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_tattoo | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_vamparena | 38 | 161 | 88 | 2 | 37, 1 | 27 | 13 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sm_warehouse_1 | 526 | 1650 | 266 | 14 | 513, 1, 1, 1, 1 | 350 | 183 | 35 | 5 | 30 | 2 | 47 | 2 | 18 |
| sp_endsequences_a | 8 | 6 | 1 | 2 | 5, 3 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_endsequences_b | 7 | 11 | 3 | 1 | 7 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_epilogue | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_genesisdevice_1 | - | - | - | - | - | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_giovanni_1 | 232 | 830 | 226 | 1 | 232 | 171 | 61 | 7 | 0 | 15 | 3 | 25 | 5 | 5 |
| sp_giovanni_2a | 223 | 518 | 74 | 1 | 223 | 181 | 42 | 22 | 6 | 0 | 7 | 32 | 7 | 9 |
| sp_giovanni_2b | 222 | 489 | 69 | 3 | 213, 8, 1 | 180 | 42 | 0 | 0 | 0 | 7 | 33 | 7 | 6 |
| sp_giovanni_3 | 80 | 126 | 0 | 1 | 80 | 80 | 0 | 9 | 0 | 0 | 9 | 12 | 9 | 3 |
| sp_giovanni_4 | 211 | 366 | 46 | 5 | 207, 1, 1, 1, 1 | 217 | 0 | 0 | 0 | 0 | 0 | 31 | 0 | 1 |
| sp_giovanni_5 | 92 | 408 | 203 | 1 | 92 | 41 | 51 | 0 | 0 | 0 | 0 | 4 | 0 | 1 |
| sp_masquerade_1 | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_ninesintro | 0 | 0 | 0 | 0 |  | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_observatory_1 | 20 | 46 | 0 | 1 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_observatory_2 | 221 | 691 | 103 | 11 | 211, 1, 1, 1, 1 | 89 | 132 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_soc_1 | 196 | 637 | 162 | 8 | 189, 1, 1, 1, 1 | 131 | 66 | 16 | 3 | 30 | 4 | 23 | 0 | 3 |
| sp_soc_2 | 107 | 264 | 22 | 3 | 51, 31, 25 | 56 | 51 | 16 | 2 | 30 | 0 | 15 | 0 | 2 |
| sp_soc_3 | 116 | 335 | 36 | 5 | 72, 21, 18, 4, 1 | 76 | 40 | 13 | 2 | 25 | 0 | 11 | 0 | 3 |
| sp_soc_4 | 60 | 171 | 32 | 3 | 49, 10, 1 | 54 | 15 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_taxiride | 7 | 5 | 0 | 2 | 5, 2 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| sp_theatre | 104 | 311 | 18 | 2 | 70, 34 | 81 | 23 | 11 | 0 | 12 | 0 | 0 | 0 | 0 |
| sp_tutorial_1 | 116 | 234 | 9 | 7 | 33, 24, 23, 16, 10 | 154 | 49 | 29 | 0 | 37 | 14 | 9 | 0 | 3 |
| totals | 11305 | 29075 | 3604 | 380 | 513, 510, 479, 413, 398 | 8458 | 3156 | 1293 | 49 | 582 | 400 | 875 | 248 | 199 |

### Keys authored per infrastructure classname
| classname | key | rows | distinct values | examples (<=5, sorted) |
| --- | --- | --- | --- | --- |
| info_hint | angles | 29 | 7 | 0 0 0 / 0 159 0 / 0 270 0 / 0 347 0 / 0 69 0 |
| info_hint | classname | 29 | 1 | info_hint |
| info_hint | hinttype | 29 | 2 | 10100 / 19000 |
| info_hint | nodeid | 17 | 1 | 0 |
| info_hint | origin | 29 | 29 | -355 1397 17 / -370 204 8.99999 / -38 65 8.99997 / -410 230 8.99999 / -418 1396 17 |
| info_hint | starthidden | 7 | 1 | 0 |
| info_hint | starthintdisabled | 17 | 1 | 0 |
| info_hint | targetname | 12 | 1 | TransformHint |
| info_node |  | 1 | 1 | ,,,0,-1,, |
| info_node | angles | 2199 | 9 | 0 0 0 / 0 125 0 / 0 135 0 / 0 180 0 / 0 240 0 |
| info_node | classname | 8353 | 1 | info_node |
| info_node | nodeid | 8353 | 590 | 0 / 1 / 10 / 100 / 101 |
| info_node | origin | 8353 | 7819 | -0.680655 -210.911 -31 / -0.708975 1187.84 -629 / -0.790817 -317.223 9.28718 / -1 -2376 25 / -1 -528 16 |
| info_node | spawnflags | 26 | 1 | 0 |
| info_node | starthidden | 190 | 1 | 0 |
| info_node | targetname | 23 | 20 | camera_node / front_corner / hp_b_1 / hp_b_2 / hp_b_3 |
| info_node_bach_run_1 | angles | 1 | 1 | 0 49 0 |
| info_node_bach_run_1 | classname | 1 | 1 | info_node_bach_run_1 |
| info_node_bach_run_1 | group_id | 1 | 1 | 1 |
| info_node_bach_run_1 | hint_rating | 1 | 1 | 3 |
| info_node_bach_run_1 | hinttype | 1 | 1 | 17004 |
| info_node_bach_run_1 | nodeid | 1 | 1 | 4 |
| info_node_bach_run_1 | origin | 1 | 1 | -5283 1777 -187 |
| info_node_bach_run_1 | starthintdisabled | 1 | 1 | 0 |
| info_node_bach_run_1 | target_angle_range | 1 | 1 | 60 |
| info_node_bach_run_1 | target_dist_max | 1 | 1 | 32000 |
| info_node_bach_run_1 | target_dist_min | 1 | 1 | 256 |
| info_node_bach_run_2 | angles | 1 | 1 | 0 229 0 |
| info_node_bach_run_2 | classname | 1 | 1 | info_node_bach_run_2 |
| info_node_bach_run_2 | group_id | 1 | 1 | 1 |
| info_node_bach_run_2 | hint_rating | 1 | 1 | 3 |
| info_node_bach_run_2 | hinttype | 1 | 1 | 17005 |
| info_node_bach_run_2 | nodeid | 1 | 1 | 5 |
| info_node_bach_run_2 | origin | 1 | 1 | -4480 3383 133 |
| info_node_bach_run_2 | target_angle_range | 1 | 1 | 60 |
| info_node_bach_run_2 | target_dist_max | 1 | 1 | 32000 |
| info_node_bach_run_2 | target_dist_min | 1 | 1 | 256 |
| info_node_bach_teleport_1 | angles | 25 | 2 | 0 0 0 / 0 232 0 |
| info_node_bach_teleport_1 | classname | 25 | 1 | info_node_bach_teleport_1 |
| info_node_bach_teleport_1 | group_id | 25 | 1 | 1 |
| info_node_bach_teleport_1 | hint_rating | 25 | 1 | 3 |
| info_node_bach_teleport_1 | hinttype | 25 | 1 | 17000 |
| info_node_bach_teleport_1 | nodeid | 25 | 25 | 20 / 21 / 22 / 25 / 26 |
| info_node_bach_teleport_1 | origin | 25 | 25 | -118.454 271.384 13.1966 / -189.761 459.038 13.1966 / -21 -297 -250 / -272.773 -264.07 13.1966 / -363.551 296.81 13.1966 |
| info_node_bach_teleport_1 | target_angle_range | 25 | 1 | 60 |
| info_node_bach_teleport_1 | target_dist_max | 25 | 1 | 32000 |
| info_node_bach_teleport_1 | target_dist_min | 25 | 1 | 256 |
| info_node_bach_teleport_2 | angles | 9 | 5 | 0 138 0 / 0 227 0 / 0 277 0 / 0 326 0 / 0 359 0 |
| info_node_bach_teleport_2 | classname | 9 | 1 | info_node_bach_teleport_2 |
| info_node_bach_teleport_2 | group_id | 9 | 1 | 1 |
| info_node_bach_teleport_2 | hint_rating | 9 | 1 | 3 |
| info_node_bach_teleport_2 | hinttype | 9 | 1 | 17001 |
| info_node_bach_teleport_2 | nodeid | 9 | 9 | 2 / 49 / 50 / 58 / 68 |
| info_node_bach_teleport_2 | origin | 9 | 9 | -337 -98 -250 / -433 101 -250 / -5286 3173 -118 / -662 -107 -250 / -703 425 -250 |
| info_node_bach_teleport_2 | target_angle_range | 9 | 1 | 60 |
| info_node_bach_teleport_2 | target_dist_max | 9 | 1 | 32000 |
| info_node_bach_teleport_2 | target_dist_min | 9 | 1 | 256 |
| info_node_bach_teleport_3 | angles | 2 | 2 | 0 0 0 / 0 121 0 |
| info_node_bach_teleport_3 | classname | 2 | 1 | info_node_bach_teleport_3 |
| info_node_bach_teleport_3 | group_id | 2 | 1 | 1 |
| info_node_bach_teleport_3 | hint_rating | 2 | 1 | 3 |
| info_node_bach_teleport_3 | hinttype | 2 | 1 | 17002 |
| info_node_bach_teleport_3 | nodeid | 2 | 2 | 1 / 2 |
| info_node_bach_teleport_3 | origin | 2 | 2 | -4939 2012 -188 / -9 -23 13 |
| info_node_bach_teleport_3 | starthintdisabled | 1 | 1 | 0 |
| info_node_bach_teleport_3 | target_angle_range | 2 | 1 | 60 |
| info_node_bach_teleport_3 | target_dist_max | 2 | 1 | 32000 |
| info_node_bach_teleport_3 | target_dist_min | 2 | 1 | 256 |
| info_node_bach_teleport_3 | targetname | 1 | 1 | reformtarget2 |
| info_node_bach_teleport_4 | angles | 1 | 1 | 0 0 0 |
| info_node_bach_teleport_4 | classname | 1 | 1 | info_node_bach_teleport_4 |
| info_node_bach_teleport_4 | group_id | 1 | 1 | 1 |
| info_node_bach_teleport_4 | hint_rating | 1 | 1 | 3 |
| info_node_bach_teleport_4 | hinttype | 1 | 1 | 17003 |
| info_node_bach_teleport_4 | nodeid | 1 | 1 | 4 |
| info_node_bach_teleport_4 | origin | 1 | 1 | -4878 3023 -186 |
| info_node_bach_teleport_4 | target_angle_range | 1 | 1 | 60 |
| info_node_bach_teleport_4 | target_dist_max | 1 | 1 | 32000 |
| info_node_bach_teleport_4 | target_dist_min | 1 | 1 | 256 |
| info_node_chang_column | angles | 2 | 1 | 0 0 0 |
| info_node_chang_column | classname | 2 | 1 | info_node_chang_column |
| info_node_chang_column | group_id | 2 | 1 | 1 |
| info_node_chang_column | hint_rating | 2 | 1 | 3 |
| info_node_chang_column | hinttype | 2 | 1 | 18001 |
| info_node_chang_column | nodeid | 2 | 1 | 1 |
| info_node_chang_column | origin | 2 | 2 | -1 83 13.1966 / -72 75 5.61725 |
| info_node_chang_column | target_angle_range | 2 | 1 | 60 |
| info_node_chang_column | target_dist_max | 2 | 1 | 32000 |
| info_node_chang_column | target_dist_min | 2 | 1 | 256 |
| info_node_chang_jumpbase | angles | 6 | 1 | 0 0 0 |
| info_node_chang_jumpbase | classname | 6 | 1 | info_node_chang_jumpbase |
| info_node_chang_jumpbase | group_id | 6 | 1 | 1 |
| info_node_chang_jumpbase | hint_rating | 6 | 1 | 3 |
| info_node_chang_jumpbase | hinttype | 6 | 1 | 18000 |
| info_node_chang_jumpbase | nodeid | 6 | 6 | 2 / 26 / 27 / 3 / 44 |
| info_node_chang_jumpbase | origin | 6 | 6 | -121.14 91.869 5.19657 / -132.317 -219.069 5.1966 / -342.954 -50.6551 5.1966 / -72 114 5.61726 / -72 37 5.61725 |
| info_node_chang_jumpbase | target_angle_range | 6 | 1 | 60 |
| info_node_chang_jumpbase | target_dist_max | 6 | 1 | 32000 |
| info_node_chang_jumpbase | target_dist_min | 6 | 1 | 256 |
| info_node_chang_ledge | angles | 37 | 1 | 0 0 0 |
| info_node_chang_ledge | classname | 37 | 1 | info_node_chang_ledge |
| info_node_chang_ledge | group_id | 37 | 1 | 1 |
| info_node_chang_ledge | hint_rating | 37 | 1 | 3 |
| info_node_chang_ledge | hinttype | 37 | 1 | 18003 |
| info_node_chang_ledge | nodeid | 37 | 34 | 1 / 10 / 11 / 12 / 13 |
| info_node_chang_ledge | origin | 37 | 37 | -1.82041 468.398 213.197 / -100.251 -367.622 213.197 / -111 -416 141 / -175.198 463.931 213.197 / -259 -196 133 |
| info_node_chang_ledge | target_angle_range | 37 | 1 | 60 |
| info_node_chang_ledge | target_dist_max | 37 | 1 | 32000 |
| info_node_chang_ledge | target_dist_min | 37 | 1 | 256 |
| info_node_chang_teleport | angles | 43 | 1 | 0 0 0 |
| info_node_chang_teleport | classname | 43 | 1 | info_node_chang_teleport |
| info_node_chang_teleport | group_id | 43 | 1 | 1 |
| info_node_chang_teleport | hint_rating | 43 | 1 | 3 |
| info_node_chang_teleport | hinttype | 43 | 1 | 18002 |
| info_node_chang_teleport | nodeid | 43 | 43 | 10 / 11 / 12 / 13 / 14 |
| info_node_chang_teleport | origin | 43 | 43 | -118.483 -365.657 261.197 / -13.3846 213.55 5.61732 / -142.757 -33.6129 5.61725 / -156.917 -499.995 60.1966 / -213.36 191.862 5.61725 |
| info_node_chang_teleport | target_angle_range | 43 | 1 | 60 |
| info_node_chang_teleport | target_dist_max | 43 | 1 | 32000 |
| info_node_chang_teleport | target_dist_min | 43 | 1 | 256 |
| info_node_climb | angles | 4 | 1 | 0 0 0 |
| info_node_climb | classname | 4 | 1 | info_node_climb |
| info_node_climb | hinttype | 4 | 1 | 10000 |
| info_node_climb | nodeid | 4 | 4 | 0 / 18 / 19 / 20 |
| info_node_climb | origin | 4 | 4 | 3658 986 -422 / 3669 979 -385 / 3678 970 -344 / 3724 974 -301 |
| info_node_climb | starthintdisabled | 4 | 1 | 0 |
| info_node_cover_corner | angles | 1438 | 64 | 0 0 0 / 0 124 0 / 0 125 0 / 0 127 0 / 0 128 0 |
| info_node_cover_corner | classname | 1438 | 1 | info_node_cover_corner |
| info_node_cover_corner | group | 95 | 7 | 1 / 2 / 3 / 4 / 5 |
| info_node_cover_corner | group_id | 1438 | 11 | 1 / 11 / 2 / 20 / 3 |
| info_node_cover_corner | hint_rating | 1438 | 7 | 0 / 1 / 3 / 4 / 5 |
| info_node_cover_corner | hinttype | 1438 | 1 | 10200 |
| info_node_cover_corner | nodeid | 1438 | 478 | 0 / 1 / 10 / 100 / 101 |
| info_node_cover_corner | origin | 1438 | 1188 | -0.0588722 -1144.06 4 / -0.0588722 403.941 4 / -0.0588722 503.941 4 / -1009 117 2617 / -1009 291 2617 |
| info_node_cover_corner | starthidden | 89 | 2 | 0 / 1 |
| info_node_cover_corner | starthintdisabled | 745 | 2 | 0 / 1 |
| info_node_cover_corner | target_angle_range | 1438 | 6 | 17 / 30 / 35 / 50 / 60 |
| info_node_cover_corner | target_dist_max | 1438 | 11 | 2000 / 300 / 32000 / 500 / 600 |
| info_node_cover_corner | target_dist_min | 1438 | 5 | 120 / 150 / 200 / 256 / 64 |
| info_node_cover_corner | target_name | 79 | 15 | Bruno guard4 / cellar_guard / guard_alarm / guard_alarm guard7 / guard_alarm7 |
| info_node_cover_corner | targetname | 236 | 32 | Cover_Corner / IdolRoom_1_Cover / IdolRoom_1_Cover2 / IdolRoom_2_Cover / IdolRoom_2_Cover2 |
| info_node_cover_low | angles | 269 | 60 | 0 0 0 / 0 1 0 / 0 101 0 / 0 105 0 / 0 120 0 |
| info_node_cover_low | classname | 269 | 1 | info_node_cover_low |
| info_node_cover_low | group | 45 | 5 | 1 / 10 / 2 / 3 / 4 |
| info_node_cover_low | group_id | 269 | 9 | 1 / 10 / 2 / 3 / 32 |
| info_node_cover_low | hint_rating | 269 | 7 | 0 / 1 / 2 / 3 / 4 |
| info_node_cover_low | hinttype | 269 | 1 | 101 |
| info_node_cover_low | nodeid | 269 | 118 | 0 / 1 / 10 / 100 / 101 |
| info_node_cover_low | origin | 269 | 267 | -0.235741 -2645.25 -123 / -1003 591 2851 / -1021 86 11 / -1025 1218 -6553 / -1036 -1460 225 |
| info_node_cover_low | starthidden | 21 | 2 | 0 / 1 |
| info_node_cover_low | starthintdisabled | 247 | 2 | 0 / 1 |
| info_node_cover_low | target_angle_range | 269 | 4 | 120 / 130 / 40 / 60 |
| info_node_cover_low | target_dist_max | 269 | 4 | 32000 / 4000 / 500 / 800 |
| info_node_cover_low | target_dist_min | 269 | 7 | 120 / 128 / 150 / 200 / 256 |
| info_node_cover_low | target_name | 32 | 16 | cellar_guard / guard guard_5 guard_west_quarters / guard1_conversation1 / guard_alarm / guard_front_door |
| info_node_cover_low | targetname | 40 | 32 | Bottleneck_Cover / breakable_cover_1 / breakable_cover_1000 / breakable_cover_14 / breakable_cover_17 |
| info_node_cover_med | angles | 431 | 115 | 0 0 0 / 0 101 0 / 0 103 0 / 0 103.5 0 / 0 105 0 |
| info_node_cover_med | classname | 431 | 1 | info_node_cover_med |
| info_node_cover_med | group | 35 | 7 | 1 / 10 / 2 / 3 / 4 |
| info_node_cover_med | group_id | 431 | 12 | 1 / 11 / 18 / 2 / 3 |
| info_node_cover_med | hint_rating | 431 | 6 | 2 / 3 / 4 / 5 / 6 |
| info_node_cover_med | hinttype | 431 | 1 | 100 |
| info_node_cover_med | nodeid | 431 | 151 | 0 / 1 / 10 / 102 / 105 |
| info_node_cover_med | origin | 431 | 414 | -100 646 -7420 / -1034.41 -1805.76 -109.518 / -1064 -1048 220 / -1071.8 -778.153 -314 / -1080 156 9 |
| info_node_cover_med | starthidden | 126 | 2 | 0 / 1 |
| info_node_cover_med | starthintdisabled | 418 | 2 | 0 / 1 |
| info_node_cover_med | target_angle_range | 431 | 4 | 120 / 140 / 60 / 90 |
| info_node_cover_med | target_dist_max | 431 | 6 | 2000 / 32000 / 4000 / 500 / 800 |
| info_node_cover_med | target_dist_min | 431 | 7 | 120 / 128 / 150 / 200 / 256 |
| info_node_cover_med | target_name | 79 | 27 | Zhao / basic_shooter_3 / guard / guard guard_4 / guard guard_patrol_1 |
| info_node_cover_med | targetname | 125 | 70 | IdolRoom_4_Cover / back_cover_a / breakable_cover_1 / breakable_cover_10 / breakable_cover_15 |
| info_node_crosswalk | angles | 22 | 1 | 0 0 0 |
| info_node_crosswalk | classname | 22 | 1 | info_node_crosswalk |
| info_node_crosswalk | hinttype | 22 | 1 | 11000 |
| info_node_crosswalk | nodeid | 22 | 14 | 0 / 1 / 2 / 268 / 269 |
| info_node_crosswalk | origin | 22 | 16 | -1000.62 180.968 -111 / -1054.1 479.756 -107.354 / -1299.27 495.43 -106.835 / -1349.05 426.796 -111 / -1351.74 181.176 -111 |
| info_node_crosswalk | starthintdisabled | 22 | 1 | 0 |
| info_node_crosswalk | targetname | 22 | 4 | crosswalk_east_west / crosswalk_south / xwalk_3 / xwalk_4 |
| info_node_hint | angles | 36 | 1 | 0 0 0 |
| info_node_hint | classname | 36 | 1 | info_node_hint |
| info_node_hint | group | 33 | 28 | 1 / A1 / A2 / A3 / A4 |
| info_node_hint | hinttype | 36 | 2 | 10000 / 10400 |
| info_node_hint | nodeid | 36 | 33 | 0 / 1 / 105 / 11 / 12 |
| info_node_hint | origin | 36 | 36 | -240 -448 9 / -240 385 9 / -245 -993 9 / -248 -576 -278 / -277 1023 273 |
| info_node_hint | starthidden | 2 | 1 | 1 |
| info_node_hint | starthintdisabled | 36 | 2 | 0 / 1 |
| info_node_hint | targetname | 5 | 4 | Truckyard_Guard_Teleport_1 / Truckyard_Guard_Teleport_2 / shoot_at_me / spotligh_target2 |
| info_node_kick_over | angles | 1 | 1 | 0 90 0 |
| info_node_kick_over | classname | 1 | 1 | info_node_kick_over |
| info_node_kick_over | group_id | 1 | 1 | 1 |
| info_node_kick_over | hint_rating | 1 | 1 | 6 |
| info_node_kick_over | hinttype | 1 | 1 | 10300 |
| info_node_kick_over | nodeid | 1 | 1 | 59 |
| info_node_kick_over | onnpckicked | 2 | 2 | table,RotateToDest,,0,-1,, / table_cover,EnableHint,,0,-1,, |
| info_node_kick_over | origin | 1 | 1 | 3570.47 -2987.31 -103 |
| info_node_kick_over | starthidden | 1 | 1 | 0 |
| info_node_kick_over | starthintdisabled | 1 | 1 | 0 |
| info_node_kick_over | target_angle_range | 1 | 1 | 60 |
| info_node_kick_over | target_dist_max | 1 | 1 | 32000 |
| info_node_kick_over | target_dist_min | 1 | 1 | 256 |
| info_node_kick_over | target_name | 1 | 1 | table |
| info_node_kick_over | targetname | 1 | 1 | kick_spot |
| info_node_link | classname | 6 | 1 | info_node_link |
| info_node_link | endnode | 6 | 6 | 18 / 19 / 22 / 23 / 26 |
| info_node_link | origin | 6 | 6 | -1705.23 704.463 -742 / -1707.88 1099.58 -742 / -1710.23 1474.42 -742 / -1803.87 705.355 -742 / -1806.52 1100.47 -742 |
| info_node_link | startnode | 6 | 6 | 16 / 17 / 20 / 21 / 24 |
| info_node_manbat_fly_to_point | angles | 13 | 1 | 0 0 0 |
| info_node_manbat_fly_to_point | classname | 13 | 1 | info_node_manbat_fly_to_point |
| info_node_manbat_fly_to_point | group_id | 13 | 1 | 1 |
| info_node_manbat_fly_to_point | hint_rating | 13 | 1 | 3 |
| info_node_manbat_fly_to_point | hinttype | 13 | 1 | 20000 |
| info_node_manbat_fly_to_point | nodeid | 13 | 13 | 1 / 10 / 11 / 12 / 13 |
| info_node_manbat_fly_to_point | origin | 13 | 13 | -2224 1536 896 / -224 1632 216 / -2240 1268.52 64 / -2240 1344 1196.64 / -868 1280 672 |
| info_node_manbat_fly_to_point | target_angle_range | 13 | 1 | 60 |
| info_node_manbat_fly_to_point | target_dist_max | 13 | 1 | 32000 |
| info_node_manbat_fly_to_point | target_dist_min | 13 | 1 | 256 |
| info_node_manbat_fly_to_point | targetname | 13 | 12 | ManBat 1 / ManBat 2 / ManBat 3 / ManBat 4 / ManBat 5 |
| info_node_patrol_point | angles | 582 | 27 | 0 0 0 / 0 102 0 / 0 109 0 / 0 147 0 / 0 148 0 |
| info_node_patrol_point | classname | 582 | 1 | info_node_patrol_point |
| info_node_patrol_point | group | 582 | 328 | A1 / A10 / A12 / A17 / A18 |
| info_node_patrol_point | group_id | 205 | 1 | 1 |
| info_node_patrol_point | hint_rating | 205 | 1 | 3 |
| info_node_patrol_point | hinttype | 582 | 1 | 10000 |
| info_node_patrol_point | ip_percent | 582 | 11 | 100 / 20 / 30 / 45 / 50 |
| info_node_patrol_point | nodeid | 582 | 200 | 0 / 1 / 10 / 100 / 101 |
| info_node_patrol_point | origin | 582 | 563 | -100 1000 577 / -100 1800 577 / -1000 476 -107 / -1003 1945.22 -107 / -1021 -170 2617 |
| info_node_patrol_point | starthidden | 61 | 1 | 0 |
| info_node_patrol_point | starthintdisabled | 515 | 1 | 0 |
| info_node_patrol_point | target_angle_range | 205 | 1 | 60 |
| info_node_patrol_point | target_dist_max | 205 | 1 | 32000 |
| info_node_patrol_point | target_dist_min | 205 | 1 | 256 |
| info_node_patrol_point | target_name | 60 | 35 | WorkOnWall / bob_rafters_end / bob_under_bed / cellar_ip_whistle / cellie_spot |
| info_node_patrol_point | targetname | 68 | 56 | d1 / d2 / d3 / d4 / d5 |
| info_node_sabbat_arch | angles | 15 | 1 | 0 0 0 |
| info_node_sabbat_arch | classname | 15 | 1 | info_node_sabbat_arch |
| info_node_sabbat_arch | group_id | 15 | 1 | 1 |
| info_node_sabbat_arch | hint_rating | 15 | 1 | 3 |
| info_node_sabbat_arch | hinttype | 15 | 1 | 16002 |
| info_node_sabbat_arch | nodeid | 15 | 15 | 114 / 115 / 117 / 118 / 119 |
| info_node_sabbat_arch | origin | 15 | 15 | 1137 979 -200.803 / 1267.18 170.076 -200.803 / 1270 982 -198 / 1390 981 -200.803 / 1393.31 169.593 -200.803 |
| info_node_sabbat_arch | starthintdisabled | 1 | 1 | 0 |
| info_node_sabbat_arch | target_angle_range | 15 | 1 | 60 |
| info_node_sabbat_arch | target_dist_max | 15 | 1 | 32000 |
| info_node_sabbat_arch | target_dist_min | 15 | 1 | 256 |
| info_node_sabbat_bottom | angles | 11 | 1 | 0 0 0 |
| info_node_sabbat_bottom | classname | 11 | 1 | info_node_sabbat_bottom |
| info_node_sabbat_bottom | group_id | 11 | 1 | 1 |
| info_node_sabbat_bottom | hint_rating | 11 | 1 | 3 |
| info_node_sabbat_bottom | hinttype | 11 | 1 | 16000 |
| info_node_sabbat_bottom | nodeid | 11 | 11 | 1 / 10 / 11 / 12 / 2 |
| info_node_sabbat_bottom | origin | 11 | 11 | 1259.46 434.653 -337.009 / 1260.52 703.943 -335.566 / 1363.51 456 -312 / 1468.16 681.3 -330.412 / 1492 459.473 -324 |
| info_node_sabbat_bottom | target_angle_range | 11 | 1 | 60 |
| info_node_sabbat_bottom | target_dist_max | 11 | 1 | 32000 |
| info_node_sabbat_bottom | target_dist_min | 11 | 1 | 256 |
| info_node_sabbat_dive | angles | 6 | 1 | 0 0 0 |
| info_node_sabbat_dive | classname | 6 | 1 | info_node_sabbat_dive |
| info_node_sabbat_dive | group_id | 6 | 1 | 1 |
| info_node_sabbat_dive | hint_rating | 6 | 1 | 3 |
| info_node_sabbat_dive | hinttype | 6 | 1 | 16005 |
| info_node_sabbat_dive | nodeid | 6 | 6 | 1 / 2 / 3 / 4 / 5 |
| info_node_sabbat_dive | origin | 6 | 6 | 1324.39 430.045 -337 / 1331.85 709.384 -337 / 1333.27 597.435 -337 / 1603.85 631.128 -337 / 1817 524.548 -319.305 |
| info_node_sabbat_dive | target_angle_range | 6 | 1 | 60 |
| info_node_sabbat_dive | target_dist_max | 6 | 1 | 32000 |
| info_node_sabbat_dive | target_dist_min | 6 | 1 | 256 |
| info_node_sabbat_hide | angles | 5 | 1 | 0 0 0 |
| info_node_sabbat_hide | classname | 5 | 1 | info_node_sabbat_hide |
| info_node_sabbat_hide | group_id | 5 | 1 | 1 |
| info_node_sabbat_hide | hint_rating | 5 | 1 | 3 |
| info_node_sabbat_hide | hinttype | 5 | 1 | 16003 |
| info_node_sabbat_hide | nodeid | 5 | 5 | 110 / 111 / 6 / 68 / 75 |
| info_node_sabbat_hide | origin | 5 | 5 | 1095 1013 -200.803 / 1110 214.017 -194 / 1924 194 -184 / 1940 983 -201 / 2164 350.19 -180 |
| info_node_sabbat_hide | starthintdisabled | 1 | 1 | 0 |
| info_node_sabbat_hide | target_angle_range | 5 | 1 | 60 |
| info_node_sabbat_hide | target_dist_max | 5 | 1 | 32000 |
| info_node_sabbat_hide | target_dist_min | 5 | 1 | 256 |
| info_node_sabbat_nojump | angles | 3 | 2 | 0 0 0 / 0 90 0 |
| info_node_sabbat_nojump | classname | 3 | 1 | info_node_sabbat_nojump |
| info_node_sabbat_nojump | group_id | 3 | 1 | 1 |
| info_node_sabbat_nojump | hint_rating | 3 | 1 | 3 |
| info_node_sabbat_nojump | hinttype | 3 | 1 | 16004 |
| info_node_sabbat_nojump | nodeid | 3 | 3 | 130 / 131 / 132 |
| info_node_sabbat_nojump | origin | 3 | 3 | 1184 842.909 -288 / 1246.47 327.031 -300.675 / 2000 440 -312 |
| info_node_sabbat_nojump | target_angle_range | 3 | 1 | 60 |
| info_node_sabbat_nojump | target_dist_max | 3 | 1 | 32000 |
| info_node_sabbat_nojump | target_dist_min | 3 | 1 | 256 |
| info_node_sabbat_top | angles | 12 | 1 | 0 0 0 |
| info_node_sabbat_top | classname | 12 | 1 | info_node_sabbat_top |
| info_node_sabbat_top | group_id | 12 | 1 | 1 |
| info_node_sabbat_top | hint_rating | 12 | 1 | 3 |
| info_node_sabbat_top | hinttype | 12 | 1 | 16001 |
| info_node_sabbat_top | nodeid | 12 | 12 | 77 / 78 / 79 / 80 / 81 |
| info_node_sabbat_top | origin | 12 | 12 | 1277 328 -183.321 / 1288 840 -183.038 / 1398.99 312.2 -180.803 / 1400 840 -180.803 / 1516 333 -180.803 |
| info_node_sabbat_top | target_angle_range | 12 | 1 | 60 |
| info_node_sabbat_top | target_dist_max | 12 | 1 | 32000 |
| info_node_sabbat_top | target_dist_min | 12 | 1 | 256 |
| info_node_shoot_at | angles | 20 | 16 | 0 11 0 / 0 110 0 / 0 144 0 / 0 156 0 / 0 189 0 |
| info_node_shoot_at | classname | 20 | 1 | info_node_shoot_at |
| info_node_shoot_at | group_id | 20 | 1 | 1 |
| info_node_shoot_at | hint_rating | 20 | 2 | 3 / 6 |
| info_node_shoot_at | hinttype | 20 | 1 | 10400 |
| info_node_shoot_at | nodeid | 20 | 15 | 0 / 1 / 2 / 28 / 3 |
| info_node_shoot_at | origin | 20 | 20 | -147.882 326.485 192 / -156.387 -21.3682 192 / -4643.71 3369 286.245 / -4855 3395 342 / -5041 3306 342 |
| info_node_shoot_at | starthidden | 11 | 2 | 0 / 1 |
| info_node_shoot_at | starthintdisabled | 12 | 2 | 0 / 1 |
| info_node_shoot_at | target_angle_range | 20 | 5 | 10 / 30 / 360 / 45 / 60 |
| info_node_shoot_at | target_dist_max | 20 | 1 | 32000 |
| info_node_shoot_at | target_dist_min | 20 | 2 | 1 / 256 |
| info_node_shoot_at | target_name | 8 | 1 | Yukie |
| info_node_shoot_at | targetname | 20 | 18 | Coolant_Target1 / Coolant_Target2 / Coolant_Target3 / Coolant_Target4 / Coolant_Target5 |
| info_node_tzimisce | angles | 1 | 1 | 0 0 0 |
| info_node_tzimisce | classname | 19 | 1 | info_node_tzimisce |
| info_node_tzimisce | nodeid | 19 | 19 | 1 / 10 / 11 / 12 / 14 |
| info_node_tzimisce | origin | 19 | 19 | -108.047 -220.68 -507 / -109.164 -75.168 -507 / -109.905 67.9205 -507 / -111.423 213.244 -507 / -61.661 -389.783 -508 |
| info_node_werewolf | classname | 80 | 1 | info_node_werewolf |
| info_node_werewolf | nodeid | 80 | 63 | 1 / 10 / 11 / 13 / 14 |
| info_node_werewolf | origin | 80 | 80 | -1018.05 -54.3004 132 / -1024 -1024 64 / -1024 -1344 64 / -1024 -1664 96 / -1048 88 132 |
| info_node_werewolf_hint | angles | 132 | 8 | 0 0 0 / 0 180 0 / 0 232 0 / 0 270 0 / 0 42 0 |
| info_node_werewolf_hint | classname | 132 | 1 | info_node_werewolf_hint |
| info_node_werewolf_hint | hinttype | 132 | 16 | 15000 / 15001 / 15002 / 15003 / 15004 |
| info_node_werewolf_hint | nodeid | 132 | 71 | 0 / 1 / 10 / 100 / 104 |
| info_node_werewolf_hint | onanimevent1 | 58 | 33 | breakable_stairs_brush_1,Break,,0,-1,, / breakable_stairs_brush_2,Break,,0,-1,, / breakable_stairs_brush_3,Break,,0,-1,, / door_a_1_relay,Trigger,,0,-1,, / door_a_2_relay,Trigger,,0,-1,, |
| info_node_werewolf_hint | origin | 132 | 132 | -1031 743 137.288 / -1031 743 166.288 / -1048 656 347.729 / -1068 367 166.288 / -1068 369 137.288 |
| info_node_werewolf_hint | starthidden | 32 | 1 | 1 |
| info_node_werewolf_hint | starthintdisabled | 132 | 2 | 0 / 1 |
| info_node_werewolf_hint | target_name | 132 | 92 | archway_a_1_endpoint / archway_a_2_endpoint / archway_a_3_endpoint / archway_a_4_endpoint / archway_a_5_endpoint |
| info_node_werewolf_hint | targetname | 129 | 117 | archway_a_1_squeeze_front / archway_a_2_squeeze_front / archway_a_3_squeeze_front / archway_a_4_squeeze_front / archway_a_5_squeeze_front |
| info_node_werewolf_hint | userdata | 4 | 2 | 200 / 300 |
| intersting_place | angles | 1293 | 251 | 0 0 0 / 0 1 0 / 0 10 0 / 0 100 0 / 0 101 0 |
| intersting_place | classname | 1293 | 1 | intersting_place |
| intersting_place | enabled | 1293 | 2 | 0 / 1 |
| intersting_place | group_id | 1293 | 35 | 1 / 10 / 11 / 12 / 13 |
| intersting_place | holster_weapon | 1011 | 2 | 0 / 1 |
| intersting_place | match_orientation | 1293 | 2 | 0 / 1 |
| intersting_place | max_bounds | 1293 | 40 | 0  0 72 / 100 100 128 / 128  32 72 / 128 32 72 / 16  112 72 |
| intersting_place | max_npcs | 1293 | 10 | 0 / 1 / 15 / 2 / 3 |
| intersting_place | max_time | 1293 | 68 | .5 / 0 / 1 / 10 / 10.0 |
| intersting_place | min_bounds | 1293 | 34 | -100 -100 0 / -128 -128 0 / -128 -16 0 / -128 -32 0 / -16 -16 0 |
| intersting_place | min_time | 1293 | 57 | .1 / .5 / 0 / 1 / 1.0 |
| intersting_place | onanimevent1 | 4 | 4 | ,,,0,-1,G.G1Health = G.G1Health +1, / ,,,0,-1,G.G2Health = G.G2Health +1, / gate_north,RemoveHealth,1,0,-1,, / gate_south,RemoveHealth,1,0,-1,, |
| intersting_place | onnpcarrived | 124 | 100 | ,,,0,-1,G.Misti_Follow = 3, / ,,,0,-1,G.T2NPCInplace = 1, / ,,,0,-1,WaitForPartner(-1), / ,,,0,-1,WaitForPartner(1), / ,,,0,-1,WaitForPartner(2), |
| intersting_place | onnpcarrived-wesp | 1 | 1 | ,,,0,-1,G.Misti_Follow = 1, |
| intersting_place | onnpcleft | 133 | 88 | ,,,0,-1,SetSamPostCall(), / ,,,0.5,-1,G.T2NPCInplace = 0, / BobInTheRafters,UseInteresting,1,0,-1,, / Boil,StopSound,,0,-1,, / CoffeeCup,StopSound,,0,-1,, |
| intersting_place | origin | 1293 | 1266 | -1 2732 -1522 / -1 3092 -256 / -1 3781 -191 / -1 457 192 / -10 2254 -7312 |
| intersting_place | rating | 1293 | 6 | 0 / 1 / 2 / 3 / 4 |
| intersting_place | spawnflags | 8 | 1 | 0 |
| intersting_place | starthidden | 103 | 2 | 0 / 1 |
| intersting_place | targetname | 745 | 321 | 17 / BackUp_1_IntPlc / BackUp_2_IntPlc / Chastity-break_int_place / Chastity_peepshw_3-int_place |
| intersting_place | testflags | 1293 | 1 | 4 |
| intersting_place | type | 1293 | 39 | Citizen_Idle / Dance / Doggy / Doorknock / Drama |
| intersting_place_conversation |  | 2 | 1 | 312 |
| intersting_place_conversation | angles | 7 | 1 | 0 0 0 |
| intersting_place_conversation | audible_dist | 49 | 13 | 00 / 10 / 1000 / 1200 / 1400 |
| intersting_place_conversation | classname | 49 | 1 | intersting_place_conversation |
| intersting_place_conversation | enabled | 49 | 2 | 0 / 1 |
| intersting_place_conversation | interesting_places | 49 | 43 | IntPlc_Conversation_1 / IntPlc_Conversation_2 / IntPlc_Conversation_3 / IntrestingPlace_Conversation_BigDialog / Node_Conversation_Card_Game |
| intersting_place_conversation | max_time | 49 | 7 | 10 / 10.0 / 15 / 15.0 / 20 |
| intersting_place_conversation | min_time | 49 | 6 | 10 / 3 / 5 / 5.0 / 8.0 |
| intersting_place_conversation | ononeoffsoundcomplete | 23 | 23 | IntPlc_Conversation_2,Disable,,44,-1,, / IntPlc_Conversation_3,Disable,,38,-1,, / conversation_spot_1,Disable,,0,-1,, / conversation_spot_2,Disable,,0,-1,, / conversation_spot_3,Disable,,0,-1,, |
| intersting_place_conversation | onplayertooclose | 3 | 3 | go_attack,Trigger,,0,-1,, / ip_after_boulder_1,Disable,,0,-1,, / ip_after_boulder_1,ScriptHide,,0,-1,, |
| intersting_place_conversation | origin | 49 | 47 | -100 -228 5 / -1039.21 513 -189.114 / -126 -700 12 / -1461 -5 -199 / -24 140 9 |
| intersting_place_conversation | player_dist | 49 | 12 | 0 / 09 / 10 / 100 / 176 |
| intersting_place_conversation | sound_loop | 35 | 7 | Environmental/People/Conversation1.wav / Environmental/People/Conversation2.wav / Environmental/People/Conversation4.wav / Environmental/People/Conversation5.wav / Environmental/People/Conversation6.wav |
| intersting_place_conversation | sound_occluded | 49 | 2 | 0 / 1 |
| intersting_place_conversation | sound_once | 20 | 1 | null.mp3 |
| intersting_place_conversation | targetname | 33 | 27 | IntPlc_Floor2_Game / ash_convo / bar_convo / barconvo1_convo / barconvo2_convo |
| intersting_place_conversation | turn_towards_talker | 49 | 2 | 0 / 1 |
| npc_maker | additionalequipment | 210 | 14 | item_w_avamp_blade / item_w_claws / item_w_colt_anaconda / item_w_crossbow / item_w_crossbow_flaming |
| npc_maker | allow_alert_lookaround | 196 | 2 | 0 / 1 |
| npc_maker | allow_kick_hint_use | 274 | 2 | 0 / 1 |
| npc_maker | alternateequipment | 90 | 3 | 0 / item_w_baton / item_w_knife |
| npc_maker | angles | 274 | 70 | 0 0 0 / 0 105 0 / 0 107 0 / 0 109 0 / 0 119 0 |
| npc_maker | base_gender | 64 | 1 | 1 |
| npc_maker | bright_route_penalty | 265 | 1 | 0 |
| npc_maker | cantdropweapons | 43 | 2 | 0 / 1 |
| npc_maker | classname | 274 | 1 | npc_maker |
| npc_maker | clothescolor1 | 274 | 1 | 0 0 0 0 |
| npc_maker | clothescolor2 | 274 | 1 | 0 0 0 0 |
| npc_maker | combat_start_activity | 274 | 2 | -1 / ACT_INVALID |
| npc_maker | crossfade_skin_time | 274 | 2 | 2.0 / 2.000 |
| npc_maker | default_camera | 274 | 1 | DialogDefault |
| npc_maker | default_disposition | 274 | 2 | Neutral / fear |
| npc_maker | demo_sequence | 255 | 1 | None |
| npc_maker | dialogname | 2 | 2 | dlg/Downtown LA/Bum_disease_male.dlg |
| npc_maker | disablereceiveshadows | 251 | 1 | 0 |
| npc_maker | disableshadows | 274 | 1 | 1 |
| npc_maker | flag_fade | 269 | 2 | 0 / 1 |
| npc_maker | flag_infchild | 269 | 2 | 0 / 1 |
| npc_maker | flag_nodrop | 265 | 2 | 0 / 1 |
| npc_maker | flag_npcclip | 268 | 2 | 0 / 1 |
| npc_maker | flag_startdisabled | 270 | 2 | 0 / 1 |
| npc_maker | flag_viewcone | 26 | 2 | 0 / 1 |
| npc_maker | floatfreq | 225 | 4 | 0 / 2 / 4 / 8 |
| npc_maker | follower_type | 274 | 1 | Default |
| npc_maker | full_investigate | 140 | 2 | 0 / 1 |
| npc_maker | haircolor | 274 | 1 | 0 0 0 0 |
| npc_maker | hearing | 274 | 10 | -1.0 / -1.00 / .3 / .6 / 0 |
| npc_maker | hint_groups | 274 | 6 | 0 / 1 / 2 / 8 9 / 9 |
| npc_maker | ignore_detected_attack | 6 | 1 | 0 |
| npc_maker | interesting_place_groups | 265 | 12 | 0 / 1 / 1 2 3 4 5 6 7 8 / 10 / 18 |
| npc_maker | investigate_mode | 274 | 2 | 3 / 4 |
| npc_maker | investigate_mode_combat | 274 | 2 | 3 / 4 |
| npc_maker | invincible | 120 | 1 | 0 |
| npc_maker | is_bossmonster | 6 | 1 | 0 |
| npc_maker | maxlivechildren | 274 | 5 | 1 / 2 / 20 / 5 / 6 |
| npc_maker | maxnpccount | 274 | 7 | 1 / 200 / 3 / 4 / 5 |
| npc_maker | minpcdistance | 1 | 1 | 0 |
| npc_maker | model | 274 | 27 | models/character/monster/rat/rat.mdl |
| npc_maker | nav_ignore_physics_props | 1 | 1 | 0 |
| npc_maker | no_alert_state | 6 | 1 | 0 |
| npc_maker | npc_perception | 274 | 8 | 1 / 10 / 3 / 4 / 5 |
| npc_maker | npc_transparent | 255 | 1 | 1 |
| npc_maker | npcsquadname | 118 | 36 | BackUp_1 / BackUp_2 / CopSquad / Cops / Deck2 |
| npc_maker | npctargetname | 259 | 79 | Backup_1_Spawned / Backup_2_Spawned / Botch_Guards_Spawned / Bum_disease_male / Cop |
| npc_maker | npctype | 267 | 11 | npc_VCop / npc_VDialogPedestrian / npc_VHuman / npc_VHumanCombatant / npc_VHunter |
| npc_maker | ondamaged | 4 | 4 | logic_on_stealth_dead,Disable,,0,-1,, / popup_50,OpenWindow,,0.5,-1,, |
| npc_maker | ondeath | 37 | 20 | ,,,0,-1,G.Tut_Aggfeed = 1, / ,,,0,-1,G.Tut_Ratfeed = 1, / ,,,0,-1,G.Tut_Stealthkill = 1, / ,,,0,-1,G.Tutorial_Blueblood = 2, / ,,,0,-1,OnKillDisc1(), |
| npc_maker | ondialogend | 2 | 2 | ,,,0,-1,SetBumsNotTalk(), / ,,,0,-1,testGuard(), |
| npc_maker | onfeduponbegin | 3 | 3 | ,,,0,-1,G.Tutorial_Bum = 1, / plague_feed_relay,Trigger,,5,-1,, / script_4b,BeginSequence,,0,1,, |
| npc_maker | onfeduponend | 5 | 5 | ,,,0,-1,G.Tutorial_Blueblood = 1, / rat,Kill,,0,-1,, / ratmaker,Spawn,,0.2,-1,, / trig_dialog_feed_on_rats,Enable,,0,-1,, |
| npc_maker | onfoundenemy | 7 | 5 | Relay_Boched_Level,Trigger,,0,-1,, |
| npc_maker | onfoundplayer | 47 | 12 | ,,,0,-1,G.Warehouse_Spotted = 1, / ,,,0,-1,t2botched(), / Bruno_Event,Trigger,,0,-1,, / Relay_Barracks_Charge,Trigger,,0,-1,, / Tub_Guy_1,FleeAndDie,,0,-1,, |
| npc_maker | onhearplayer | 1 | 1 |  |
| npc_maker | onincapacitatedend | 1 | 1 | ,,,0,-1,G.Sabbat_Incapacitated = 0, |
| npc_maker | onincapacitatedstart | 1 | 1 | ,,,0,-1,G.Sabbat_Incapacitated = 1, |
| npc_maker | onlastnpcdied | 5 | 5 | bum_maker_1,Spawn,,0,-1,, / rope_1*,Break,,0,-1,, / rope_2*,ScriptHide,,0,-1,, / rope_3*,ScriptHide,,0,-1,, / rope_5*,ScriptHide,,0,-1,, |
| npc_maker | onlostplayer | 12 | 1 |  |
| npc_maker | onnpcdied | 16 | 4 | ,,,0,-1,huntersDead[0] = 1, / ,,,0,-1,huntersDead[1] = 1, / ,,,0,-1,huntersDead[2] = 1, / h_kill_counter,Subtract,1,0,-1,, |
| npc_maker | onspawnnpc | 136 | 34 | ,,,0.5,-1,SetBumsTalk(), / backup,Kill,,5,-1,, / first_floor_guard,Kill,,5,-1,, / guard_to_nurse,StartSchedule,,0,-1,, / h_attack_1,StartSchedule,,1,-1,, |
| npc_maker | origin | 274 | 271 | -1039.88 -1807 -117 / -1084 -970 -5878.77 / -1087.89 -2143.94 -110.567 / -1091.73 -1933 -120 / -1134.2 739.39 -110.67 |
| npc_maker | percent_occluded_chase | 274 | 4 | 100 / 30 / 35 / 70 |
| npc_maker | percent_occluded_cover | 274 | 3 | 0 / 20 / 30 |
| npc_maker | percent_occluded_flank | 274 | 4 | 0 / 10 / 20 / 30 |
| npc_maker | percent_occluded_wait | 274 | 3 | 0 / 10 / 5 |
| npc_maker | percent_occluded_walk | 274 | 3 | 0 / 10 / 30 |
| npc_maker | physdamagescale | 274 | 2 | 1.0 / 1.000 |
| npc_maker | pl_criminal_attack | 274 | 4 | -1 / 1 / 2 / 6 |
| npc_maker | pl_criminal_flee | 274 | 5 | -1 / 2 / 4 / 5 / 6 |
| npc_maker | pl_investigate | 274 | 3 | -1 / 1 / 6 |
| npc_maker | pl_supernatural_attack | 274 | 5 | -1 / 1 / 2 / 4 / 6 |
| npc_maker | pl_supernatural_flee | 274 | 5 | -1 / 3 / 4 / 5 / 6 |
| npc_maker | player_reaction | 274 | 3 | D_HT 10 / D_HT 5 / D_NU 0 |
| npc_maker | renderamt | 255 | 1 | 255 |
| npc_maker | rendercolor | 255 | 1 | 255 255 255 |
| npc_maker | renderfx | 251 | 1 | 0 |
| npc_maker | rendermode | 251 | 1 | 0 |
| npc_maker | skin | 265 | 4 | 0 / 1 / 2 / 3 |
| npc_maker | skincolor | 274 | 1 | 0 0 0 0 |
| npc_maker | soundgroup | 16 | 5 | Asian / Young_Thug / bum_disease_male / officer/officer_2 / young_thug |
| npc_maker | spawnflags | 274 | 9 | 0 / 12388 / 16388 / 32804 / 36 |
| npc_maker | spawnfrequency | 274 | 13 | -1 / 1 / 10 / 120 / 15 |
| npc_maker | squadname | 87 | 33 | BackUp_1 / BackUp_2 / CopSquad / Front_Guard / Kitchen_Guards |
| npc_maker | starthidden | 129 | 2 | 0 / 1 |
| npc_maker | stattemplate | 274 | 31 | BloodHuntFortitude / BloodHuntPresence / BloodHuntProtean / BluebloodFastfood / BumFastFood |
| npc_maker | stay_entrenched | 142 | 2 | 0 / 1 |
| npc_maker | targetname | 274 | 136 | BackUp_1_Spawner / BackUp_2_Spawner / CopGenerator / Guard_Charge_Spawner1 / Guard_Charge_Spawner2 |
| npc_maker | team_name | 2 | 1 | MingXiao |
| npc_maker | team_name-wesp | 40 | 6 | cops / giovanni / temple / thugs / tong |
| npc_maker | teleport_move_timer | 89 | 1 | 0 |
| npc_maker | trimcolor | 274 | 1 | 0 0 0 0 |
| npc_maker | use_interesting | 265 | 2 | 0 / 1 |
| npc_maker | vision | 274 | 8 | -1 / 0 / 1000 / 1500 / 3600 |
| npc_maker_fleshpile | allow_alert_lookaround | 8 | 2 | 0 / 1 |
| npc_maker_fleshpile | allow_kick_hint_use | 12 | 1 | 1 |
| npc_maker_fleshpile | angles | 12 | 5 | 0 0 0 / 0 1 0 / 0 180 0 / 0 270 0 / 0 90 0 |
| npc_maker_fleshpile | base_gender | 6 | 1 | 1 |
| npc_maker_fleshpile | bright_route_penalty | 4 | 1 | 0 |
| npc_maker_fleshpile | cantdropweapons | 3 | 1 | 0 |
| npc_maker_fleshpile | classname | 12 | 1 | npc_maker_fleshpile |
| npc_maker_fleshpile | clothescolor1 | 12 | 1 | 0 0 0 0 |
| npc_maker_fleshpile | clothescolor2 | 12 | 1 | 0 0 0 0 |
| npc_maker_fleshpile | combat_start_activity | 12 | 2 | -1 / ACT_INVALID |
| npc_maker_fleshpile | crossfade_skin_time | 12 | 1 | 2.0 |
| npc_maker_fleshpile | default_camera | 12 | 1 | DialogDefault |
| npc_maker_fleshpile | default_disposition | 12 | 1 | Neutral |
| npc_maker_fleshpile | demo_sequence | 6 | 1 | None |
| npc_maker_fleshpile | disablereceiveshadows | 2 | 1 | 0 |
| npc_maker_fleshpile | disableshadows | 12 | 1 | 1 |
| npc_maker_fleshpile | flag_fade | 12 | 1 | 0 |
| npc_maker_fleshpile | flag_infchild | 12 | 2 | 0 / 1 |
| npc_maker_fleshpile | flag_isglobalspawner | 8 | 1 | 0 |
| npc_maker_fleshpile | flag_nodrop | 12 | 2 | 0 / 1 |
| npc_maker_fleshpile | flag_npcclip | 12 | 1 | 0 |
| npc_maker_fleshpile | flag_startdisabled | 12 | 2 | 0 / 1 |
| npc_maker_fleshpile | flag_useglobalspawnersettings | 8 | 1 | 0 |
| npc_maker_fleshpile | flag_zombieaitype | 8 | 1 | 0 |
| npc_maker_fleshpile | floatfreq | 8 | 2 | 0 / 2 |
| npc_maker_fleshpile | follower_type | 12 | 1 | Default |
| npc_maker_fleshpile | full_investigate | 4 | 1 | 0 |
| npc_maker_fleshpile | haircolor | 12 | 1 | 0 0 0 0 |
| npc_maker_fleshpile | hearing | 12 | 1 | -1.00 |
| npc_maker_fleshpile | hint_groups | 12 | 1 |  |
| npc_maker_fleshpile | ignore_detected_attack | 3 | 1 | 0 |
| npc_maker_fleshpile | interesting_place_groups | 4 | 1 | 0 |
| npc_maker_fleshpile | investigate_mode | 12 | 1 | 4 |
| npc_maker_fleshpile | investigate_mode_combat | 12 | 1 | 4 |
| npc_maker_fleshpile | invincible | 4 | 1 | 0 |
| npc_maker_fleshpile | is_bossmonster | 3 | 1 | 0 |
| npc_maker_fleshpile | maxlivechildren | 12 | 2 | 1 / 5 |
| npc_maker_fleshpile | maxnpccount | 12 | 2 | 1 / 9999 |
| npc_maker_fleshpile | model | 12 | 1 |  |
| npc_maker_fleshpile | no_alert_state | 3 | 1 | 0 |
| npc_maker_fleshpile | npc_perception | 12 | 2 | 10 / 3 |
| npc_maker_fleshpile | npc_transparent | 6 | 1 | 1 |
| npc_maker_fleshpile | npcsquadname | 12 | 4 | final_squad / runners / tz / tz4 |
| npc_maker_fleshpile | npctargetname | 6 | 2 | tzim_creation3 / xxx |
| npc_maker_fleshpile | npctype | 12 | 1 | npc_VTzimisceRunner |
| npc_maker_fleshpile | origin | 12 | 8 | -1245.25 3651.69 -759 / -1393.66 -3627.95 -999.57 / -719.088 -233.008 -235 / -727.088 243.992 -238 / -944 -3616 -999.57 |
| npc_maker_fleshpile | percent_occluded_chase | 12 | 1 | 30 |
| npc_maker_fleshpile | percent_occluded_cover | 12 | 1 | 30 |
| npc_maker_fleshpile | percent_occluded_flank | 12 | 1 | 20 |
| npc_maker_fleshpile | percent_occluded_wait | 12 | 1 | 10 |
| npc_maker_fleshpile | percent_occluded_walk | 12 | 1 | 10 |
| npc_maker_fleshpile | physdamagescale | 12 | 1 | 1.0 |
| npc_maker_fleshpile | pl_criminal_attack | 12 | 2 | -1 / 6 |
| npc_maker_fleshpile | pl_criminal_flee | 12 | 2 | -1 / 6 |
| npc_maker_fleshpile | pl_investigate | 12 | 2 | -1 / 6 |
| npc_maker_fleshpile | pl_supernatural_attack | 12 | 2 | -1 / 6 |
| npc_maker_fleshpile | pl_supernatural_flee | 12 | 2 | -1 / 6 |
| npc_maker_fleshpile | player_reaction | 12 | 2 | D_HT 10 / D_NU 0 |
| npc_maker_fleshpile | renderamt | 6 | 1 | 255 |
| npc_maker_fleshpile | rendercolor | 6 | 1 | 255 255 255 |
| npc_maker_fleshpile | renderfx | 2 | 1 | 0 |
| npc_maker_fleshpile | rendermode | 2 | 1 | 0 |
| npc_maker_fleshpile | skin | 4 | 1 | 0 |
| npc_maker_fleshpile | skincolor | 12 | 1 | 0 0 0 0 |
| npc_maker_fleshpile | spawnflags | 12 | 2 | 0 / 4 |
| npc_maker_fleshpile | spawnfrequency | 12 | 2 | 2 / 5 |
| npc_maker_fleshpile | squadname | 3 | 2 | final_squad / tz |
| npc_maker_fleshpile | stattemplate | 12 | 2 | HeadRunners / TzimisceCreation3 |
| npc_maker_fleshpile | stay_entrenched | 4 | 1 | 0 |
| npc_maker_fleshpile | targetname | 12 | 7 | Final_Spawners / Spawner1 / Spawner2 / fleshpile1 / fleshpile2 |
| npc_maker_fleshpile | teleport_move_timer | 3 | 1 | 0 |
| npc_maker_fleshpile | trimcolor | 12 | 1 | 0 0 0 0 |
| npc_maker_fleshpile | use_interesting | 4 | 1 | 0 |
| npc_maker_fleshpile | vision | 12 | 2 | -1 / 1500 |
| npc_maker_zombie | additionalequipment | 106 | 3 | item_w_fists / item_w_fists_zombie / item_w_zombie_fists |
| npc_maker_zombie | allow_alert_lookaround | 114 | 1 | 0 |
| npc_maker_zombie | allow_kick_hint_use | 114 | 2 | 0 / 1 |
| npc_maker_zombie | angles | 114 | 32 | 0 0 0 / 0 105 0 / 0 105.5 0 / 0 135 0 / 0 14 0 |
| npc_maker_zombie | base_gender | 3 | 1 | 1 |
| npc_maker_zombie | bright_route_penalty | 114 | 1 | 0 |
| npc_maker_zombie | cantdropweapons | 104 | 2 | 0 / 1 |
| npc_maker_zombie | classname | 114 | 1 | npc_maker_zombie |
| npc_maker_zombie | clothescolor1 | 114 | 1 | 0 0 0 0 |
| npc_maker_zombie | clothescolor2 | 114 | 1 | 0 0 0 0 |
| npc_maker_zombie | combat_start_activity | 114 | 2 | -1 / ACT_INVALID |
| npc_maker_zombie | crossfade_skin_time | 114 | 1 | 2.0 |
| npc_maker_zombie | default_camera | 114 | 1 | DialogDefault |
| npc_maker_zombie | default_disposition | 114 | 1 | Neutral |
| npc_maker_zombie | demo_sequence | 111 | 1 | None |
| npc_maker_zombie | disablereceiveshadows | 111 | 2 | 0 / 1 |
| npc_maker_zombie | disableshadows | 114 | 2 | 0 / 1 |
| npc_maker_zombie | flag_fade | 114 | 2 | 0 / 1 |
| npc_maker_zombie | flag_infchild | 114 | 2 | 0 / 1 |
| npc_maker_zombie | flag_isglobalspawner | 20 | 1 | 0 |
| npc_maker_zombie | flag_nodrop | 114 | 1 | 0 |
| npc_maker_zombie | flag_npcclip | 114 | 2 | 0 / 1 |
| npc_maker_zombie | flag_startdisabled | 114 | 2 | 0 / 1 |
| npc_maker_zombie | flag_useglobalspawnersettings | 20 | 1 | 0 |
| npc_maker_zombie | flag_viewcone | 38 | 1 | 1 |
| npc_maker_zombie | flag_zombieaitype | 114 | 2 | 1 / 2 |
| npc_maker_zombie | floatfreq | 114 | 2 | 0 / 5 |
| npc_maker_zombie | follower_type | 114 | 1 | Default |
| npc_maker_zombie | full_investigate | 113 | 1 | 0 |
| npc_maker_zombie | haircolor | 114 | 1 | 0 0 0 0 |
| npc_maker_zombie | hearing | 114 | 3 | -1 / -1.0 / -1.00 |
| npc_maker_zombie | hint_groups | 114 | 2 | 32 |
| npc_maker_zombie | interesting_place_groups | 114 | 5 | 0 / 1 / 18 / 19 / 32 |
| npc_maker_zombie | investigate_mode | 114 | 1 | 4 |
| npc_maker_zombie | investigate_mode_combat | 114 | 1 | 4 |
| npc_maker_zombie | invincible | 110 | 1 | 0 |
| npc_maker_zombie | maxlivechildren | 114 | 4 | 0 / 1 / 2 / 4 |
| npc_maker_zombie | maxnpccount | 114 | 6 | 1 / 10 / 24 / 3 / 4 |
| npc_maker_zombie | minpcdistance | 27 | 1 | 0 |
| npc_maker_zombie | model | 114 | 7 |  |
| npc_maker_zombie | npc_perception | 114 | 1 | 3 |
| npc_maker_zombie | npc_transparent | 111 | 1 | 1 |
| npc_maker_zombie | npcsquadname | 114 | 7 | MainHall_Guards / Zombies / Zombies2 / spawned_zombies / zombie_squad |
| npc_maker_zombie | npctargetname | 114 | 5 | Zombie / Zombie_Spawned / floor1_plague_victim / floor2_plague_victim / zombie |
| npc_maker_zombie | npctype | 114 | 1 | npc_VZombie |
| npc_maker_zombie | ondamaged | 17 | 2 | Relay_Kill_Nadia,Trigger,,0,-1,, / Relay_Nadia_Flees,Trigger,,0,-1,, |
| npc_maker_zombie | ondeath | 17 | 5 | ,,,0,-1,gio3_checkAllZombieDead(), / spell_1,ScriptUnhide,,0,-1,, / spell_2,ScriptUnhide,,0,-1,, / spell_3,ScriptUnhide,,0,-1,, / spell_4,ScriptUnhide,,0,-1,, |
| npc_maker_zombie | onspawnnpc | 8 | 4 | Zombie_Spawner_1_Relay,Trigger,,0,-1,, / Zombie_Spawner_2_Relay,Trigger,,0,-1,, / Zombie_Spawner_3_Relay,Trigger,,0,-1,, / Zombie_Spawner_4_Relay,Trigger,,0,-1,, |
| npc_maker_zombie | origin | 114 | 110 | -1040 -512 232 / -1264 -336 220 / -132 -916 220 / -1368 -232 220 / -1485 -897 220 |
| npc_maker_zombie | percent_occluded_chase | 114 | 2 | 100 / 30 |
| npc_maker_zombie | percent_occluded_cover | 114 | 2 | 0 / 30 |
| npc_maker_zombie | percent_occluded_flank | 114 | 2 | 0 / 20 |
| npc_maker_zombie | percent_occluded_wait | 114 | 2 | 0 / 10 |
| npc_maker_zombie | percent_occluded_walk | 114 | 2 | 0 / 10 |
| npc_maker_zombie | physdamagescale | 114 | 1 | 1.0 |
| npc_maker_zombie | pl_criminal_attack | 114 | 2 | -1 / 6 |
| npc_maker_zombie | pl_criminal_flee | 114 | 2 | -1 / 6 |
| npc_maker_zombie | pl_investigate | 114 | 2 | -1 / 6 |
| npc_maker_zombie | pl_supernatural_attack | 114 | 2 | -1 / 6 |
| npc_maker_zombie | pl_supernatural_flee | 114 | 2 | -1 / 6 |
| npc_maker_zombie | player_reaction | 114 | 3 | D_HT 0 / D_HT 5 / D_NU 0 |
| npc_maker_zombie | remove_distance | 114 | 5 | 1024.0 / 1500 / 3000 / 600 / 9999 |
| npc_maker_zombie | renderamt | 111 | 1 | 255 |
| npc_maker_zombie | rendercolor | 111 | 1 | 255 255 255 |
| npc_maker_zombie | renderfx | 111 | 1 | 0 |
| npc_maker_zombie | rendermode | 111 | 1 | 0 |
| npc_maker_zombie | should_ragdoll | 114 | 1 | 1 |
| npc_maker_zombie | skin | 114 | 1 | 0 |
| npc_maker_zombie | skincolor | 114 | 1 | 0 0 0 0 |
| npc_maker_zombie | spawnflags | 114 | 2 | 0 / 4 |
| npc_maker_zombie | spawnfrequency | 114 | 6 | 0 / 1 / 10 / 20 / 5 |
| npc_maker_zombie | squadname | 84 | 5 | MainHall_Guards / Zombies / Zombies2 / zombie_squad_female / zombie_squad_male |
| npc_maker_zombie | starthidden | 37 | 2 | 0 / 1 |
| npc_maker_zombie | stattemplate | 114 | 2 | CrackhousePlagueVictim / Zombie |
| npc_maker_zombie | stay_entrenched | 114 | 1 | 0 |
| npc_maker_zombie | targetname | 114 | 19 | Zombie_Makers / Zombie_Makers2 / Zombie_Spawner_1 / Zombie_Spawner_2 / Zombie_Spawner_3 |
| npc_maker_zombie | team_name | 3 | 1 | zombies |
| npc_maker_zombie | team_name-wesp | 89 | 3 | giovanni / plague_victim_damage_team / zombies |
| npc_maker_zombie | teleport_move_timer | 110 | 1 | 0 |
| npc_maker_zombie | trimcolor | 114 | 1 | 0 0 0 0 |
| npc_maker_zombie | use_interesting | 114 | 2 | 0 / 1 |
| npc_maker_zombie | vision | 114 | 2 | -1 / 400 |
| npc_vcamera | additionalequipment | 13 | 1 | weapon_smg1 |
| npc_vcamera | allow_alert_lookaround | 83 | 2 | 0 / 1 |
| npc_vcamera | allow_kick_hint_use | 84 | 1 | 1 |
| npc_vcamera | angles | 84 | 32 | 0 0 0 / 0 104 0 / 0 128 0 / 0 130 0 / 0 142 0 |
| npc_vcamera | base_gender | 24 | 1 | 1 |
| npc_vcamera | bright_route_penalty | 79 | 1 | 0 |
| npc_vcamera | cantdropweapons | 25 | 1 | 0 |
| npc_vcamera | classname | 84 | 1 | npc_VCamera |
| npc_vcamera | clothescolor1 | 84 | 1 | 0 0 0 0 |
| npc_vcamera | clothescolor2 | 84 | 1 | 0 0 0 0 |
| npc_vcamera | combat_start_activity | 84 | 2 | -1 / ACT_INVALID |
| npc_vcamera | crossfade_skin_time | 84 | 2 | 2 / 2.0 |
| npc_vcamera | default_camera | 84 | 1 | DialogDefault |
| npc_vcamera | default_disposition | 84 | 1 | Neutral |
| npc_vcamera | demo_sequence | 69 | 1 | None |
| npc_vcamera | disablereceiveshadows | 68 | 1 | 0 |
| npc_vcamera | disableshadows | 84 | 1 | 1 |
| npc_vcamera | floatfreq | 83 | 2 | 0 / 2 |
| npc_vcamera | follower_type | 84 | 1 | Default |
| npc_vcamera | full_investigate | 78 | 1 | 0 |
| npc_vcamera | haircolor | 84 | 1 | 0 0 0 0 |
| npc_vcamera | hearing | 84 | 7 | -1 / -1.0 / -1.00 / 0 / 0.10 |
| npc_vcamera | hint_groups | 84 | 2 | 1 |
| npc_vcamera | ignore_detected_attack | 8 | 1 | 0 |
| npc_vcamera | interesting_place_groups | 79 | 1 | 0 |
| npc_vcamera | investigate_mode | 84 | 2 | 3 / 4 |
| npc_vcamera | investigate_mode_combat | 84 | 2 | 3 / 4 |
| npc_vcamera | invincible | 61 | 1 | 0 |
| npc_vcamera | is_bossmonster | 8 | 1 | 0 |
| npc_vcamera | no_alert_state | 8 | 1 | 0 |
| npc_vcamera | npc_perception | 84 | 5 | 1 / 10 / 3 / 5 / 9 |
| npc_vcamera | npc_transparent | 69 | 1 | 1 |
| npc_vcamera | onfoundplayer | 9 | 7 | Bruno_Event,Trigger,,0,-1,, / Sequence_Hey,BeginSequence,,0,-1,, / stealth failed,Trigger,,0,-1,, |
| npc_vcamera | origin | 84 | 80 | -1007 1310 240 / -1023 91 39 / -1044 900 313 / -1063 708 302 / -1136 -392 5 |
| npc_vcamera | percent_occluded_chase | 84 | 1 | 30 |
| npc_vcamera | percent_occluded_cover | 84 | 1 | 30 |
| npc_vcamera | percent_occluded_flank | 84 | 1 | 20 |
| npc_vcamera | percent_occluded_wait | 84 | 1 | 10 |
| npc_vcamera | percent_occluded_walk | 84 | 1 | 10 |
| npc_vcamera | physdamagescale | 84 | 1 | 1.0 |
| npc_vcamera | pl_criminal_attack | 84 | 2 | -1 / 6 |
| npc_vcamera | pl_criminal_flee | 84 | 2 | -1 / 6 |
| npc_vcamera | pl_investigate | 84 | 3 | -1 / 1 / 6 |
| npc_vcamera | pl_supernatural_attack | 84 | 3 | -1 / 1 / 6 |
| npc_vcamera | pl_supernatural_flee | 84 | 2 | -1 / 6 |
| npc_vcamera | player_reaction | 84 | 4 | D_HT 0 / D_HT 10 / D_HT 5 / D_NU 0 |
| npc_vcamera | renderamt | 84 | 1 | 255 |
| npc_vcamera | rendercolor | 84 | 1 | 255 255 255 |
| npc_vcamera | renderfx | 79 | 1 | 0 |
| npc_vcamera | rendermode | 79 | 1 | 0 |
| npc_vcamera | skin | 79 | 1 | 0 |
| npc_vcamera | skincolor | 84 | 1 | 0 0 0 0 |
| npc_vcamera | spawnflags | 84 | 2 | 0 / 4 |
| npc_vcamera | squadname | 84 | 39 | BackUp_1 / BackUp_2 / CopSquad / Deck4 / Front_Guard |
| npc_vcamera | starthidden | 50 | 2 | 0 / 1 |
| npc_vcamera | stattemplate | 84 | 1 | NPCGeneric |
| npc_vcamera | stay_entrenched | 79 | 1 | 0 |
| npc_vcamera | targetname | 64 | 37 | BackUp_1_Cammera / BackUp_2_Cammera / Barracks_Precog_cam / Botch2_Precog_Cam / Cam_Basement_tong |
| npc_vcamera | teleport_move_timer | 55 | 1 | 0 |
| npc_vcamera | trimcolor | 84 | 1 | 0 0 0 0 |
| npc_vcamera | use_interesting | 79 | 1 | 0 |
| npc_vcamera | vision | 84 | 11 | -1 / 0 / 1000 / 1500 / 2400 |
| npc_vcamerasecurity | allow_alert_lookaround | 5 | 2 | 0 / 1 |
| npc_vcamerasecurity | allow_kick_hint_use | 5 | 2 | 0 / 1 |
| npc_vcamerasecurity | angles | 5 | 3 | 0 0 0 / 0 180 0 / 0 270 0 |
| npc_vcamerasecurity | base_gender | 5 | 1 | 1 |
| npc_vcamerasecurity | bright_route_penalty | 5 | 1 | 0 |
| npc_vcamerasecurity | classname | 5 | 1 | npc_VCameraSecurity |
| npc_vcamerasecurity | clothescolor1 | 5 | 1 | 0 0 0 0 |
| npc_vcamerasecurity | clothescolor2 | 5 | 1 | 0 0 0 0 |
| npc_vcamerasecurity | combat_start_activity | 5 | 2 | -1 / ACT_IDLE |
| npc_vcamerasecurity | crossfade_skin_time | 5 | 1 | 2.0 |
| npc_vcamerasecurity | default_camera | 5 | 1 | DialogDefault |
| npc_vcamerasecurity | default_disposition | 5 | 1 | Neutral |
| npc_vcamerasecurity | disableshadows | 5 | 1 | 1 |
| npc_vcamerasecurity | floatfreq | 5 | 1 | 0 |
| npc_vcamerasecurity | follower_type | 5 | 1 | Default |
| npc_vcamerasecurity | full_investigate | 5 | 1 | 0 |
| npc_vcamerasecurity | haircolor | 5 | 1 | 0 0 0 0 |
| npc_vcamerasecurity | hearing | 5 | 1 | 0 |
| npc_vcamerasecurity | hint_groups | 5 | 1 |  |
| npc_vcamerasecurity | interesting_place_groups | 5 | 1 | 0 |
| npc_vcamerasecurity | investigate_mode | 5 | 1 | 4 |
| npc_vcamerasecurity | investigate_mode_combat | 5 | 1 | 4 |
| npc_vcamerasecurity | linked_camera | 5 | 5 | sec_cam_1 / sec_cam_2 / sec_cam_3 / sec_cam_4 / sec_cam_5 |
| npc_vcamerasecurity | npc_perception | 5 | 1 | 3 |
| npc_vcamerasecurity | origin | 5 | 5 | -284 -26 -188 / 1106 -432 -184 / 1324 -1162 -188 / 332 -462 -183.948 / 44 -1412 -187 |
| npc_vcamerasecurity | percent_occluded_chase | 5 | 2 | 0 / 30 |
| npc_vcamerasecurity | percent_occluded_cover | 5 | 2 | 0 / 30 |
| npc_vcamerasecurity | percent_occluded_flank | 5 | 2 | 0 / 20 |
| npc_vcamerasecurity | percent_occluded_wait | 5 | 2 | 10 / 100 |
| npc_vcamerasecurity | percent_occluded_walk | 5 | 2 | 0 / 10 |
| npc_vcamerasecurity | physdamagescale | 5 | 1 | 1.0 |
| npc_vcamerasecurity | pl_criminal_attack | 5 | 2 | 1 / 6 |
| npc_vcamerasecurity | pl_criminal_flee | 5 | 1 | 6 |
| npc_vcamerasecurity | pl_investigate | 5 | 2 | 1 / 6 |
| npc_vcamerasecurity | pl_supernatural_attack | 5 | 2 | 1 / 6 |
| npc_vcamerasecurity | pl_supernatural_flee | 5 | 1 | 6 |
| npc_vcamerasecurity | player_reaction | 5 | 1 | D_HT 5 |
| npc_vcamerasecurity | renderamt | 5 | 1 | 255 |
| npc_vcamerasecurity | rendercolor | 5 | 1 | 255 255 255 |
| npc_vcamerasecurity | renderfx | 5 | 1 | 0 |
| npc_vcamerasecurity | rendermode | 5 | 1 | 0 |
| npc_vcamerasecurity | skin | 5 | 1 | 0 |
| npc_vcamerasecurity | skincolor | 5 | 1 | 0 0 0 0 |
| npc_vcamerasecurity | spawnflags | 5 | 1 | 0 |
| npc_vcamerasecurity | squadname | 5 | 1 | squad_basement |
| npc_vcamerasecurity | starthidden | 1 | 1 | 0 |
| npc_vcamerasecurity | stattemplate | 5 | 1 | NPCGeneric |
| npc_vcamerasecurity | stay_entrenched | 5 | 1 | 0 |
| npc_vcamerasecurity | targetname | 5 | 5 | sec_cam_1_npc / sec_cam_2_npc / sec_cam_3_npc / sec_cam_4_npc / sec_cam_5_npc |
| npc_vcamerasecurity | trimcolor | 5 | 1 | 0 0 0 0 |
| npc_vcamerasecurity | use_interesting | 5 | 1 | 0 |
| npc_vcamerasecurity | vision | 5 | 4 | 1024 / 512 / 750 / 900 |
| npc_vchangbrosblade | additionalequipment | 2 | 2 | item_w_chang_blade / item_w_katana |
| npc_vchangbrosblade | allow_alert_lookaround | 2 | 1 | 1 |
| npc_vchangbrosblade | allow_kick_hint_use | 2 | 1 | 1 |
| npc_vchangbrosblade | angles | 2 | 1 | 0 0 0 |
| npc_vchangbrosblade | cantdropweapons | 2 | 2 | 0 / 1 |
| npc_vchangbrosblade | classname | 2 | 1 | npc_VChangBrosBlade |
| npc_vchangbrosblade | clothescolor1 | 2 | 1 | 0 0 0 0 |
| npc_vchangbrosblade | clothescolor2 | 2 | 1 | 0 0 0 0 |
| npc_vchangbrosblade | combat_start_activity | 2 | 1 | -1 |
| npc_vchangbrosblade | crossfade_skin_time | 2 | 1 | 2.0 |
| npc_vchangbrosblade | default_camera | 2 | 1 | ChangDialog |
| npc_vchangbrosblade | default_disposition | 2 | 1 | Neutral |
| npc_vchangbrosblade | demo_sequence | 2 | 1 | None |
| npc_vchangbrosblade | disableshadows | 2 | 1 | 1 |
| npc_vchangbrosblade | follower_type | 2 | 1 | Default |
| npc_vchangbrosblade | haircolor | 2 | 1 | 0 0 0 0 |
| npc_vchangbrosblade | hearing | 2 | 1 | 3.00 |
| npc_vchangbrosblade | hint_groups | 2 | 1 |  |
| npc_vchangbrosblade | investigate_mode | 2 | 1 | 4 |
| npc_vchangbrosblade | investigate_mode_combat | 2 | 1 | 4 |
| npc_vchangbrosblade | is_bossmonster | 2 | 1 | 1 |
| npc_vchangbrosblade | model | 2 | 1 |  |
| npc_vchangbrosblade | npc_perception | 2 | 1 | 10 |
| npc_vchangbrosblade | npc_transparent | 2 | 1 | 1 |
| npc_vchangbrosblade | ondeath | 2 | 1 | chang_death_counter,Add,1,0,-1,, |
| npc_vchangbrosblade | origin | 2 | 1 | -100 52 9.42065 |
| npc_vchangbrosblade | percent_occluded_chase | 2 | 1 | 30 |
| npc_vchangbrosblade | percent_occluded_cover | 2 | 1 | 30 |
| npc_vchangbrosblade | percent_occluded_flank | 2 | 1 | 20 |
| npc_vchangbrosblade | percent_occluded_wait | 2 | 1 | 10 |
| npc_vchangbrosblade | percent_occluded_walk | 2 | 1 | 10 |
| npc_vchangbrosblade | physdamagescale | 2 | 1 | 1.0 |
| npc_vchangbrosblade | pl_criminal_attack | 2 | 1 | -1 |
| npc_vchangbrosblade | pl_criminal_flee | 2 | 1 | -1 |
| npc_vchangbrosblade | pl_investigate | 2 | 1 | -1 |
| npc_vchangbrosblade | pl_supernatural_attack | 2 | 1 | -1 |
| npc_vchangbrosblade | pl_supernatural_flee | 2 | 1 | -1 |
| npc_vchangbrosblade | player_reaction | 2 | 1 | D_NU 0 |
| npc_vchangbrosblade | renderamt | 2 | 1 | 255 |
| npc_vchangbrosblade | rendercolor | 2 | 1 | 255 255 255 |
| npc_vchangbrosblade | skincolor | 2 | 1 | 0 0 0 0 |
| npc_vchangbrosblade | spawnflags | 2 | 1 | 4 |
| npc_vchangbrosblade | squadname | 2 | 1 | ChangBrothers |
| npc_vchangbrosblade | starthidden | 2 | 1 | 0 |
| npc_vchangbrosblade | stattemplate | 2 | 1 | ChangBros |
| npc_vchangbrosblade | targetname | 2 | 2 | ChangBrosBlade_basic / ChangBrosBlade_plus |
| npc_vchangbrosblade | trimcolor | 2 | 1 | 0 0 0 0 |
| npc_vchangbrosblade | vision | 2 | 1 | 3600 |
| npc_vchangbrosclaw | additionalequipment | 2 | 1 | item_w_chang_claw |
| npc_vchangbrosclaw | allow_alert_lookaround | 3 | 1 | 1 |
| npc_vchangbrosclaw | allow_kick_hint_use | 3 | 1 | 1 |
| npc_vchangbrosclaw | alternateequipment | 1 | 1 | item_w_katana |
| npc_vchangbrosclaw | angles | 3 | 2 | 0 0 0 / 0 13 0 |
| npc_vchangbrosclaw | bright_route_penalty | 1 | 1 | 0 |
| npc_vchangbrosclaw | cantdropweapons | 3 | 2 | 0 / 1 |
| npc_vchangbrosclaw | classname | 3 | 1 | npc_VChangBrosClaw |
| npc_vchangbrosclaw | clothescolor1 | 3 | 1 | 0 0 0 0 |
| npc_vchangbrosclaw | clothescolor2 | 3 | 1 | 0 0 0 0 |
| npc_vchangbrosclaw | combat_start_activity | 3 | 1 | -1 |
| npc_vchangbrosclaw | crossfade_skin_time | 3 | 1 | 2.0 |
| npc_vchangbrosclaw | default_camera | 3 | 1 | DialogDefault |
| npc_vchangbrosclaw | default_disposition | 3 | 1 | Neutral |
| npc_vchangbrosclaw | demo_sequence | 3 | 2 | Idle / None |
| npc_vchangbrosclaw | dialogname | 2 | 1 | dlg/Giovanni/Chang.dlg |
| npc_vchangbrosclaw | disablereceiveshadows | 1 | 1 | 0 |
| npc_vchangbrosclaw | disableshadows | 3 | 1 | 1 |
| npc_vchangbrosclaw | floatfreq | 1 | 1 | 0 |
| npc_vchangbrosclaw | follower_type | 3 | 1 | Default |
| npc_vchangbrosclaw | full_investigate | 1 | 1 | 0 |
| npc_vchangbrosclaw | haircolor | 3 | 1 | 0 0 0 0 |
| npc_vchangbrosclaw | hearing | 3 | 2 | 0.75 / 3.00 |
| npc_vchangbrosclaw | hint_groups | 3 | 1 |  |
| npc_vchangbrosclaw | interesting_place_groups | 1 | 1 | 0 |
| npc_vchangbrosclaw | investigate_mode | 3 | 1 | 4 |
| npc_vchangbrosclaw | investigate_mode_combat | 3 | 1 | 4 |
| npc_vchangbrosclaw | invincible | 1 | 1 | 0 |
| npc_vchangbrosclaw | is_bossmonster | 2 | 1 | 1 |
| npc_vchangbrosclaw | model | 3 | 2 |  |
| npc_vchangbrosclaw | npc_perception | 3 | 2 | 10 / 8 |
| npc_vchangbrosclaw | npc_transparent | 3 | 1 | 1 |
| npc_vchangbrosclaw | ondeath | 2 | 1 | chang_death_counter,Add,1,0,-1,, |
| npc_vchangbrosclaw | ondialogend | 2 | 1 | Relay_Start_Combat,Trigger,,0,-1,, |
| npc_vchangbrosclaw | onfoundplayer | 3 | 3 | ,,,0,-1,G.Temple2SencondChanceLost = 1, / ,,,0,-1,t2botched(), |
| npc_vchangbrosclaw | origin | 3 | 2 | -307 468.49 -132 / -40 103 9.42064 |
| npc_vchangbrosclaw | percent_occluded_chase | 3 | 2 | 100 / 30 |
| npc_vchangbrosclaw | percent_occluded_cover | 3 | 2 | 0 / 30 |
| npc_vchangbrosclaw | percent_occluded_flank | 3 | 2 | 0 / 20 |
| npc_vchangbrosclaw | percent_occluded_wait | 3 | 2 | 0 / 10 |
| npc_vchangbrosclaw | percent_occluded_walk | 3 | 2 | 0 / 10 |
| npc_vchangbrosclaw | physdamagescale | 3 | 1 | 1.0 |
| npc_vchangbrosclaw | pl_criminal_attack | 3 | 1 | -1 |
| npc_vchangbrosclaw | pl_criminal_flee | 3 | 1 | -1 |
| npc_vchangbrosclaw | pl_investigate | 3 | 1 | -1 |
| npc_vchangbrosclaw | pl_supernatural_attack | 3 | 1 | -1 |
| npc_vchangbrosclaw | pl_supernatural_flee | 3 | 1 | -1 |
| npc_vchangbrosclaw | player_reaction | 3 | 2 | D_HT 5 / D_NU 0 |
| npc_vchangbrosclaw | renderamt | 3 | 1 | 255 |
| npc_vchangbrosclaw | rendercolor | 3 | 1 | 255 255 255 |
| npc_vchangbrosclaw | renderfx | 1 | 1 | 0 |
| npc_vchangbrosclaw | rendermode | 1 | 1 | 0 |
| npc_vchangbrosclaw | skin | 1 | 1 | 0 |
| npc_vchangbrosclaw | skincolor | 3 | 1 | 0 0 0 0 |
| npc_vchangbrosclaw | spawnflags | 3 | 1 | 4 |
| npc_vchangbrosclaw | squadname | 3 | 2 | ChangBrothers / wheelroom |
| npc_vchangbrosclaw | starthidden | 3 | 2 | 0 / 1 |
| npc_vchangbrosclaw | stattemplate | 3 | 1 | ChangBrosClaw |
| npc_vchangbrosclaw | stay_entrenched | 1 | 1 | 0 |
| npc_vchangbrosclaw | targetname | 3 | 3 | Chang_basic / Chang_plus / plus_guard_waterwheel3 |
| npc_vchangbrosclaw | team_name-wesp | 1 | 1 | temple |
| npc_vchangbrosclaw | trimcolor | 3 | 1 | 0 0 0 0 |
| npc_vchangbrosclaw | use_interesting | 1 | 1 | 0 |
| npc_vchangbrosclaw | vision | 3 | 2 | 2400 / 3600 |
| npc_vcop | additionalequipment | 2 | 1 | item_w_baton |
| npc_vcop | allow_alert_lookaround | 2 | 1 | 1 |
| npc_vcop | allow_kick_hint_use | 2 | 1 | 1 |
| npc_vcop | alternateequipment | 2 | 1 | item_w_glock_17c |
| npc_vcop | angles | 2 | 1 | 0 0 0 |
| npc_vcop | bright_route_penalty | 2 | 1 | 0 |
| npc_vcop | classname | 2 | 1 | npc_VCop |
| npc_vcop | clothescolor1 | 2 | 1 | 0 0 0 0 |
| npc_vcop | clothescolor2 | 2 | 1 | 0 0 0 0 |
| npc_vcop | combat_start_activity | 2 | 1 | -1 |
| npc_vcop | crossfade_skin_time | 2 | 1 | 2.000 |
| npc_vcop | default_camera | 2 | 1 | DialogDefault |
| npc_vcop | default_disposition | 2 | 1 | Neutral |
| npc_vcop | demo_sequence | 2 | 1 | None |
| npc_vcop | disablereceiveshadows | 2 | 1 | 0 |
| npc_vcop | disableshadows | 2 | 1 | 1 |
| npc_vcop | floatfreq | 2 | 1 | 50 |
| npc_vcop | follower_type | 2 | 1 | Default |
| npc_vcop | haircolor | 2 | 1 | 0 0 0 0 |
| npc_vcop | hearing | 2 | 1 | -1.00 |
| npc_vcop | hint_groups | 2 | 1 |  |
| npc_vcop | interesting_place_groups | 2 | 1 | 0 |
| npc_vcop | investigate_mode | 2 | 1 | 0 |
| npc_vcop | investigate_mode_combat | 2 | 1 | 6 |
| npc_vcop | model | 2 | 1 |  |
| npc_vcop | npc_perception | 2 | 1 | 4 |
| npc_vcop | npc_transparent | 2 | 1 | 1 |
| npc_vcop | origin | 2 | 2 | -779.401 466.834 -103 / 656.261 -2583.67 -109 |
| npc_vcop | percent_occluded_chase | 2 | 1 | 40 |
| npc_vcop | percent_occluded_cover | 2 | 1 | 20 |
| npc_vcop | percent_occluded_flank | 2 | 1 | 25 |
| npc_vcop | percent_occluded_wait | 2 | 1 | 5 |
| npc_vcop | percent_occluded_walk | 2 | 1 | 10 |
| npc_vcop | physdamagescale | 2 | 1 | 1.0 |
| npc_vcop | pl_criminal_attack | 2 | 1 | 1 |
| npc_vcop | pl_criminal_flee | 2 | 1 | 5 |
| npc_vcop | pl_investigate | 2 | 1 | 1 |
| npc_vcop | pl_supernatural_attack | 2 | 1 | 1 |
| npc_vcop | pl_supernatural_flee | 2 | 1 | 4 |
| npc_vcop | player_reaction | 2 | 1 | D_NU 0 |
| npc_vcop | renderamt | 2 | 1 | 255 |
| npc_vcop | rendercolor | 2 | 1 | 255 255 255 |
| npc_vcop | renderfx | 2 | 1 | 0 |
| npc_vcop | rendermode | 2 | 1 | 0 |
| npc_vcop | skin | 2 | 1 | 0 |
| npc_vcop | skincolor | 2 | 1 | 0 0 0 0 |
| npc_vcop | spawnflags | 2 | 1 | 4 |
| npc_vcop | squadname | 2 | 1 | cops |
| npc_vcop | stattemplate | 2 | 1 | OfficerGeneric |
| npc_vcop | targetname | 2 | 2 | patrol_cop / patrol_cop_north |
| npc_vcop | trimcolor | 2 | 1 | 0 0 0 0 |
| npc_vcop | use_interesting | 2 | 1 | 0 |
| npc_vcop | vision | 2 | 1 | -1 |
| npc_vdialogpedestrian | allow_alert_lookaround | 5 | 2 | 0 / 1 |
| npc_vdialogpedestrian | allow_kick_hint_use | 5 | 1 | 1 |
| npc_vdialogpedestrian | angles | 5 | 5 | 0 102 0 / 0 135 0 / 0 230 0 / 0 289 0 / 0 62 0 |
| npc_vdialogpedestrian | base_gender | 5 | 1 | 1 |
| npc_vdialogpedestrian | bright_route_penalty | 5 | 1 | 0 |
| npc_vdialogpedestrian | classname | 5 | 1 | npc_VDialogPedestrian |
| npc_vdialogpedestrian | clothescolor1 | 5 | 1 | 0 0 0 0 |
| npc_vdialogpedestrian | clothescolor2 | 5 | 1 | 0 0 0 0 |
| npc_vdialogpedestrian | combat_start_activity | 5 | 1 | -1 |
| npc_vdialogpedestrian | crossfade_skin_time | 5 | 1 | 2.0 |
| npc_vdialogpedestrian | default_camera | 5 | 2 | DialogDefault / DialogDefaultWoman |
| npc_vdialogpedestrian | default_disposition | 5 | 1 | Neutral |
| npc_vdialogpedestrian | demo_sequence | 5 | 1 | None |
| npc_vdialogpedestrian | dialogname | 1 | 1 | dlg/Giovanni/Maria.dlg |
| npc_vdialogpedestrian | disablereceiveshadows | 5 | 1 | 0 |
| npc_vdialogpedestrian | disableshadows | 5 | 1 | 1 |
| npc_vdialogpedestrian | floatfreq | 5 | 2 | 0 / 2 |
| npc_vdialogpedestrian | follower_type | 5 | 1 | Default |
| npc_vdialogpedestrian | full_investigate | 5 | 1 | 0 |
| npc_vdialogpedestrian | haircolor | 5 | 1 | 0 0 0 0 |
| npc_vdialogpedestrian | hearing | 5 | 1 | -1.00 |
| npc_vdialogpedestrian | hint_groups | 5 | 1 |  |
| npc_vdialogpedestrian | interesting_place_groups | 5 | 2 | 0 / 1 |
| npc_vdialogpedestrian | investigate_mode | 5 | 1 | 4 |
| npc_vdialogpedestrian | investigate_mode_combat | 5 | 1 | 4 |
| npc_vdialogpedestrian | invincible | 5 | 1 | 0 |
| npc_vdialogpedestrian | level_reset_type | 1 | 1 | 2 |
| npc_vdialogpedestrian | model | 5 | 5 |  |
| npc_vdialogpedestrian | npc_perception | 5 | 1 | 3 |
| npc_vdialogpedestrian | npc_transparent | 5 | 1 | 1 |
| npc_vdialogpedestrian | ondeath | 1 | 1 | maria_drop,Trigger,,0,-1,, |
| npc_vdialogpedestrian | ondialogend | 1 | 1 | check_leave,Test,,0,-1,, |
| npc_vdialogpedestrian | onfoundenemy | 5 | 2 | ,,,0,-1,gio1_panicVictorMaria(), / Relay_Boched_Level,Trigger,,0,-1,, |
| npc_vdialogpedestrian | origin | 5 | 5 | -171 1263 -385 / -592.539 702.629 -415.336 / -668.327 1745.3 -323 / -718 1454 -359 / -877 1002 -399 |
| npc_vdialogpedestrian | percent_occluded_chase | 5 | 1 | 30 |
| npc_vdialogpedestrian | percent_occluded_cover | 5 | 1 | 30 |
| npc_vdialogpedestrian | percent_occluded_flank | 5 | 1 | 20 |
| npc_vdialogpedestrian | percent_occluded_wait | 5 | 1 | 10 |
| npc_vdialogpedestrian | percent_occluded_walk | 5 | 1 | 10 |
| npc_vdialogpedestrian | physdamagescale | 5 | 1 | 1.0 |
| npc_vdialogpedestrian | pl_criminal_attack | 5 | 1 | 6 |
| npc_vdialogpedestrian | pl_criminal_flee | 5 | 1 | 1 |
| npc_vdialogpedestrian | pl_investigate | 5 | 2 | -1 / 6 |
| npc_vdialogpedestrian | pl_supernatural_attack | 5 | 1 | 6 |
| npc_vdialogpedestrian | pl_supernatural_flee | 5 | 1 | 1 |
| npc_vdialogpedestrian | player_reaction | 5 | 1 | D_NU 0 |
| npc_vdialogpedestrian | renderamt | 5 | 1 | 255 |
| npc_vdialogpedestrian | rendercolor | 5 | 1 | 255 255 255 |
| npc_vdialogpedestrian | renderfx | 5 | 1 | 0 |
| npc_vdialogpedestrian | rendermode | 5 | 1 | 0 |
| npc_vdialogpedestrian | skin | 5 | 1 | 0 |
| npc_vdialogpedestrian | skincolor | 5 | 1 | 0 0 0 0 |
| npc_vdialogpedestrian | soundgroup | 3 | 3 | Blue_blood/Giovanni guest / Italian_accent / Unique/Maria |
| npc_vdialogpedestrian | spawnflags | 5 | 1 | 8196 |
| npc_vdialogpedestrian | squadname | 5 | 1 | squad_guard |
| npc_vdialogpedestrian | stattemplate | 5 | 1 | NPCGeneric |
| npc_vdialogpedestrian | stay_entrenched | 5 | 1 | 0 |
| npc_vdialogpedestrian | targetname | 5 | 2 | Maria / partygoer |
| npc_vdialogpedestrian | trimcolor | 5 | 1 | 0 0 0 0 |
| npc_vdialogpedestrian | use_interesting | 5 | 1 | 0 |
| npc_vdialogpedestrian | vision | 5 | 1 | -1 |
| npc_vghoulcroucher | additionalequipment | 22 | 1 | item_w_knife |
| npc_vghoulcroucher | allow_alert_lookaround | 44 | 1 | 1 |
| npc_vghoulcroucher | allow_kick_hint_use | 44 | 1 | 1 |
| npc_vghoulcroucher | alternateequipment | 8 | 1 | item_w_claws_ghoul |
| npc_vghoulcroucher | angles | 44 | 22 | 0 105 0 / 0 120 0 / 0 131 0 / 0 135 0 / 0 165 0 |
| npc_vghoulcroucher | base_gender | 9 | 1 | 1 |
| npc_vghoulcroucher | bright_route_penalty | 44 | 1 | 0 |
| npc_vghoulcroucher | classname | 44 | 1 | npc_VGhoulCroucher |
| npc_vghoulcroucher | clothescolor1 | 44 | 1 | 0 0 0 0 |
| npc_vghoulcroucher | clothescolor2 | 44 | 1 | 0 0 0 0 |
| npc_vghoulcroucher | combat_start_activity | 44 | 1 | -1 |
| npc_vghoulcroucher | crossfade_skin_time | 44 | 1 | 2.0 |
| npc_vghoulcroucher | default_camera | 44 | 1 | DialogDefault |
| npc_vghoulcroucher | default_disposition | 44 | 1 | Neutral |
| npc_vghoulcroucher | demo_sequence | 35 | 1 | None |
| npc_vghoulcroucher | disablereceiveshadows | 35 | 1 | 0 |
| npc_vghoulcroucher | disableshadows | 44 | 1 | 1 |
| npc_vghoulcroucher | disturbed | 44 | 2 | 0 / 1 |
| npc_vghoulcroucher | floatfreq | 44 | 1 | 0 |
| npc_vghoulcroucher | follower_type | 44 | 1 | Default |
| npc_vghoulcroucher | full_investigate | 44 | 1 | 0 |
| npc_vghoulcroucher | haircolor | 44 | 1 | 0 0 0 0 |
| npc_vghoulcroucher | hearing | 44 | 2 | -1.00 / 0.75 |
| npc_vghoulcroucher | hint_groups | 44 | 1 |  |
| npc_vghoulcroucher | interesting_place_groups | 44 | 2 | 0 / 2 |
| npc_vghoulcroucher | investigate_mode | 44 | 1 | 4 |
| npc_vghoulcroucher | investigate_mode_combat | 44 | 1 | 4 |
| npc_vghoulcroucher | invincible | 44 | 1 | 0 |
| npc_vghoulcroucher | model | 44 | 2 |  |
| npc_vghoulcroucher | npc_perception | 44 | 3 | 1 / 3 / 9 |
| npc_vghoulcroucher | npc_transparent | 35 | 1 | 1 |
| npc_vghoulcroucher | on_fire | 9 | 1 | 1 |
| npc_vghoulcroucher | origin | 44 | 43 | -1220 -36 -2512 / -1243 1001 -3032 / -1261 465 -3166 / -1288 -34 -2974 / -1298 238 -2974 |
| npc_vghoulcroucher | percent_occluded_chase | 44 | 1 | 30 |
| npc_vghoulcroucher | percent_occluded_cover | 44 | 1 | 30 |
| npc_vghoulcroucher | percent_occluded_flank | 44 | 1 | 20 |
| npc_vghoulcroucher | percent_occluded_wait | 44 | 1 | 10 |
| npc_vghoulcroucher | percent_occluded_walk | 44 | 1 | 10 |
| npc_vghoulcroucher | physdamagescale | 44 | 1 | 1.0 |
| npc_vghoulcroucher | pl_criminal_attack | 44 | 1 | -1 |
| npc_vghoulcroucher | pl_criminal_flee | 44 | 1 | -1 |
| npc_vghoulcroucher | pl_investigate | 44 | 1 | -1 |
| npc_vghoulcroucher | pl_supernatural_attack | 44 | 1 | -1 |
| npc_vghoulcroucher | pl_supernatural_flee | 44 | 1 | -1 |
| npc_vghoulcroucher | player_reaction | 44 | 2 | D_HT 5 / D_NU 0 |
| npc_vghoulcroucher | renderamt | 44 | 1 | 255 |
| npc_vghoulcroucher | rendercolor | 44 | 1 | 255 255 255 |
| npc_vghoulcroucher | renderfx | 44 | 1 | 0 |
| npc_vghoulcroucher | rendermode | 44 | 1 | 0 |
| npc_vghoulcroucher | skin | 44 | 1 | 0 |
| npc_vghoulcroucher | skincolor | 44 | 1 | 0 0 0 0 |
| npc_vghoulcroucher | soundgroup | 44 | 2 | croucher / stalker |
| npc_vghoulcroucher | spawnflags | 44 | 1 | 4 |
| npc_vghoulcroucher | squadname | 44 | 12 | cells1 / cells2 / cells3 / dining_hall / entrance |
| npc_vghoulcroucher | starthidden | 7 | 1 | 1 |
| npc_vghoulcroucher | stattemplate | 44 | 2 | MalkMansionCroucher / MalkMansionStalker |
| npc_vghoulcroucher | stay_entrenched | 44 | 1 | 0 |
| npc_vghoulcroucher | targetname | 44 | 37 | ghoul_dining_hall_c_1 / ghoul_dining_hall_c_2 / ghoul_dining_hall_c_3 / ghoul_dining_hall_s_4 / ghoul_dining_hall_s_5 |
| npc_vghoulcroucher | teleport_move_timer | 44 | 1 | 0 |
| npc_vghoulcroucher | trimcolor | 44 | 1 | 0 0 0 0 |
| npc_vghoulcroucher | use_interesting | 44 | 2 | 0 / 1 |
| npc_vghoulcroucher | vision | 44 | 2 | -1 / 540 |
| npc_vhumancombatant | additionalequipment | 417 | 22 | 0 / item_w_avamp_blade / item_w_baseball_bat / item_w_claws / item_w_colt_anaconda |
| npc_vhumancombatant | allow_alert_lookaround | 422 | 2 | 0 / 1 |
| npc_vhumancombatant | allow_kick_hint_use | 422 | 2 | 0 / 1 |
| npc_vhumancombatant | alternateequipment | 154 | 12 | 0 / item_w_avamp_blade / item_w_baseball_bat / item_w_baton / item_w_deserteagle |
| npc_vhumancombatant | angles | 422 | 138 | 0 -90 0 / 0 0 0 / 0 10.5 0 / 0 102 0 / 0 103 0 |
| npc_vhumancombatant | base_gender | 109 | 2 | 0 / 1 |
| npc_vhumancombatant | bright_route_penalty | 411 | 1 | 0 |
| npc_vhumancombatant | cantdropweapons | 116 | 1 | 0 |
| npc_vhumancombatant | classname | 422 | 1 | npc_VHumanCombatant |
| npc_vhumancombatant | clothescolor1 | 422 | 1 | 0 0 0 0 |
| npc_vhumancombatant | clothescolor2 | 422 | 1 | 0 0 0 0 |
| npc_vhumancombatant | combat_start_activity | 422 | 2 | -1 / ACT_INVALID |
| npc_vhumancombatant | crossfade_skin_time | 422 | 2 | 2 / 2.0 |
| npc_vhumancombatant | default_camera | 422 | 3 | DialogDefault / DialogDefaultWoman / ZhaoFarHigh |
| npc_vhumancombatant | default_disposition | 422 | 5 | Anger / Combat / Damaged / Default / Neutral |
| npc_vhumancombatant | demo_sequence | 370 | 6 | Idle / NONE / None / operate_computer / sit_barstool_into_alt |
| npc_vhumancombatant | dialogname | 21 | 16 | dlg/Chinatown/Barry_R.dlg / dlg/Chinatown/Ricky.dlg / dlg/Chinatown/Zhao.dlg / dlg/Downtown LA/Bomberman.dlg / dlg/Downtown LA/Dema.dlg |
| npc_vhumancombatant | disablereceiveshadows | 370 | 2 | 0 / 1 |
| npc_vhumancombatant | disableshadows | 422 | 2 | 0 / 1 |
| npc_vhumancombatant | floatfreq | 418 | 5 | 0 / 10 / 2 / 4 / 8 |
| npc_vhumancombatant | follower_type | 422 | 1 | Default |
| npc_vhumancombatant | full_investigate | 380 | 2 | 0 / 1 |
| npc_vhumancombatant | haircolor | 422 | 1 | 0 0 0 0 |
| npc_vhumancombatant | hearing | 422 | 15 | -1 / -1.0 / -1.00 / .01 / .1 |
| npc_vhumancombatant | hint_groups | 422 | 20 | 1 / 1 10 / 1 2 / 1 3 / 1 4 |
| npc_vhumancombatant | ignore_detected_attack | 26 | 1 | 0 |
| npc_vhumancombatant | interesting_place_groups | 416 | 50 | 0 / 1 / 1 2 / 1 2 6 / 1 40 41 42 |
| npc_vhumancombatant | investigate_mode | 422 | 5 | 0 / 1 / 3 / 4 / 6 |
| npc_vhumancombatant | investigate_mode_combat | 422 | 5 | 0 / 1 / 3 / 4 / 6 |
| npc_vhumancombatant | invincible | 196 | 2 | 0 / 1 |
| npc_vhumancombatant | is_bossmonster | 32 | 2 | 0 / 1 |
| npc_vhumancombatant | level_reset_type | 2 | 1 | 1 |
| npc_vhumancombatant | model | 422 | 38 |  |
| npc_vhumancombatant | nav_ignore_physics_props | 1 | 1 | 0 |
| npc_vhumancombatant | no_alert_state | 29 | 2 | 0 / 1 |
| npc_vhumancombatant | npc_perception | 422 | 11 | 0 / 1 / 10 / 2 / 3 |
| npc_vhumancombatant | npc_transparent | 370 | 1 | 1 |
| npc_vhumancombatant | npcsquadname | 4 | 2 | squad_guard_north / tong2 |
| npc_vhumancombatant | ondamaged | 92 | 64 | ,,,0,-1,OnKeyGuardDeath(), / ,,,2.2,-1,checkMafiaState(), / ,TweakParam,vision 540,0,-1,, / Door_Stop_Button2,Kill,,0,-1,, / Relay_Boched_Stern,Trigger,,0.1,-1,, |
| npc_vhumancombatant | ondamaged-wesp | 2 | 1 | ,,,0,-1,G.Boris_Hostile = 2, |
| npc_vhumancombatant | ondeath | 250 | 93 | ,,,0,-1,G.Glaze_Kill = 1, / ,,,0,-1,G.Guard1_Killed = 1, / ,,,0,-1,G.Guard2_Killed = 1, / ,,,0,-1,G.Igor_Talk = 1, / ,,,0,-1,G.Tawni_Boy_Dead = 1, |
| npc_vhumancombatant | ondialogbegin | 8 | 8 | Trigger_Dirty_Cop_DLG,Kill,,0,-1,, / Trigger_Hey_Come_Here,Kill,,0,-1,, / dema_player_teleport,Teleport,,0,1,, / grr_scheme,FadeIn,0.5,0,-1,, / possession,TurnOn,,0,-1,, |
| npc_vhumancombatant | ondialogend | 25 | 22 | ,,,0,-1,IgorEndDialog(), / ,,,0,-1,bodyguardRandResponce(), / ,,,0,-1,brianDialogResults(), / ,,,0,-1,checkMafiaState(), / ,,,0,-1,demaDialog(), |
| npc_vhumancombatant | onfeduponbegin | 2 | 2 | ,,,0,-1,OnKeyGuardDeath(), / sound_monk_prayer,Kill,,0,-1,, |
| npc_vhumancombatant | onfoundenemy | 74 | 19 | ,,,0,-1,G.Boris_Hostile = 2, / ,,,2.2,-1,checkMafiaState(), / Door_Stop_Button2,Kill,,0,-1,, / Door_locker,Trigger,,0,-1,, / Panic_Party,Trigger,,3,-1,, |
| npc_vhumancombatant | onfoundplayer | 295 | 71 | ,,,0,-1,G.ParkingGarageSpotted = 1, / ,,,0,-1,G.Temple2SencondChanceLost = 1, / ,,,0,-1,G.Temple2_Botched = 1, / ,,,0,-1,G.Warehouse_Spotted = 1, / ,,,0,-1,setSpotted(), |
| npc_vhumancombatant | ongrapplebegin | 2 | 2 | al_mumble,Kill,,0,1,, / al_mumble_breaker,Kill,,0,1,, |
| npc_vhumancombatant | onhearcombat | 84 | 28 | ,,,0,-1,G.Boris_Hostile = 2, / ,,,2.2,-1,checkMafiaState(), / Mike,TweakParam,vision 1400,0,-1,, / al_mumble,Kill,,0,1,, / al_mumble_breaker,Kill,,0,1,, |
| npc_vhumancombatant | onhearplayer | 9 | 3 | room_3_talking,StopSound,,0,-1,, / sound_monk_prayer,Kill,,0,-1,, |
| npc_vhumancombatant | onhearworld | 3 | 3 | Door_Stop_Button2,Kill,,0,-1,, |
| npc_vhumancombatant | onincapacitatedend | 3 | 3 | Door_Stop_Button2,ScriptUnhide,,0,-1,, / brian_talk,Enable,,0,-1,, / dennis_talk,Enable,,0,-1,, |
| npc_vhumancombatant | onincapacitatedstart | 10 | 8 | ,,,0,-1,OnKeyGuardDeath(), / Door_Stop_Button2,ScriptHide,,0,-1,, / brian_talk,Disable,,0,-1,, / dennis_talk,Disable,,0,-1,, / logic_deny_access,Disable,,0,-1,, |
| npc_vhumancombatant | onspawnnpc | 1 | 1 |  |
| npc_vhumancombatant | onstatefleeing | 1 | 1 | Relay_VictorMariaFlee,Trigger,,0,-1,, |
| npc_vhumancombatant | onunknownvisionplayer | 1 | 1 | perception_office_thugs,Trigger,,0,1,, |
| npc_vhumancombatant | origin | 422 | 412 | -100 -690.438 1 / -100.72 2576.39 -6819 / -1004 60 1 / -1020 80 2 / -1034.91 1291.49 174 |
| npc_vhumancombatant | percent_occluded_chase | 422 | 12 | 0 / 10 / 100 / 15 / 20 |
| npc_vhumancombatant | percent_occluded_cover | 422 | 11 | 0 / 10 / 100 / 15 / 20 |
| npc_vhumancombatant | percent_occluded_flank | 422 | 7 | 0 / 10 / 15 / 20 / 30 |
| npc_vhumancombatant | percent_occluded_wait | 422 | 10 | 0 / 10 / 100 / 15 / 20 |
| npc_vhumancombatant | percent_occluded_walk | 422 | 7 | 0 / 10 / 100 / 20 / 30 |
| npc_vhumancombatant | physdamagescale | 422 | 3 | 1 / 1.0 / 10 |
| npc_vhumancombatant | pl_criminal_attack | 422 | 4 | -1 / 1 / 2 / 6 |
| npc_vhumancombatant | pl_criminal_flee | 422 | 5 | -1 / 2 / 4 / 5 / 6 |
| npc_vhumancombatant | pl_investigate | 422 | 4 | -1 / 1 / 5 / 6 |
| npc_vhumancombatant | pl_supernatural_attack | 422 | 5 | -1 / 1 / 2 / 3 / 6 |
| npc_vhumancombatant | pl_supernatural_flee | 422 | 7 | -1 / 1 / 2 / 3 / 4 |
| npc_vhumancombatant | player_reaction | 422 | 7 | D_HT 0 / D_HT 10 / D_HT 5 / D_HT 6 / D_LI 0 |
| npc_vhumancombatant | radius | 9 | 5 | 0 / 448 / 750 / 827 / 985 |
| npc_vhumancombatant | renderamt | 422 | 1 | 255 |
| npc_vhumancombatant | rendercolor | 422 | 1 | 255 255 255 |
| npc_vhumancombatant | renderfx | 418 | 1 | 0 |
| npc_vhumancombatant | rendermode | 418 | 1 | 0 |
| npc_vhumancombatant | skin | 418 | 4 | 0 / 1 / 2 / 3 |
| npc_vhumancombatant | skincolor | 422 | 1 | 0 0 0 0 |
| npc_vhumancombatant | soundgroup | 57 | 11 | Al / Asian / Italian_accent/upset / Museum_Guard / Unique/Brian |
| npc_vhumancombatant | spawnflags | 422 | 9 | 0 / 12 / 12900 / 29284 / 33284 |
| npc_vhumancombatant | squadname | 422 | 102 | BelmontTeam / Deck2 / Deck3 / Deck4 / East_Door_Guard |
| npc_vhumancombatant | starthidden | 224 | 2 | 0 / 1 |
| npc_vhumancombatant | stattemplate | 422 | 40 | BeachhouseThug / BeachhouseThugDennis / Bomberman / DaneGenCop / DaneLessCop |
| npc_vhumancombatant | stay_entrenched | 391 | 2 | 0 / 1 |
| npc_vhumancombatant | targetname | 422 | 319 | Al / Barry_R / Brian / Carl / Cop_Deck1_Guard1 |
| npc_vhumancombatant | team_name-wesp | 175 | 7 | cops / giovanni / hunters / temple / thugs |
| npc_vhumancombatant | teleport_move_timer | 65 | 2 | 0 / 2 |
| npc_vhumancombatant | trimcolor | 422 | 1 | 0 0 0 0 |
| npc_vhumancombatant | use_interesting | 420 | 2 | 0 / 1 |
| npc_vhumancombatant | usescript | 2 | 1 | igor_buddy |
| npc_vhumancombatant | vision | 422 | 27 | -1 / 0 / 1000 / 1024 / 1100 |
| npc_vhumancombatpatrol | additionalequipment | 1 | 1 | item_w_thirtyeight |
| npc_vhumancombatpatrol | allow_alert_lookaround | 1 | 1 | 1 |
| npc_vhumancombatpatrol | allow_kick_hint_use | 1 | 1 | 1 |
| npc_vhumancombatpatrol | alternateequipment | 1 | 1 | item_w_baton |
| npc_vhumancombatpatrol | angles | 1 | 1 | 0 180 0 |
| npc_vhumancombatpatrol | base_gender | 1 | 1 | 1 |
| npc_vhumancombatpatrol | bright_route_penalty | 1 | 1 | 0 |
| npc_vhumancombatpatrol | classname | 1 | 1 | npc_VHumanCombatPatrol |
| npc_vhumancombatpatrol | clothescolor1 | 1 | 1 | 0 0 0 0 |
| npc_vhumancombatpatrol | clothescolor2 | 1 | 1 | 0 0 0 0 |
| npc_vhumancombatpatrol | combat_start_activity | 1 | 1 | -1 |
| npc_vhumancombatpatrol | crossfade_skin_time | 1 | 1 | 2.0 |
| npc_vhumancombatpatrol | default_camera | 1 | 1 | DialogDefault |
| npc_vhumancombatpatrol | default_disposition | 1 | 1 | Neutral |
| npc_vhumancombatpatrol | disableshadows | 1 | 1 | 1 |
| npc_vhumancombatpatrol | floatfreq | 1 | 1 | 0 |
| npc_vhumancombatpatrol | follower_type | 1 | 1 | Default |
| npc_vhumancombatpatrol | full_investigate | 1 | 1 | 0 |
| npc_vhumancombatpatrol | haircolor | 1 | 1 | 0 0 0 0 |
| npc_vhumancombatpatrol | hearing | 1 | 1 | -1.00 |
| npc_vhumancombatpatrol | hint_groups | 1 | 1 |  |
| npc_vhumancombatpatrol | interesting_place_groups | 1 | 1 | 5 |
| npc_vhumancombatpatrol | investigate_mode | 1 | 1 | 4 |
| npc_vhumancombatpatrol | investigate_mode_combat | 1 | 1 | 4 |
| npc_vhumancombatpatrol | model | 1 | 1 |  |
| npc_vhumancombatpatrol | npc_perception | 1 | 1 | 3 |
| npc_vhumancombatpatrol | ondeath | 1 | 1 | inc_kill_count,Trigger,,0,-1,, |
| npc_vhumancombatpatrol | onfoundplayer | 1 | 1 | alert_event,Trigger,,0,-1,, |
| npc_vhumancombatpatrol | origin | 1 | 1 | 1635.73 -464.136 -283.849 |
| npc_vhumancombatpatrol | percent_occluded_chase | 1 | 1 | 35 |
| npc_vhumancombatpatrol | percent_occluded_cover | 1 | 1 | 20 |
| npc_vhumancombatpatrol | percent_occluded_flank | 1 | 1 | 10 |
| npc_vhumancombatpatrol | percent_occluded_wait | 1 | 1 | 5 |
| npc_vhumancombatpatrol | percent_occluded_walk | 1 | 1 | 30 |
| npc_vhumancombatpatrol | physdamagescale | 1 | 1 | 1.0 |
| npc_vhumancombatpatrol | pl_criminal_attack | 1 | 1 | 1 |
| npc_vhumancombatpatrol | pl_criminal_flee | 1 | 1 | 5 |
| npc_vhumancombatpatrol | pl_investigate | 1 | 1 | 1 |
| npc_vhumancombatpatrol | pl_supernatural_attack | 1 | 1 | 1 |
| npc_vhumancombatpatrol | pl_supernatural_flee | 1 | 1 | 5 |
| npc_vhumancombatpatrol | player_reaction | 1 | 1 | D_HT 5 |
| npc_vhumancombatpatrol | renderamt | 1 | 1 | 255 |
| npc_vhumancombatpatrol | rendercolor | 1 | 1 | 255 255 255 |
| npc_vhumancombatpatrol | renderfx | 1 | 1 | 0 |
| npc_vhumancombatpatrol | rendermode | 1 | 1 | 0 |
| npc_vhumancombatpatrol | skin | 1 | 1 | 0 |
| npc_vhumancombatpatrol | skincolor | 1 | 1 | 0 0 0 0 |
| npc_vhumancombatpatrol | spawnflags | 1 | 1 | 4 |
| npc_vhumancombatpatrol | squadname | 1 | 1 | squad_basement |
| npc_vhumancombatpatrol | stattemplate | 1 | 1 | MuseumGuard |
| npc_vhumancombatpatrol | stay_entrenched | 1 | 1 | 0 |
| npc_vhumancombatpatrol | targetname | 1 | 1 | npc_guard_b1 |
| npc_vhumancombatpatrol | trimcolor | 1 | 1 | 0 0 0 0 |
| npc_vhumancombatpatrol | use_interesting | 1 | 1 | 1 |
| npc_vhumancombatpatrol | vision | 1 | 1 | -1 |
| npc_vlasombra | additionalequipment | 3 | 1 | item_w_glock_17c |
| npc_vlasombra | allow_alert_lookaround | 3 | 1 | 1 |
| npc_vlasombra | allow_kick_hint_use | 3 | 1 | 0 |
| npc_vlasombra | angles | 3 | 1 | 0 270 0 |
| npc_vlasombra | bright_route_penalty | 3 | 1 | 100000 |
| npc_vlasombra | classname | 3 | 1 | npc_VLasombra |
| npc_vlasombra | clothescolor1 | 3 | 1 | 0 0 0 0 |
| npc_vlasombra | clothescolor2 | 3 | 1 | 0 0 0 0 |
| npc_vlasombra | combat_start_activity | 3 | 1 | -1 |
| npc_vlasombra | crossfade_skin_time | 3 | 1 | 2.0 |
| npc_vlasombra | default_camera | 3 | 1 | DialogDefault |
| npc_vlasombra | default_disposition | 3 | 1 | Neutral |
| npc_vlasombra | demo_sequence | 3 | 1 | None |
| npc_vlasombra | disablereceiveshadows | 3 | 1 | 0 |
| npc_vlasombra | disableshadows | 3 | 1 | 1 |
| npc_vlasombra | floatfreq | 3 | 1 | 0 |
| npc_vlasombra | follower_type | 3 | 1 | Default |
| npc_vlasombra | full_investigate | 3 | 1 | 0 |
| npc_vlasombra | haircolor | 3 | 1 | 0 0 0 0 |
| npc_vlasombra | hearing | 3 | 1 | -1.00 |
| npc_vlasombra | hint_groups | 3 | 1 |  |
| npc_vlasombra | interesting_place_groups | 3 | 1 | 0 |
| npc_vlasombra | investigate_mode | 3 | 1 | 4 |
| npc_vlasombra | investigate_mode_combat | 3 | 1 | 4 |
| npc_vlasombra | invincible | 3 | 1 | 0 |
| npc_vlasombra | model | 3 | 1 |  |
| npc_vlasombra | npc_perception | 3 | 1 | 6 |
| npc_vlasombra | npc_transparent | 3 | 1 | 1 |
| npc_vlasombra | ondamaged | 9 | 7 | Lasombra,MakeInvincible,0,2.5,-1,, / Lasombra,MakeInvincible,1,0.5,-1,, / Lasombra_2,MakeInvincible,0,2.5,-1,, / Lasombra_2,MakeInvincible,1,0.5,-1,, / Lasombra_3,MakeInvincible,0,2.5,-1,, |
| npc_vlasombra | ondeath | 3 | 1 | ,,,0,-1,deadLasombra(), |
| npc_vlasombra | origin | 3 | 3 | 16 196 -439 / 354.748 204.287 -439 / 780 168 -439 |
| npc_vlasombra | percent_occluded_chase | 3 | 1 | 0 |
| npc_vlasombra | percent_occluded_cover | 3 | 1 | 0 |
| npc_vlasombra | percent_occluded_flank | 3 | 1 | 0 |
| npc_vlasombra | percent_occluded_wait | 3 | 1 | 100 |
| npc_vlasombra | percent_occluded_walk | 3 | 1 | 0 |
| npc_vlasombra | physdamagescale | 3 | 1 | 1.0 |
| npc_vlasombra | pl_criminal_attack | 3 | 1 | -1 |
| npc_vlasombra | pl_criminal_flee | 3 | 1 | -1 |
| npc_vlasombra | pl_investigate | 3 | 1 | -1 |
| npc_vlasombra | pl_supernatural_attack | 3 | 1 | -1 |
| npc_vlasombra | pl_supernatural_flee | 3 | 1 | -1 |
| npc_vlasombra | player_reaction | 3 | 1 | D_HT 5 |
| npc_vlasombra | renderamt | 3 | 1 | 255 |
| npc_vlasombra | rendercolor | 3 | 1 | 255 255 255 |
| npc_vlasombra | renderfx | 3 | 1 | 0 |
| npc_vlasombra | rendermode | 3 | 1 | 0 |
| npc_vlasombra | skin | 3 | 1 | 0 |
| npc_vlasombra | skincolor | 3 | 1 | 0 0 0 0 |
| npc_vlasombra | spawnflags | 3 | 1 | 4 |
| npc_vlasombra | squadname | 3 | 1 | shadow_gunners |
| npc_vlasombra | starthidden | 3 | 1 | 1 |
| npc_vlasombra | stattemplate | 3 | 1 | VampireLasombra |
| npc_vlasombra | stay_entrenched | 3 | 1 | 1 |
| npc_vlasombra | targetname | 3 | 3 | Lasombra / Lasombra_2 / Lasombra_3 |
| npc_vlasombra | trimcolor | 3 | 1 | 0 0 0 0 |
| npc_vlasombra | use_interesting | 3 | 1 | 0 |
| npc_vlasombra | vision | 3 | 1 | -1 |
| npc_vmanbat | allow_alert_lookaround | 1 | 1 | 1 |
| npc_vmanbat | allow_kick_hint_use | 1 | 1 | 1 |
| npc_vmanbat | angles | 1 | 1 | 0 90 0 |
| npc_vmanbat | bright_route_penalty | 1 | 1 | 0 |
| npc_vmanbat | cantdropweapons | 1 | 1 | 0 |
| npc_vmanbat | classname | 1 | 1 | npc_VManBat |
| npc_vmanbat | clothescolor1 | 1 | 1 | 0 0 0 0 |
| npc_vmanbat | clothescolor2 | 1 | 1 | 0 0 0 0 |
| npc_vmanbat | combat_start_activity | 1 | 1 | ACT_INVALID |
| npc_vmanbat | crossfade_skin_time | 1 | 1 | 2.0 |
| npc_vmanbat | default_camera | 1 | 1 | DialogDefault |
| npc_vmanbat | default_disposition | 1 | 1 | Neutral |
| npc_vmanbat | demo_sequence | 1 | 1 | None |
| npc_vmanbat | disablereceiveshadows | 1 | 1 | 0 |
| npc_vmanbat | disableshadows | 1 | 1 | 1 |
| npc_vmanbat | floatfreq | 1 | 1 | 0 |
| npc_vmanbat | follower_type | 1 | 1 | Default |
| npc_vmanbat | full_investigate | 1 | 1 | 0 |
| npc_vmanbat | haircolor | 1 | 1 | 0 0 0 0 |
| npc_vmanbat | hearing | 1 | 1 | -1.00 |
| npc_vmanbat | hint_groups | 1 | 1 |  |
| npc_vmanbat | interesting_place_groups | 1 | 1 | 0 |
| npc_vmanbat | investigate_mode | 1 | 1 | 4 |
| npc_vmanbat | investigate_mode_combat | 1 | 1 | 4 |
| npc_vmanbat | invincible | 1 | 1 | 0 |
| npc_vmanbat | is_bossmonster | 1 | 1 | 1 |
| npc_vmanbat | model | 1 | 1 |  |
| npc_vmanbat | npc_perception | 1 | 1 | 3 |
| npc_vmanbat | npc_transparent | 1 | 1 | 1 |
| npc_vmanbat | ondeath | 1 | 1 | plus_check_exit,Test,,1,-1,, |
| npc_vmanbat | origin | 1 | 1 | 7 -153 204 |
| npc_vmanbat | percent_occluded_chase | 1 | 1 | 30 |
| npc_vmanbat | percent_occluded_cover | 1 | 1 | 30 |
| npc_vmanbat | percent_occluded_flank | 1 | 1 | 20 |
| npc_vmanbat | percent_occluded_wait | 1 | 1 | 10 |
| npc_vmanbat | percent_occluded_walk | 1 | 1 | 10 |
| npc_vmanbat | physdamagescale | 1 | 1 | 1.0 |
| npc_vmanbat | pl_criminal_attack | 1 | 1 | 6 |
| npc_vmanbat | pl_criminal_flee | 1 | 1 | 6 |
| npc_vmanbat | pl_investigate | 1 | 1 | 6 |
| npc_vmanbat | pl_supernatural_attack | 1 | 1 | 6 |
| npc_vmanbat | pl_supernatural_flee | 1 | 1 | 6 |
| npc_vmanbat | player_reaction | 1 | 1 | D_NU 0 |
| npc_vmanbat | renderamt | 1 | 1 | 255 |
| npc_vmanbat | rendercolor | 1 | 1 | 255 255 255 |
| npc_vmanbat | renderfx | 1 | 1 | 0 |
| npc_vmanbat | rendermode | 1 | 1 | 0 |
| npc_vmanbat | skin | 1 | 1 | 0 |
| npc_vmanbat | skincolor | 1 | 1 | 0 0 0 0 |
| npc_vmanbat | spawnflags | 1 | 1 | 4 |
| npc_vmanbat | squadname | 1 | 1 | ManBat Squad |
| npc_vmanbat | starthidden | 1 | 1 | 1 |
| npc_vmanbat | stattemplate | 1 | 1 | ManBat |
| npc_vmanbat | stay_entrenched | 1 | 1 | 0 |
| npc_vmanbat | targetname | 1 | 1 | ManBat |
| npc_vmanbat | teleport_move_timer | 1 | 1 | 0 |
| npc_vmanbat | trimcolor | 1 | 1 | 0 0 0 0 |
| npc_vmanbat | use_interesting | 1 | 1 | 0 |
| npc_vmanbat | vision | 1 | 1 | -1 |
| npc_vmingxiao | allow_alert_lookaround | 1 | 1 | 0 |
| npc_vmingxiao | allow_kick_hint_use | 1 | 1 | 1 |
| npc_vmingxiao | angles | 1 | 1 | 0 90 0 |
| npc_vmingxiao | bright_route_penalty | 1 | 1 | 0 |
| npc_vmingxiao | cantdropweapons | 1 | 1 | 1 |
| npc_vmingxiao | classname | 1 | 1 | npc_VMingXiao |
| npc_vmingxiao | clothescolor1 | 1 | 1 | 0 0 0 0 |
| npc_vmingxiao | clothescolor2 | 1 | 1 | 0 0 0 0 |
| npc_vmingxiao | combat_start_activity | 1 | 1 | ACT_INVALID |
| npc_vmingxiao | crossfade_skin_time | 1 | 1 | 2.0 |
| npc_vmingxiao | default_camera | 1 | 1 |  |
| npc_vmingxiao | default_disposition | 1 | 1 | Neutral |
| npc_vmingxiao | demo_sequence | 1 | 1 | None |
| npc_vmingxiao | dialogname | 1 | 1 | dlg/Main Characters/MingXiao2.dlg |
| npc_vmingxiao | disablereceiveshadows | 1 | 1 | 0 |
| npc_vmingxiao | disableshadows | 1 | 1 | 1 |
| npc_vmingxiao | floatfreq | 1 | 1 | 0 |
| npc_vmingxiao | follower_type | 1 | 1 | Default |
| npc_vmingxiao | full_investigate | 1 | 1 | 0 |
| npc_vmingxiao | haircolor | 1 | 1 | 0 0 0 0 |
| npc_vmingxiao | hearing | 1 | 1 | -1.00 |
| npc_vmingxiao | hint_groups | 1 | 1 |  |
| npc_vmingxiao | interesting_place_groups | 1 | 1 | 0 |
| npc_vmingxiao | investigate_mode | 1 | 1 | 4 |
| npc_vmingxiao | investigate_mode_combat | 1 | 1 | 4 |
| npc_vmingxiao | is_bossmonster | 1 | 1 | 1 |
| npc_vmingxiao | model | 1 | 1 |  |
| npc_vmingxiao | npc_perception | 1 | 1 | 9 |
| npc_vmingxiao | npc_transparent | 1 | 1 | 1 |
| npc_vmingxiao | ondeath | 2 | 2 | ,,,0,-1,OnMingXiaoDead(), / logic_key_spawn,Trigger,,0,-1,, |
| npc_vmingxiao | ondialogend | 1 | 1 | logic_switch_mx,Trigger,,0,-1,, |
| npc_vmingxiao | origin | 1 | 1 | 64 -120 -487 |
| npc_vmingxiao | percent_occluded_chase | 1 | 1 | 30 |
| npc_vmingxiao | percent_occluded_cover | 1 | 1 | 30 |
| npc_vmingxiao | percent_occluded_flank | 1 | 1 | 20 |
| npc_vmingxiao | percent_occluded_wait | 1 | 1 | 10 |
| npc_vmingxiao | percent_occluded_walk | 1 | 1 | 10 |
| npc_vmingxiao | physdamagescale | 1 | 1 | 1.0 |
| npc_vmingxiao | pl_criminal_attack | 1 | 1 | -1 |
| npc_vmingxiao | pl_criminal_flee | 1 | 1 | -1 |
| npc_vmingxiao | pl_investigate | 1 | 1 | -1 |
| npc_vmingxiao | pl_supernatural_attack | 1 | 1 | -1 |
| npc_vmingxiao | pl_supernatural_flee | 1 | 1 | -1 |
| npc_vmingxiao | player_reaction | 1 | 1 | D_NU 0 |
| npc_vmingxiao | renderamt | 1 | 1 | 255 |
| npc_vmingxiao | rendercolor | 1 | 1 | 255 255 255 |
| npc_vmingxiao | renderfx | 1 | 1 | 0 |
| npc_vmingxiao | rendermode | 1 | 1 | 0 |
| npc_vmingxiao | skin | 1 | 1 | 0 |
| npc_vmingxiao | skincolor | 1 | 1 | 0 0 0 0 |
| npc_vmingxiao | spawnflags | 1 | 1 | 4 |
| npc_vmingxiao | squadname | 1 | 1 | MingXiao |
| npc_vmingxiao | starthidden | 1 | 1 | 0 |
| npc_vmingxiao | stattemplate | 1 | 1 | MingXiao |
| npc_vmingxiao | stay_entrenched | 1 | 1 | 0 |
| npc_vmingxiao | targetname | 1 | 1 | MingXiao2 |
| npc_vmingxiao | team_name | 1 | 1 | MingXiao |
| npc_vmingxiao | trimcolor | 1 | 1 | 0 0 0 0 |
| npc_vmingxiao | use_interesting | 1 | 1 | 0 |
| npc_vmingxiao | vision | 1 | 1 | 6000 |
| npc_vpedestrian | allow_alert_lookaround | 19 | 2 | 0 / 1 |
| npc_vpedestrian | allow_kick_hint_use | 20 | 1 | 1 |
| npc_vpedestrian | angles | 20 | 13 | 0 -90 0 / 0 0 0 / 0 132 0 / 0 171 0 / 0 196 0 |
| npc_vpedestrian | base_gender | 2 | 1 | 1 |
| npc_vpedestrian | bright_route_penalty | 20 | 1 | 0 |
| npc_vpedestrian | cantdropweapons | 6 | 1 | 0 |
| npc_vpedestrian | classname | 20 | 1 | npc_VPedestrian |
| npc_vpedestrian | clothescolor1 | 20 | 1 | 0 0 0 0 |
| npc_vpedestrian | clothescolor2 | 20 | 1 | 0 0 0 0 |
| npc_vpedestrian | combat_start_activity | 20 | 1 | -1 |
| npc_vpedestrian | crossfade_skin_time | 20 | 1 | 2.0 |
| npc_vpedestrian | default_camera | 20 | 1 | DialogDefault |
| npc_vpedestrian | default_disposition | 20 | 2 | Default / Neutral |
| npc_vpedestrian | demo_sequence | 18 | 2 | None / sitback_idle_left_deep |
| npc_vpedestrian | dialogname | 6 | 2 | dlg/Downtown LA/Milligan.dlg / dlg/Giovanni/Bruno.dlg |
| npc_vpedestrian | disablereceiveshadows | 18 | 1 | 0 |
| npc_vpedestrian | disableshadows | 20 | 2 | 0 / 1 |
| npc_vpedestrian | floatfreq | 20 | 2 | 0 / 2 |
| npc_vpedestrian | follower_type | 20 | 1 | Default |
| npc_vpedestrian | full_investigate | 19 | 1 | 0 |
| npc_vpedestrian | haircolor | 20 | 1 | 0 0 0 0 |
| npc_vpedestrian | hearing | 20 | 2 | -1.00 / 0 |
| npc_vpedestrian | hint_groups | 20 | 2 | 1 |
| npc_vpedestrian | ignore_detected_attack | 1 | 1 | 1 |
| npc_vpedestrian | interesting_place_groups | 20 | 7 | 0 / 1 / 11 2 / 11 3 / 4 |
| npc_vpedestrian | investigate_mode | 20 | 4 | -1 / 0 / 3 / 4 |
| npc_vpedestrian | investigate_mode_combat | 20 | 4 | -1 / 0 / 3 / 4 |
| npc_vpedestrian | invincible | 18 | 2 | 0 / 1 |
| npc_vpedestrian | is_bossmonster | 5 | 1 | 1 |
| npc_vpedestrian | level_reset_type | 6 | 2 | 1 / 2 |
| npc_vpedestrian | model | 20 | 16 |  |
| npc_vpedestrian | npc_perception | 20 | 2 | 1 / 3 |
| npc_vpedestrian | npc_transparent | 18 | 1 | 1 |
| npc_vpedestrian | ondamaged | 1 | 1 | DJ-playing,Kill,,0,-1,, |
| npc_vpedestrian | ondeath | 4 | 4 | ,,,0,-1,milliganDeath(), / ,,,0,-1,setTawniDead(), / DJ-playing,Kill,,0,-1,, / Relay_Thug_Death,Trigger,,0,-1,, |
| npc_vpedestrian | ondialogend | 1 | 1 | ,,,0,-1,milliganSkylineDialog(), |
| npc_vpedestrian | onfoundplayer | 5 | 5 | ,,,0,-1,G.Warehouse_Spotted = 1, / ,,,0,-1,setSpotted(), / doorknob_front-wesp,Lock,,0,-1,, / unhide_trig_combat_main,Trigger,,0,1,, / unhide_trig_door_close,Trigger,,0,1,, |
| npc_vpedestrian | onhearcombat | 6 | 3 | dancer,FleeAndDie,,0,-1,, / innocent_1,FleeAndDie,,0,-1,, / soundscheme2,FadeIn,1.00,0,1,, |
| npc_vpedestrian | origin | 20 | 20 | -1569.22 -1307.06 33 / -280 -28 136 / -331.51 652.803 1 / -336.702 542.854 1 / -341.393 598.696 4 |
| npc_vpedestrian | percent_occluded_chase | 20 | 2 | 0 / 30 |
| npc_vpedestrian | percent_occluded_cover | 20 | 3 | 0 / 30 / 70 |
| npc_vpedestrian | percent_occluded_flank | 20 | 2 | 0 / 20 |
| npc_vpedestrian | percent_occluded_wait | 20 | 2 | 10 / 100 |
| npc_vpedestrian | percent_occluded_walk | 20 | 3 | 0 / 10 / 20 |
| npc_vpedestrian | physdamagescale | 20 | 1 | 1.0 |
| npc_vpedestrian | pl_criminal_attack | 20 | 3 | -1 / 1 / 6 |
| npc_vpedestrian | pl_criminal_flee | 20 | 2 | 1 / 6 |
| npc_vpedestrian | pl_investigate | 20 | 3 | -1 / 1 / 6 |
| npc_vpedestrian | pl_supernatural_attack | 20 | 2 | -1 / 6 |
| npc_vpedestrian | pl_supernatural_flee | 20 | 3 | 1 / 2 / 6 |
| npc_vpedestrian | player_reaction | 20 | 2 | D_HT 5 / D_NU 0 |
| npc_vpedestrian | renderamt | 20 | 1 | 255 |
| npc_vpedestrian | rendercolor | 20 | 1 | 255 255 255 |
| npc_vpedestrian | renderfx | 20 | 1 | 0 |
| npc_vpedestrian | rendermode | 20 | 1 | 0 |
| npc_vpedestrian | skin | 20 | 1 | 0 |
| npc_vpedestrian | skincolor | 20 | 1 | 0 0 0 0 |
| npc_vpedestrian | soundgroup | 12 | 9 | Asian / Unique/Milligan / citizen_2 / citizen_goth / patron_club/female_dancer_2 |
| npc_vpedestrian | spawnflags | 20 | 3 | 12388 / 4 / 8196 |
| npc_vpedestrian | squadname | 20 | 10 | apartment / barflies / coward / dancers / dj |
| npc_vpedestrian | starthidden | 9 | 2 | 0 / 1 |
| npc_vpedestrian | stattemplate | 20 | 3 | NPCGeneric / VampireGeneric / WarehouseThugFastFood |
| npc_vpedestrian | stay_entrenched | 19 | 1 | 0 |
| npc_vpedestrian | targetname | 20 | 9 | Meeting_Guys / Milligan / Tub_Guy_1 / dancer / innocent |
| npc_vpedestrian | teleport_move_timer | 2 | 1 | 0 |
| npc_vpedestrian | trimcolor | 20 | 1 | 0 0 0 0 |
| npc_vpedestrian | use_interesting | 20 | 2 | 0 / 1 |
| npc_vpedestrian | vision | 20 | 3 | -1 / 0 / 40 |
| npc_vsabbatgunman | additionalequipment | 18 | 5 | item_w_deserteagle / item_w_glock_17c / item_w_ithaca_m_37 / item_w_mac_10 / item_w_uzi |
| npc_vsabbatgunman | allow_alert_lookaround | 18 | 1 | 1 |
| npc_vsabbatgunman | allow_kick_hint_use | 18 | 1 | 1 |
| npc_vsabbatgunman | alternateequipment | 1 | 1 | item_w_knife |
| npc_vsabbatgunman | angles | 18 | 9 | 0 191.5 0 / 0 196 0 / 0 211.5 0 / 0 259 0 / 0 275 0 |
| npc_vsabbatgunman | base_gender | 3 | 1 | 1 |
| npc_vsabbatgunman | bright_route_penalty | 18 | 1 | 0 |
| npc_vsabbatgunman | cantdropweapons | 2 | 1 | 0 |
| npc_vsabbatgunman | classname | 18 | 1 | npc_VSabbatGunman |
| npc_vsabbatgunman | clothescolor1 | 18 | 1 | 0 0 0 0 |
| npc_vsabbatgunman | clothescolor2 | 18 | 1 | 0 0 0 0 |
| npc_vsabbatgunman | combat_start_activity | 18 | 1 | -1 |
| npc_vsabbatgunman | crossfade_skin_time | 18 | 1 | 2.0 |
| npc_vsabbatgunman | default_camera | 18 | 1 | DialogDefault |
| npc_vsabbatgunman | default_disposition | 18 | 1 | Neutral |
| npc_vsabbatgunman | demo_sequence | 15 | 1 | None |
| npc_vsabbatgunman | dialogname | 1 | 1 | dlg/Downtown LA/Bishop_Vick.dlg |
| npc_vsabbatgunman | disablereceiveshadows | 15 | 1 | 0 |
| npc_vsabbatgunman | disableshadows | 18 | 1 | 1 |
| npc_vsabbatgunman | floatfreq | 18 | 1 | 0 |
| npc_vsabbatgunman | follower_type | 18 | 1 | Default |
| npc_vsabbatgunman | full_investigate | 18 | 1 | 0 |
| npc_vsabbatgunman | haircolor | 18 | 1 | 0 0 0 0 |
| npc_vsabbatgunman | hearing | 18 | 3 | -1.0 / -1.00 / 0 |
| npc_vsabbatgunman | hint_groups | 18 | 1 |  |
| npc_vsabbatgunman | interesting_place_groups | 18 | 1 | 0 |
| npc_vsabbatgunman | investigate_mode | 18 | 2 | 1 / 4 |
| npc_vsabbatgunman | investigate_mode_combat | 18 | 2 | 1 / 4 |
| npc_vsabbatgunman | invincible | 18 | 2 | 0 / 1 |
| npc_vsabbatgunman | is_bossmonster | 2 | 1 | 1 |
| npc_vsabbatgunman | model | 18 | 5 |  |
| npc_vsabbatgunman | npc_perception | 18 | 3 | 10 / 3 / 6 |
| npc_vsabbatgunman | npc_transparent | 15 | 1 | 1 |
| npc_vsabbatgunman | ondeath | 6 | 4 | ,,,0,-1,brunoDeath(), / Relay_Open_Doors,Trigger,,0,-1,, / arena_door_knobs,Unlock,,0,-1,, / vick_death_relay,Trigger,,0,-1,, |
| npc_vsabbatgunman | ondialogend | 1 | 1 | arena_combat_relay,Trigger,,0,-1,, |
| npc_vsabbatgunman | onfoundplayer | 1 | 1 |  |
| npc_vsabbatgunman | onhalfhealth | 2 | 1 | spell_timer,Disable,,0,-1,, |
| npc_vsabbatgunman | origin | 18 | 18 | -1364 -1824 244 / -1628 -780 -111 / -387 513 1 / -387 513 1.00 / -59.2 -990.128 -428 |
| npc_vsabbatgunman | percent_occluded_chase | 18 | 2 | 100 / 30 |
| npc_vsabbatgunman | percent_occluded_cover | 18 | 2 | 0 / 30 |
| npc_vsabbatgunman | percent_occluded_flank | 18 | 2 | 0 / 20 |
| npc_vsabbatgunman | percent_occluded_wait | 18 | 2 | 0 / 10 |
| npc_vsabbatgunman | percent_occluded_walk | 18 | 2 | 0 / 10 |
| npc_vsabbatgunman | physdamagescale | 18 | 1 | 1.0 |
| npc_vsabbatgunman | pl_criminal_attack | 18 | 2 | -1 / 6 |
| npc_vsabbatgunman | pl_criminal_flee | 18 | 2 | -1 / 6 |
| npc_vsabbatgunman | pl_investigate | 18 | 2 | -1 / 1 |
| npc_vsabbatgunman | pl_supernatural_attack | 18 | 2 | -1 / 6 |
| npc_vsabbatgunman | pl_supernatural_flee | 18 | 2 | -1 / 6 |
| npc_vsabbatgunman | player_reaction | 18 | 2 | D_HT 5 / D_NU 0 |
| npc_vsabbatgunman | renderamt | 18 | 1 | 255 |
| npc_vsabbatgunman | rendercolor | 18 | 1 | 255 255 255 |
| npc_vsabbatgunman | renderfx | 18 | 1 | 0 |
| npc_vsabbatgunman | rendermode | 18 | 1 | 0 |
| npc_vsabbatgunman | skin | 18 | 1 | 0 |
| npc_vsabbatgunman | skincolor | 18 | 1 | 0 0 0 0 |
| npc_vsabbatgunman | spawnflags | 18 | 1 | 4 |
| npc_vsabbatgunman | squadname | 18 | 6 | arena / celerity_hall_crew / f3_celerity_hall_crew / f4_celerity_hall_crew / meeting |
| npc_vsabbatgunman | starthidden | 13 | 2 | 0 / 1 |
| npc_vsabbatgunman | stattemplate | 18 | 7 | BishopVick / BloodHuntPotence / BloodHuntPresence / Bruno / SabbatCelerityGlock |
| npc_vsabbatgunman | stay_entrenched | 18 | 1 | 0 |
| npc_vsabbatgunman | targetname | 18 | 17 | Bishop Vick / Bruno / celerity_1 / celerity_2 / celerity_3 |
| npc_vsabbatgunman | team_name-wesp | 1 | 1 | giovanni |
| npc_vsabbatgunman | teleport_move_timer | 4 | 2 | 0 / 2 |
| npc_vsabbatgunman | trimcolor | 18 | 1 | 0 0 0 0 |
| npc_vsabbatgunman | use_interesting | 18 | 1 | 0 |
| npc_vsabbatgunman | vision | 18 | 1 | -1 |
| npc_vtzimisce | allow_alert_lookaround | 4 | 1 | 1 |
| npc_vtzimisce | allow_kick_hint_use | 4 | 1 | 1 |
| npc_vtzimisce | angles | 4 | 3 | 0 0 0 / 0 180 0 / 0 90 0 |
| npc_vtzimisce | base_gender | 4 | 1 | 1 |
| npc_vtzimisce | bright_route_penalty | 4 | 1 | 0 |
| npc_vtzimisce | classname | 4 | 1 | npc_VTzimisce |
| npc_vtzimisce | clothescolor1 | 4 | 1 | 0 0 0 0 |
| npc_vtzimisce | clothescolor2 | 4 | 1 | 0 0 0 0 |
| npc_vtzimisce | combat_start_activity | 4 | 1 | -1 |
| npc_vtzimisce | crossfade_skin_time | 4 | 1 | 2.0 |
| npc_vtzimisce | default_camera | 4 | 1 | DialogDefault |
| npc_vtzimisce | default_disposition | 4 | 1 | Neutral |
| npc_vtzimisce | disableshadows | 4 | 1 | 1 |
| npc_vtzimisce | floatfreq | 4 | 1 | 0 |
| npc_vtzimisce | follower_type | 4 | 1 | Default |
| npc_vtzimisce | full_investigate | 4 | 1 | 0 |
| npc_vtzimisce | haircolor | 4 | 1 | 0 0 0 0 |
| npc_vtzimisce | hearing | 4 | 1 | 0.40 |
| npc_vtzimisce | hint_groups | 4 | 1 |  |
| npc_vtzimisce | interesting_place_groups | 4 | 3 | 0 / 3 / 7 |
| npc_vtzimisce | investigate_mode | 4 | 1 | 4 |
| npc_vtzimisce | investigate_mode_combat | 4 | 1 | 4 |
| npc_vtzimisce | invincible | 4 | 1 | 0 |
| npc_vtzimisce | model | 4 | 1 |  |
| npc_vtzimisce | npc_perception | 4 | 1 | 3 |
| npc_vtzimisce | origin | 4 | 4 | -1184 -3616 -995.57 / -680 -544 -536 / 2338.31 -391.087 -483.572 / 3184 2176 -227.572 |
| npc_vtzimisce | percent_occluded_chase | 4 | 1 | 90 |
| npc_vtzimisce | percent_occluded_cover | 4 | 1 | 0 |
| npc_vtzimisce | percent_occluded_flank | 4 | 1 | 0 |
| npc_vtzimisce | percent_occluded_wait | 4 | 1 | 10 |
| npc_vtzimisce | percent_occluded_walk | 4 | 1 | 0 |
| npc_vtzimisce | physdamagescale | 4 | 1 | 1.0 |
| npc_vtzimisce | pl_criminal_attack | 4 | 1 | -1 |
| npc_vtzimisce | pl_criminal_flee | 4 | 1 | -1 |
| npc_vtzimisce | pl_investigate | 4 | 1 | -1 |
| npc_vtzimisce | pl_supernatural_attack | 4 | 1 | -1 |
| npc_vtzimisce | pl_supernatural_flee | 4 | 1 | -1 |
| npc_vtzimisce | player_reaction | 4 | 1 | D_HT 10 |
| npc_vtzimisce | renderamt | 4 | 1 | 255 |
| npc_vtzimisce | rendercolor | 4 | 1 | 255 255 255 |
| npc_vtzimisce | renderfx | 4 | 1 | 0 |
| npc_vtzimisce | rendermode | 4 | 1 | 0 |
| npc_vtzimisce | skin | 4 | 1 | 0 |
| npc_vtzimisce | skincolor | 4 | 1 | 0 0 0 0 |
| npc_vtzimisce | spawnflags | 4 | 1 | 4 |
| npc_vtzimisce | squadname | 4 | 3 | final_squad / tz3 / tz5 |
| npc_vtzimisce | stattemplate | 4 | 1 | TzimisceCreation1 |
| npc_vtzimisce | stay_entrenched | 4 | 1 | 0 |
| npc_vtzimisce | trimcolor | 4 | 1 | 0 0 0 0 |
| npc_vtzimisce | use_interesting | 4 | 2 | 0 / 1 |
| npc_vtzimisce | vision | 4 | 1 | 1000 |
| npc_vtzimisceheadclaw | allow_alert_lookaround | 5 | 2 | 0 / 1 |
| npc_vtzimisceheadclaw | allow_kick_hint_use | 5 | 1 | 1 |
| npc_vtzimisceheadclaw | alternatequipment | 3 | 1 | item_w_tzimisce2_claw |
| npc_vtzimisceheadclaw | angles | 5 | 4 | 0 142 0 / 0 180 0 / 0 185 0 / 0 90 0 |
| npc_vtzimisceheadclaw | base_gender | 2 | 1 | 1 |
| npc_vtzimisceheadclaw | bright_route_penalty | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | classname | 5 | 1 | npc_VTzimisceHeadClaw |
| npc_vtzimisceheadclaw | clothescolor1 | 5 | 1 | 0 0 0 0 |
| npc_vtzimisceheadclaw | clothescolor2 | 5 | 1 | 0 0 0 0 |
| npc_vtzimisceheadclaw | combat_start_activity | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | crossfade_skin_time | 5 | 1 | 2.0 |
| npc_vtzimisceheadclaw | default_camera | 5 | 1 | DialogDefault |
| npc_vtzimisceheadclaw | default_disposition | 5 | 1 | Neutral |
| npc_vtzimisceheadclaw | demo_sequence | 3 | 1 | None |
| npc_vtzimisceheadclaw | disablereceiveshadows | 3 | 1 | 0 |
| npc_vtzimisceheadclaw | disableshadows | 5 | 2 | 0 / 1 |
| npc_vtzimisceheadclaw | floatfreq | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | follower_type | 5 | 1 | Default |
| npc_vtzimisceheadclaw | full_investigate | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | haircolor | 5 | 1 | 0 0 0 0 |
| npc_vtzimisceheadclaw | hearing | 5 | 2 | -1.00 / 0.40 |
| npc_vtzimisceheadclaw | hint_groups | 5 | 1 |  |
| npc_vtzimisceheadclaw | interesting_place_groups | 5 | 3 | 0 / 32 / 4 |
| npc_vtzimisceheadclaw | investigate_mode | 5 | 1 | 4 |
| npc_vtzimisceheadclaw | investigate_mode_combat | 5 | 1 | 4 |
| npc_vtzimisceheadclaw | invincible | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | model | 5 | 1 |  |
| npc_vtzimisceheadclaw | npc_perception | 5 | 1 | 3 |
| npc_vtzimisceheadclaw | npc_transparent | 3 | 1 | 1 |
| npc_vtzimisceheadclaw | onfoundenemy | 1 | 1 | Spawner1,Enable,,0,-1,, |
| npc_vtzimisceheadclaw | origin | 5 | 5 | -10.2377 3672.23 -617 / -336 -724 0 / 2269.79 1398.39 -623 / 2383.57 2151.96 -227.572 / 496 -1673 -716 |
| npc_vtzimisceheadclaw | percent_occluded_chase | 5 | 1 | 30 |
| npc_vtzimisceheadclaw | percent_occluded_cover | 5 | 1 | 30 |
| npc_vtzimisceheadclaw | percent_occluded_flank | 5 | 1 | 20 |
| npc_vtzimisceheadclaw | percent_occluded_wait | 5 | 1 | 10 |
| npc_vtzimisceheadclaw | percent_occluded_walk | 5 | 1 | 10 |
| npc_vtzimisceheadclaw | physdamagescale | 5 | 1 | 1.0 |
| npc_vtzimisceheadclaw | pl_criminal_attack | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | pl_criminal_flee | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | pl_investigate | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | pl_supernatural_attack | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | pl_supernatural_flee | 5 | 1 | -1 |
| npc_vtzimisceheadclaw | player_reaction | 5 | 1 | D_HT 10 |
| npc_vtzimisceheadclaw | renderamt | 5 | 1 | 255 |
| npc_vtzimisceheadclaw | rendercolor | 5 | 1 | 255 255 255 |
| npc_vtzimisceheadclaw | renderfx | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | rendermode | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | skin | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | skincolor | 5 | 1 | 0 0 0 0 |
| npc_vtzimisceheadclaw | spawnflags | 5 | 1 | 4 |
| npc_vtzimisceheadclaw | squadname | 5 | 3 | fatguysteam / tz / tz4 |
| npc_vtzimisceheadclaw | starthidden | 1 | 1 | 0 |
| npc_vtzimisceheadclaw | stattemplate | 5 | 1 | TzimisceCreation2 |
| npc_vtzimisceheadclaw | stay_entrenched | 5 | 1 | 0 |
| npc_vtzimisceheadclaw | targetname | 1 | 1 | f1_creation_1 |
| npc_vtzimisceheadclaw | trimcolor | 5 | 1 | 0 0 0 0 |
| npc_vtzimisceheadclaw | use_interesting | 5 | 2 | 0 / 1 |
| npc_vtzimisceheadclaw | vision | 5 | 4 | -1 / 1500 / 3600 / 800 |
| npc_vtzimiscerunner | allow_alert_lookaround | 19 | 2 | 0 / 1 |
| npc_vtzimiscerunner | allow_kick_hint_use | 19 | 1 | 1 |
| npc_vtzimiscerunner | angles | 19 | 15 | 0 0 0 / 0 159 0 / 0 16 0 / 0 160 0 / 0 197 0 |
| npc_vtzimiscerunner | base_gender | 5 | 1 | 1 |
| npc_vtzimiscerunner | bright_route_penalty | 19 | 1 | 0 |
| npc_vtzimiscerunner | classname | 19 | 1 | npc_VTzimisceRunner |
| npc_vtzimiscerunner | clothescolor1 | 19 | 1 | 0 0 0 0 |
| npc_vtzimiscerunner | clothescolor2 | 19 | 1 | 0 0 0 0 |
| npc_vtzimiscerunner | combat_start_activity | 19 | 1 | -1 |
| npc_vtzimiscerunner | crossfade_skin_time | 19 | 1 | 2.0 |
| npc_vtzimiscerunner | default_camera | 19 | 1 | DialogDefault |
| npc_vtzimiscerunner | default_disposition | 19 | 1 | Neutral |
| npc_vtzimiscerunner | demo_sequence | 14 | 2 | None / scripted_jumping_down_attack |
| npc_vtzimiscerunner | disablereceiveshadows | 14 | 1 | 0 |
| npc_vtzimiscerunner | disableshadows | 19 | 1 | 1 |
| npc_vtzimiscerunner | floatfreq | 19 | 1 | 0 |
| npc_vtzimiscerunner | follower_type | 19 | 1 | Default |
| npc_vtzimiscerunner | full_investigate | 19 | 1 | 0 |
| npc_vtzimiscerunner | haircolor | 19 | 1 | 0 0 0 0 |
| npc_vtzimiscerunner | hearing | 19 | 4 | -1.00 / 0.40 / 0.75 / 1.00 |
| npc_vtzimiscerunner | hint_groups | 19 | 1 |  |
| npc_vtzimiscerunner | interesting_place_groups | 19 | 8 | 0 / 1 / 2 / 29 / 3 |
| npc_vtzimiscerunner | investigate_mode | 19 | 1 | 4 |
| npc_vtzimiscerunner | investigate_mode_combat | 19 | 1 | 4 |
| npc_vtzimiscerunner | invincible | 19 | 1 | 0 |
| npc_vtzimiscerunner | model | 19 | 1 |  |
| npc_vtzimiscerunner | npc_perception | 19 | 2 | 3 / 4 |
| npc_vtzimiscerunner | npc_transparent | 14 | 1 | 1 |
| npc_vtzimiscerunner | onfoundplayer | 2 | 2 | Spawner1,Enable,,0,-1,, / Spawner2,Enable,,0,-1,, |
| npc_vtzimiscerunner | origin | 19 | 19 | -193 959 6 / -242.038 3749.66 -613 / -741.633 -448.132 -730.532 / -766.933 3057.7 -723.293 / -770.471 2553.83 -716.506 |
| npc_vtzimiscerunner | percent_occluded_chase | 19 | 2 | 30 / 50 |
| npc_vtzimiscerunner | percent_occluded_cover | 19 | 2 | 10 / 30 |
| npc_vtzimiscerunner | percent_occluded_flank | 19 | 2 | 0 / 20 |
| npc_vtzimiscerunner | percent_occluded_wait | 19 | 2 | 0 / 10 |
| npc_vtzimiscerunner | percent_occluded_walk | 19 | 2 | 10 / 40 |
| npc_vtzimiscerunner | physdamagescale | 19 | 1 | 1.0 |
| npc_vtzimiscerunner | pl_criminal_attack | 19 | 2 | -1 / 6 |
| npc_vtzimiscerunner | pl_criminal_flee | 19 | 2 | -1 / 6 |
| npc_vtzimiscerunner | pl_investigate | 19 | 2 | -1 / 1 |
| npc_vtzimiscerunner | pl_supernatural_attack | 19 | 2 | -1 / 1 |
| npc_vtzimiscerunner | pl_supernatural_flee | 19 | 2 | -1 / 6 |
| npc_vtzimiscerunner | player_reaction | 19 | 1 | D_HT 10 |
| npc_vtzimiscerunner | radius | 1 | 1 | 552 |
| npc_vtzimiscerunner | renderamt | 19 | 1 | 255 |
| npc_vtzimiscerunner | rendercolor | 19 | 1 | 255 255 255 |
| npc_vtzimiscerunner | renderfx | 19 | 1 | 0 |
| npc_vtzimiscerunner | rendermode | 19 | 1 | 0 |
| npc_vtzimiscerunner | skin | 19 | 1 | 0 |
| npc_vtzimiscerunner | skincolor | 19 | 1 | 0 0 0 0 |
| npc_vtzimiscerunner | spawnflags | 19 | 1 | 4 |
| npc_vtzimiscerunner | squadname | 19 | 7 | Runners / Runners2 / squad_bob / tz / tz2 |
| npc_vtzimiscerunner | starthidden | 1 | 1 | 1 |
| npc_vtzimiscerunner | stattemplate | 19 | 1 | TzimisceCreation3 |
| npc_vtzimiscerunner | stay_entrenched | 19 | 1 | 0 |
| npc_vtzimiscerunner | targetname | 1 | 1 | BobDeathFromAbove |
| npc_vtzimiscerunner | trimcolor | 19 | 1 | 0 0 0 0 |
| npc_vtzimiscerunner | use_interesting | 19 | 2 | 0 / 1 |
| npc_vtzimiscerunner | vision | 19 | 5 | -1 / 1500 / 540 / 550 / 600 |
| npc_vvampire | additionalequipment | 17 | 7 | 0 / item_w_avamp_blade / item_w_claws / item_w_deserteagle / item_w_fists |
| npc_vvampire | allow_alert_lookaround | 17 | 1 | 1 |
| npc_vvampire | allow_kick_hint_use | 17 | 1 | 1 |
| npc_vvampire | alternateequipment | 5 | 2 | 0 / item_w_katana |
| npc_vvampire | angles | 17 | 11 | 0 0 0 / 0 135 0 / 0 180 0 / 0 211.5 0 / 0 270 0 |
| npc_vvampire | base_gender | 9 | 1 | 1 |
| npc_vvampire | bright_route_penalty | 17 | 1 | 0 |
| npc_vvampire | classname | 17 | 1 | npc_VVampire |
| npc_vvampire | clothescolor1 | 17 | 1 | 0 0 0 0 |
| npc_vvampire | clothescolor2 | 17 | 1 | 0 0 0 0 |
| npc_vvampire | combat_start_activity | 17 | 1 | -1 |
| npc_vvampire | crossfade_skin_time | 17 | 1 | 2.0 |
| npc_vvampire | default_camera | 17 | 1 | DialogDefault |
| npc_vvampire | default_disposition | 17 | 1 | Neutral |
| npc_vvampire | demo_sequence | 9 | 1 | None |
| npc_vvampire | dialogname | 1 | 1 | dlg/Main Characters/Regent.dlg |
| npc_vvampire | disablereceiveshadows | 8 | 1 | 0 |
| npc_vvampire | disableshadows | 17 | 2 | 0 / 1 |
| npc_vvampire | floatfreq | 16 | 1 | 0 |
| npc_vvampire | follower_type | 17 | 1 | Default |
| npc_vvampire | full_investigate | 16 | 1 | 0 |
| npc_vvampire | haircolor | 17 | 1 | 0 0 0 0 |
| npc_vvampire | hearing | 17 | 2 | -1.00 / 100 |
| npc_vvampire | hint_groups | 17 | 1 |  |
| npc_vvampire | interesting_place_groups | 17 | 2 | 0 / 32 |
| npc_vvampire | investigate_mode | 17 | 2 | 1 / 4 |
| npc_vvampire | investigate_mode_combat | 17 | 1 | 4 |
| npc_vvampire | invincible | 14 | 1 | 0 |
| npc_vvampire | model | 17 | 10 |  |
| npc_vvampire | npc_perception | 17 | 2 | 3 / 5 |
| npc_vvampire | npc_transparent | 9 | 1 | 1 |
| npc_vvampire | ondialogend | 1 | 1 | ,,,0,-1,regentDialog(), |
| npc_vvampire | onfoundplayer | 8 | 8 |  |
| npc_vvampire | origin | 17 | 17 | -1091 -460 3801 / -1192 164 -111 / -1213 -165 3801 / -1452 276 -111 / -1456 348 -111 |
| npc_vvampire | percent_occluded_chase | 17 | 1 | 30 |
| npc_vvampire | percent_occluded_cover | 17 | 1 | 30 |
| npc_vvampire | percent_occluded_flank | 17 | 1 | 20 |
| npc_vvampire | percent_occluded_wait | 17 | 1 | 10 |
| npc_vvampire | percent_occluded_walk | 17 | 1 | 10 |
| npc_vvampire | physdamagescale | 17 | 1 | 1.0 |
| npc_vvampire | pl_criminal_attack | 17 | 2 | -1 / 2 |
| npc_vvampire | pl_criminal_flee | 17 | 2 | -1 / 6 |
| npc_vvampire | pl_investigate | 17 | 2 | -1 / 6 |
| npc_vvampire | pl_supernatural_attack | 17 | 2 | -1 / 2 |
| npc_vvampire | pl_supernatural_flee | 17 | 2 | -1 / 6 |
| npc_vvampire | player_reaction | 17 | 3 | D_HT 10 / D_HT 5 / D_NU 0 |
| npc_vvampire | renderamt | 17 | 1 | 255 |
| npc_vvampire | rendercolor | 17 | 1 | 255 255 255 |
| npc_vvampire | renderfx | 17 | 1 | 0 |
| npc_vvampire | rendermode | 17 | 1 | 0 |
| npc_vvampire | skin | 17 | 1 | 0 |
| npc_vvampire | skincolor | 17 | 1 | 0 0 0 0 |
| npc_vvampire | soundgroup | 8 | 1 | Unique/E |
| npc_vvampire | spawnflags | 17 | 1 | 4 |
| npc_vvampire | squadname | 17 | 7 | chantry / docking_bay / patrollers / snipers / squad_1 |
| npc_vvampire | starthidden | 5 | 2 | 0 / 1 |
| npc_vvampire | stattemplate | 17 | 8 | BloodHuntFortitude / BloodHuntPotence / BloodHuntPresence / BloodHuntProtean / SabbatWithPotence |
| npc_vvampire | stay_entrenched | 16 | 2 | 0 / 1 |
| npc_vvampire | targetname | 17 | 16 | Regent / auspex_1 / f4_m_protean_heather_1 / fortitude_1 / fortitude_2 |
| npc_vvampire | teleport_move_timer | 8 | 1 | 0 |
| npc_vvampire | trimcolor | 17 | 1 | 0 0 0 0 |
| npc_vvampire | use_interesting | 17 | 2 | 0 / 1 |
| npc_vvampire | vision | 17 | 3 | -1 / 1500 / 700 |
| npc_vzombie | additionalequipment | 45 | 1 | item_w_fists |
| npc_vzombie | allow_alert_lookaround | 45 | 1 | 0 |
| npc_vzombie | allow_kick_hint_use | 45 | 2 | 0 / 1 |
| npc_vzombie | angles | 45 | 30 | 0 0 0 / 0 109 0 / 0 129 0 / 0 138 0 / 0 141 0 |
| npc_vzombie | bright_route_penalty | 45 | 1 | 0 |
| npc_vzombie | cantdropweapons | 12 | 1 | 1 |
| npc_vzombie | classname | 45 | 1 | npc_VZombie |
| npc_vzombie | clothescolor1 | 45 | 1 | 0 0 0 0 |
| npc_vzombie | clothescolor2 | 45 | 1 | 0 0 0 0 |
| npc_vzombie | combat_start_activity | 45 | 2 | -1 / ACT_INVALID |
| npc_vzombie | conflict_range | 45 | 1 | 100.0 |
| npc_vzombie | crossfade_skin_time | 45 | 1 | 2.0 |
| npc_vzombie | default_camera | 45 | 1 | DialogDefault |
| npc_vzombie | default_disposition | 45 | 1 | Neutral |
| npc_vzombie | demo_sequence | 45 | 1 | None |
| npc_vzombie | disablereceiveshadows | 45 | 1 | 0 |
| npc_vzombie | disableshadows | 45 | 1 | 1 |
| npc_vzombie | floatfreq | 45 | 1 | 0 |
| npc_vzombie | follower_type | 45 | 2 | Combat / Default |
| npc_vzombie | friendship_level | 45 | 1 | 1 |
| npc_vzombie | full_investigate | 45 | 1 | 0 |
| npc_vzombie | haircolor | 45 | 1 | 0 0 0 0 |
| npc_vzombie | hearing | 45 | 1 | -1.00 |
| npc_vzombie | hint_groups | 45 | 2 | 32 |
| npc_vzombie | interesting_place_groups | 45 | 3 | 0 / 2 / 32 |
| npc_vzombie | investigate_mode | 45 | 1 | 4 |
| npc_vzombie | investigate_mode_combat | 45 | 1 | 4 |
| npc_vzombie | invincible | 45 | 1 | 0 |
| npc_vzombie | model | 45 | 6 |  |
| npc_vzombie | npc_perception | 45 | 1 | 3 |
| npc_vzombie | npc_transparent | 45 | 1 | 1 |
| npc_vzombie | ondamaged | 2 | 1 | Relay_Kill_Nadia,Trigger,,0,-1,, |
| npc_vzombie | ondeath | 18 | 2 | ,,,0,-1,gio3_checkAllZombieDead(), / ,,,0,-1,zombieKillCounter(), |
| npc_vzombie | origin | 45 | 45 | -1280 -1536 220 / -1408 -1216 220 / -1472 -1536 220 / -1552 3336 -894 / -1552 757 -894 |
| npc_vzombie | percent_occluded_chase | 45 | 1 | 30 |
| npc_vzombie | percent_occluded_cover | 45 | 1 | 30 |
| npc_vzombie | percent_occluded_flank | 45 | 1 | 20 |
| npc_vzombie | percent_occluded_wait | 45 | 1 | 10 |
| npc_vzombie | percent_occluded_walk | 45 | 1 | 10 |
| npc_vzombie | physdamagescale | 45 | 1 | 1.0 |
| npc_vzombie | pl_criminal_attack | 45 | 2 | -1 / 6 |
| npc_vzombie | pl_criminal_flee | 45 | 2 | -1 / 6 |
| npc_vzombie | pl_investigate | 45 | 2 | -1 / 6 |
| npc_vzombie | pl_supernatural_attack | 45 | 2 | -1 / 6 |
| npc_vzombie | pl_supernatural_flee | 45 | 2 | -1 / 6 |
| npc_vzombie | player_reaction | 45 | 3 | D_HT 10 / D_HT 5 / D_NU 0 |
| npc_vzombie | remove_distance | 45 | 2 | 1024.0 / 9999 |
| npc_vzombie | renderamt | 45 | 1 | 255 |
| npc_vzombie | rendercolor | 45 | 1 | 255 255 255 |
| npc_vzombie | renderfx | 45 | 1 | 0 |
| npc_vzombie | rendermode | 45 | 1 | 0 |
| npc_vzombie | should_ragdoll | 45 | 1 | 1 |
| npc_vzombie | skin | 45 | 1 | 0 |
| npc_vzombie | skincolor | 45 | 1 | 0 0 0 0 |
| npc_vzombie | spawnflags | 45 | 1 | 4 |
| npc_vzombie | squadname | 45 | 3 | arena / floor2_plague_victim_squad / squad_zombie |
| npc_vzombie | starthidden | 23 | 2 | 0 / 1 |
| npc_vzombie | stattemplate | 45 | 2 | CrackhousePlagueVictim / Zombie |
| npc_vzombie | stay_entrenched | 45 | 1 | 0 |
| npc_vzombie | targetname | 45 | 10 | Entrance_Zombie / Exit_Zombie / arena_plague_victim / floor2_plague_victim / hall_zombie |
| npc_vzombie | team_name-wesp | 11 | 1 | plague_victim_damage_team |
| npc_vzombie | teleport_move_timer | 45 | 1 | 0 |
| npc_vzombie | trimcolor | 45 | 1 | 0 0 0 0 |
| npc_vzombie | use_interesting | 45 | 2 | 0 / 1 |
| npc_vzombie | vision | 45 | 1 | -1 |
| npc_vzombie | warn_range | 45 | 1 | 200.0 |
| npc_vzombie | zombieaitype | 12 | 2 | 1 / 8 |

### Hint types
| classname | hinttype | rows | maps |
| --- | --- | --- | --- |
| info_hint | 10100 | 17 | 2 |
| info_hint | 19000 | 12 | 1 |
| info_node_bach_run_1 | 17004 | 1 | 1 |
| info_node_bach_run_2 | 17005 | 1 | 1 |
| info_node_bach_teleport_1 | 17000 | 25 | 4 |
| info_node_bach_teleport_2 | 17001 | 9 | 2 |
| info_node_bach_teleport_3 | 17002 | 2 | 2 |
| info_node_bach_teleport_4 | 17003 | 1 | 1 |
| info_node_chang_column | 18001 | 2 | 2 |
| info_node_chang_jumpbase | 18000 | 6 | 2 |
| info_node_chang_ledge | 18003 | 37 | 4 |
| info_node_chang_teleport | 18002 | 43 | 1 |
| info_node_climb | 10000 | 4 | 1 |
| info_node_cover_corner | 10200 | 1438 | 34 |
| info_node_cover_low | 101 | 269 | 28 |
| info_node_cover_med | 100 | 431 | 30 |
| info_node_crosswalk | 11000 | 22 | 4 |
| info_node_hint | 10000 | 34 | 4 |
| info_node_hint | 10400 | 2 | 1 |
| info_node_kick_over | 10300 | 1 | 1 |
| info_node_manbat_fly_to_point | 20000 | 13 | 1 |
| info_node_patrol_point | 10000 | 582 | 33 |
| info_node_sabbat_arch | 16002 | 15 | 1 |
| info_node_sabbat_bottom | 16000 | 11 | 1 |
| info_node_sabbat_dive | 16005 | 6 | 1 |
| info_node_sabbat_hide | 16003 | 5 | 1 |
| info_node_sabbat_nojump | 16004 | 3 | 1 |
| info_node_sabbat_top | 16001 | 12 | 1 |
| info_node_shoot_at | 10400 | 20 | 3 |
| info_node_werewolf_hint | 15004 | 38 | 1 |
| info_node_werewolf_hint | 15001 | 25 | 1 |
| info_node_werewolf_hint | 15000 | 11 | 1 |
| info_node_werewolf_hint | 15005 | 9 | 1 |
| info_node_werewolf_hint | 15006 | 9 | 1 |
| info_node_werewolf_hint | 15012 | 9 | 1 |
| info_node_werewolf_hint | 15002 | 8 | 1 |
| info_node_werewolf_hint | 15003 | 6 | 1 |
| info_node_werewolf_hint | 15016 | 5 | 1 |
| info_node_werewolf_hint | 15011 | 3 | 1 |
| info_node_werewolf_hint | 15017 | 3 | 1 |
| info_node_werewolf_hint | 15008 | 2 | 1 |
| info_node_werewolf_hint | 15013 | 1 | 1 |
| info_node_werewolf_hint | 15014 | 1 | 1 |
| info_node_werewolf_hint | 15015 | 1 | 1 |
| info_node_werewolf_hint | 15018 | 1 | 1 |

### Interesting-place groups
| group_id | rows | maps | enabled=1 | enabled=0 | min_time | max_time |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 242 | 39 | 229 | 13 | 0.0 | 23523235.0 |
| 19 | 138 | 12 | 138 | 0 | 5.0 | 6000.0 |
| 11 | 104 | 14 | 64 | 40 | 0.0 | 500000.0 |
| 2 | 95 | 31 | 81 | 14 | 0.0 | 50000.0 |
| 8 | 77 | 21 | 72 | 5 | 0.0 | 500000.0 |
| 3 | 69 | 25 | 63 | 6 | 0.0 | 99999.0 |
| 32 | 56 | 22 | 48 | 8 | 0.0 | 50000.0 |
| 4 | 56 | 19 | 53 | 3 | 0.0 | 99999.0 |
| 13 | 37 | 14 | 33 | 4 | 0.0 | 10000.0 |
| 5 | 37 | 16 | 34 | 3 | 0.0 | 99999.0 |
| 6 | 36 | 16 | 31 | 5 | 0.0 | 99999.0 |
| 14 | 35 | 13 | 31 | 4 | 5.0 | 9999.0 |
| 9 | 33 | 14 | 27 | 6 | 0.0 | 50000.0 |
| 12 | 32 | 10 | 30 | 2 | 0.0 | 10000.0 |
| 7 | 32 | 14 | 28 | 4 | 0.0 | 500000.0 |
| 10 | 29 | 13 | 22 | 7 | 0.0 | 50000.0 |
| 15 | 20 | 11 | 19 | 1 | 5.0 | 10000.0 |
| 20 | 20 | 4 | 20 | 0 | 5.0 | 1000.0 |
| 31 | 20 | 9 | 17 | 3 | 0.5 | 9999.0 |
| 17 | 19 | 6 | 12 | 7 | 5.0 | 1000.0 |
| 18 | 17 | 8 | 15 | 2 | 3.0 | 6000.0 |
| 16 | 14 | 7 | 14 | 0 | 10.0 | 1000.0 |
| 22 | 13 | 3 | 9 | 4 | 5.0 | 40.0 |
| 30 | 10 | 6 | 5 | 5 | 0.0 | 10000.0 |
| 21 | 9 | 5 | 9 | 0 | 5.0 | 1000.0 |
| 23 | 9 | 3 | 8 | 1 | 5.0 | 20.0 |
| 28 | 7 | 5 | 6 | 1 | 30.0 | 50000.0 |
| 29 | 7 | 4 | 6 | 1 | 5.0 | 50000.0 |
| 26 | 6 | 4 | 4 | 2 | 20.0 | 500000.0 |
| 27 | 5 | 4 | 4 | 1 | 20.0 | 500000.0 |
| 25 | 4 | 4 | 4 | 0 | 5.0 | 50000.0 |
| 24 | 2 | 2 | 2 | 0 | 5.0 | 1000.0 |
| 41 | 1 | 1 | 1 | 0 | 5.0 | 10.0 |
| 43 | 1 | 1 | 1 | 0 | 5.0 | 10.0 |
| 44 | 1 | 1 | 1 | 0 | 5.0 | 10.0 |

### Keys authored on `info_node_patrol_point`
| key authored on info_node_patrol_point |
| --- |
| angles |
| classname |
| group |
| group_id |
| hint_rating |
| hinttype |
| ip_percent |
| nodeid |
| origin |
| starthidden |
| starthintdisabled |
| target_angle_range |
| target_dist_max |
| target_dist_min |
| target_name |
| targetname |

### Conversation places
| key on intersting_place_conversation | rows |
| --- | --- |
|  | 2 |
| angles | 7 |
| audible_dist | 49 |
| classname | 49 |
| enabled | 49 |
| interesting_places | 49 |
| max_time | 49 |
| min_time | 49 |
| ononeoffsoundcomplete | 14 |
| onplayertooclose | 2 |
| origin | 49 |
| player_dist | 49 |
| sound_loop | 35 |
| sound_occluded | 49 |
| sound_once | 20 |
| targetname | 33 |
| turn_towards_talker | 49 |

### Makers by `NPCType`
| map | NPCType | maker requests | maker classnames |
| --- | --- | --- | --- |
| ch_hub_1 | npc_VCop | 17 | npc_maker |
| ch_hub_1 | npc_VHunter | 3 | npc_maker |
| ch_hub_1 | npc_VRat | 8 | npc_maker |
| ch_lotus_1 | npc_VHumanCombatant | 4 | npc_maker |
| ch_temple_1 | npc_VHumanCombatant | 2 | npc_maker |
| ch_temple_2 | npc_VHumanCombatant | 13 | npc_maker |
| ch_temple_3 | npc_VHumanCombatant | 6 | npc_maker |
| ch_temple_4 | npc_VMingXiao | 1 | npc_maker |
| ch_temple_4 | npc_VMingXiaoTentacle | 1 | npc_maker |
| hw_609_1 | npc_VTzimisceRunner | 4 | npc_maker_fleshpile |
| hw_cemetery_1 | npc_VZombie | 67 | npc_maker_zombie |
| hw_hub_1 | npc_VCop | 39 | npc_maker |
| hw_hub_1 | npc_VHunter | 3 | npc_maker |
| hw_hub_1 | npc_VRat | 15 | npc_maker |
| hw_jewelry_1 | npc_VTzimisceRunner | 4 | npc_maker_fleshpile |
| hw_warrens_1 | npc_VTzimisceRunner | 2 | npc_maker |
| hw_warrens_3 | npc_VTzimisceRunner | 2 | npc_maker_fleshpile |
| hw_warrens_4 | npc_VTzimisceRunner | 2 | npc_maker_fleshpile |
| la_crackhouse_1 | npc_VZombie | 27 | npc_maker_zombie |
| la_dane_1 |  | 7 | npc_maker |
| la_hospital_1 | npc_VZombie | 3 | npc_maker_zombie |
| la_hub_1 | npc_VCop | 27 | npc_maker |
| la_hub_1 | npc_VDialogPedestrian | 1 | npc_maker |
| la_hub_1 | npc_VHunter | 3 | npc_maker |
| la_hub_1 | npc_VRat | 9 | npc_maker |
| la_museum_1 | npc_VHuman | 2 | npc_maker |
| la_museum_1 | npc_VHumanCombatant | 5 | npc_maker |
| la_plaguebearer_sewer_1 | npc_VRat | 13 | npc_maker |
| la_ventruetower_1b | npc_VHumanCombatant | 7 | npc_maker |
| la_ventruetower_3 | npc_VHumanCombatant | 4 | npc_maker |
| sm_hub_1 | npc_VCop | 33 | npc_maker |
| sm_hub_1 | npc_VHuman | 4 | npc_maker |
| sm_hub_1 | npc_VHunter | 3 | npc_maker |
| sm_hub_1 | npc_VRat | 8 | npc_maker |
| sm_medical_1 | npc_VHumanCombatant | 2 | npc_maker |
| sm_pier_1 | npc_VCop | 3 | npc_maker |
| sm_warehouse_1 | npc_VHumanCombatant | 2 | npc_maker |
| sp_giovanni_1 | npc_VHumanCombatant | 3 | npc_maker |
| sp_giovanni_2a | npc_VHumanCombatant | 3 | npc_maker |
| sp_giovanni_2a | npc_VZombie | 4 | npc_maker_zombie |
| sp_giovanni_2b | npc_VHumanCombatant | 3 | npc_maker |
| sp_giovanni_2b | npc_VZombie | 4 | npc_maker_zombie |
| sp_giovanni_3 | npc_VZombie | 9 | npc_maker_zombie |
| sp_soc_1 | npc_VVampire | 4 | npc_maker |
| sp_tutorial_1 | npc_VPedestrian | 3 | npc_maker |
| sp_tutorial_1 | npc_VRat | 3 | npc_maker |
| sp_tutorial_1 | npc_VVampire | 8 | npc_maker |

### Squads by map
| map | squad | placed members | maker requests |
| --- | --- | --- | --- |
| ch_fishmarket_1 | Yukie | 2 | 0 |
| ch_fulab_1 | BelmontTeam | 5 | 0 |
| ch_fulab_1 | guards | 19 | 0 |
| ch_glaze_1 | barflies | 2 | 0 |
| ch_glaze_1 | dancers | 4 | 0 |
| ch_glaze_1 | dj | 1 | 0 |
| ch_glaze_1 | djs | 1 | 0 |
| ch_glaze_1 | squad_1 | 12 | 0 |
| ch_glaze_1 | talkers | 2 | 0 |
| ch_hub_1 | Cops | 0 | 12 |
| ch_hub_1 | hunters | 0 | 3 |
| ch_lotus_1 | tong | 4 | 1 |
| ch_lotus_1 | tong2 | 7 | 4 |
| ch_lotus_1 | tongBase | 4 | 1 |
| ch_ramen_1 | squad_backroom | 2 | 0 |
| ch_ramen_1 | squad_backroom_k | 4 | 0 |
| ch_temple_1 | center | 6 | 0 |
| ch_temple_1 | front | 4 | 0 |
| ch_temple_1 | squad_guard2 | 1 | 0 |
| ch_temple_1 | temple_front | 7 | 2 |
| ch_temple_1 | temple_inside | 2 | 0 |
| ch_temple_1 | temple_inside2 | 2 | 0 |
| ch_temple_1 | west_barracks | 5 | 0 |
| ch_temple_2 | barracks | 10 | 3 |
| ch_temple_2 | central | 3 | 0 |
| ch_temple_2 | kitchen | 7 | 2 |
| ch_temple_2 | lowerlevel_botch | 5 | 2 |
| ch_temple_2 | recroom | 3 | 2 |
| ch_temple_2 | squad_bathroom | 1 | 0 |
| ch_temple_2 | squad_central | 1 | 0 |
| ch_temple_2 | squad_room1 | 4 | 0 |
| ch_temple_2 | squad_waterwheel | 8 | 0 |
| ch_temple_2 | wheelroom | 13 | 4 |
| ch_temple_3 | BackUp_1 | 2 | 1 |
| ch_temple_3 | BackUp_2 | 2 | 1 |
| ch_temple_3 | squad_%i | 16 | 0 |
| ch_temple_3 | squad_center | 4 | 0 |
| ch_temple_3 | squad_patrol | 3 | 0 |
| ch_temple_4 | MingXiao | 3 | 2 |
| ch_zhaos_1 | tong_squad | 17 | 0 |
| ch_zhaos_1 | zhaos_squad | 7 | 0 |
| hw_609_1 | runners | 4 | 4 |
| hw_ash_sewer_1 | hunters_1 | 3 | 0 |
| hw_ash_sewer_1 | hunters_2 | 5 | 0 |
| hw_ash_sewer_1 | hunters_3 | 3 | 0 |
| hw_ash_sewer_1 | hunters_4 | 7 | 0 |
| hw_ash_sewer_1 | hunters_5 | 3 | 0 |
| hw_cemetery_1 | zombie_squad | 0 | 58 |
| hw_cemetery_1 | zombie_squad_female | 4 | 5 |
| hw_cemetery_1 | zombie_squad_male | 63 | 4 |
| hw_hub_1 | cops | 0 | 11 |
| hw_hub_1 | hunters | 0 | 3 |
| hw_hub_1 | thugs | 4 | 0 |
| hw_jewelry_1 | runners | 0 | 4 |
| hw_netcafe_1 | squad_bob | 1 | 0 |
| hw_tawni_1 | apartment | 2 | 0 |
| hw_warrens_1 | Runners | 3 | 1 |
| hw_warrens_1 | Runners2 | 2 | 1 |
| hw_warrens_2 | Runners | 2 | 0 |
| hw_warrens_2b | final_squad | 1 | 0 |
| hw_warrens_2b | tz4 | 1 | 0 |
| hw_warrens_3 | tz | 4 | 1 |
| hw_warrens_3 | tz2 | 2 | 0 |
| hw_warrens_3 | tz3 | 2 | 0 |
| hw_warrens_3 | tz4 | 3 | 1 |
| hw_warrens_4 | final_squad | 3 | 2 |
| hw_warrens_4 | tz | 2 | 0 |
| hw_warrens_4 | tz2 | 1 | 0 |
| hw_warrens_4 | tz3 | 2 | 0 |
| hw_warrens_4 | tz4 | 2 | 0 |
| hw_warrens_4 | tz5 | 1 | 0 |
| la_bradbury_2 | celerity_hall_crew | 5 | 0 |
| la_bradbury_2 | docking_bay | 4 | 0 |
| la_bradbury_2 | f3_celerity_hall_crew | 5 | 0 |
| la_bradbury_2 | f4_celerity_hall_crew | 2 | 0 |
| la_bradbury_2 | fatguysteam | 1 | 0 |
| la_bradbury_2 | shadow_gunners | 3 | 0 |
| la_bradbury_3 | docking_bay | 2 | 0 |
| la_chantry_1 | chantry | 1 | 0 |
| la_confession_1 | djs | 1 | 0 |
| la_confession_1 | russianmafia | 1 | 0 |
| la_crackhouse_1 | arena | 12 | 0 |
| la_crackhouse_1 | floor2_plague_victim_squad | 1 | 0 |
| la_crackhouse_1 | spawned_zombies | 0 | 27 |
| la_dane_1 | deck1 | 6 | 0 |
| la_dane_1 | Deck2 | 2 | 2 |
| la_dane_1 | Deck3 | 3 | 3 |
| la_dane_1 | Deck4 | 5 | 0 |
| la_dane_1 | deck5 | 6 | 2 |
| la_dane_1 | dirtycop | 1 | 0 |
| la_dane_1 | Stern | 2 | 0 |
| la_empire_2 | russianmafia | 9 | 0 |
| la_hospital_1 | Zombies | 0 | 3 |
| la_hub_1 | Cops | 0 | 14 |
| la_hub_1 | hunters | 0 | 3 |
| la_hub_1 | jets | 4 | 0 |
| la_hub_1 | russians | 2 | 0 |
| la_hub_1 | sharks | 3 | 0 |
| la_hub_1 | squadigor | 1 | 0 |
| la_library_1 | squad_library | 2 | 0 |
| la_malkavian_2 | dining_hall | 5 | 0 |
| la_malkavian_2 | entrance | 3 | 0 |
| la_malkavian_2 | great_hall | 4 | 0 |
| la_malkavian_2 | parlor_1 | 2 | 0 |
| la_malkavian_2 | parlor_2 | 3 | 0 |
| la_malkavian_2 | trophy_room | 2 | 0 |
| la_malkavian_3 | cells1 | 4 | 0 |
| la_malkavian_3 | cells2 | 3 | 0 |
| la_malkavian_3 | cells3 | 2 | 0 |
| la_malkavian_3 | labs | 7 | 0 |
| la_malkavian_4 | dining_hall | 2 | 0 |
| la_malkavian_4 | fire_escape_2 | 4 | 0 |
| la_malkavian_4 | fire_escape_3 | 2 | 0 |
| la_malkavian_4 | great_hall | 1 | 0 |
| la_museum_1 | squad_basement | 12 | 3 |
| la_museum_1 | squad_basement_2 | 4 | 2 |
| la_museum_1 | squad_gallery | 6 | 2 |
| la_parkinggarage_1 | first_floor_gang | 6 | 0 |
| la_parkinggarage_1 | first_floor_guards | 2 | 0 |
| la_parkinggarage_1 | fourth_floor_gang | 9 | 0 |
| la_parkinggarage_1 | fourth_floor_tong | 6 | 0 |
| la_parkinggarage_1 | second_floor_guards | 1 | 0 |
| la_parkinggarage_1 | second_floor_tong | 8 | 0 |
| la_parkinggarage_1 | third_floor_gang | 10 | 0 |
| la_skyline_1 | milligan | 1 | 0 |
| la_ventruetower_1b | first_floor_thugs | 5 | 0 |
| la_ventruetower_1b | floor_2 | 8 | 0 |
| la_ventruetower_1b | squad_3 | 5 | 4 |
| la_ventruetower_1b | squad_4 | 4 | 0 |
| la_ventruetower_2 | floor_1 | 6 | 0 |
| la_ventruetower_2 | patrollers | 4 | 0 |
| la_ventruetower_2 | snipers | 2 | 0 |
| la_ventruetower_2 | squad_3 | 8 | 0 |
| la_ventruetower_3 | CopSquad | 4 | 3 |
| la_ventruetower_3 | ManBat Squad | 2 | 1 |
| sm_beachhouse_1 | beachhouse | 7 | 0 |
| sm_diner_1 | assassins | 4 | 0 |
| sm_hub_1 | cops | 31 | 8 |
| sm_hub_1 | hunters | 0 | 3 |
| sm_hub_2 | squad_1 | 3 | 0 |
| sm_hub_2 | squad_2 | 2 | 0 |
| sm_hub_2 | squad_3 | 3 | 0 |
| sm_hub_2 | squad_4 | 3 | 0 |
| sm_pier_1 | beachhouse | 4 | 0 |
| sm_pier_1 | Cops | 0 | 3 |
| sm_warehouse_1 | back | 1 | 0 |
| sm_warehouse_1 | coward | 1 | 0 |
| sm_warehouse_1 | failed_sneak_squad | 5 | 0 |
| sm_warehouse_1 | freighthouse | 1 | 0 |
| sm_warehouse_1 | garage guy | 1 | 0 |
| sm_warehouse_1 | Office_Thugs | 5 | 0 |
| sm_warehouse_1 | rail1 | 2 | 0 |
| sm_warehouse_1 | rail1a | 1 | 0 |
| sm_warehouse_1 | rail2 | 4 | 0 |
| sm_warehouse_1 | sabbat | 2 | 0 |
| sm_warehouse_1 | station bathroom | 1 | 0 |
| sm_warehouse_1 | station_front | 1 | 0 |
| sm_warehouse_1 | truckyard | 2 | 0 |
| sm_warehouse_1 | ware1 | 6 | 0 |
| sm_warehouse_1 | ware1a | 4 | 1 |
| sm_warehouse_1 | ware1b | 8 | 1 |
| sm_warehouse_1 | ware2 | 1 | 0 |
| sm_warehouse_1 | wasware2 | 1 | 0 |
| sp_giovanni_1 | squad_guard | 7 | 0 |
| sp_giovanni_1 | squad_guard_east | 6 | 1 |
| sp_giovanni_1 | squad_guard_front | 5 | 0 |
| sp_giovanni_1 | squad_guard_north | 5 | 3 |
| sp_giovanni_1 | squad_guard_west | 2 | 1 |
| sp_giovanni_2a | East_Door_Guard | 1 | 0 |
| sp_giovanni_2a | Front_Guard | 5 | 1 |
| sp_giovanni_2a | Hall_Guards | 2 | 0 |
| sp_giovanni_2a | MainHall_Guards | 10 | 5 |
| sp_giovanni_2a | meeting | 6 | 0 |
| sp_giovanni_2a | Pool_Guards | 1 | 0 |
| sp_giovanni_2a | Study_Guard | 2 | 0 |
| sp_giovanni_2a | West_Door_Guard | 1 | 0 |
| sp_giovanni_2a | West_Hall_Guard | 4 | 1 |
| sp_giovanni_2b | Hall_Guards | 4 | 0 |
| sp_giovanni_2b | Kitchen_Guards | 5 | 1 |
| sp_giovanni_2b | MainHall_Guards | 12 | 5 |
| sp_giovanni_2b | meeting | 6 | 0 |
| sp_giovanni_2b | Pool_Guards | 5 | 1 |
| sp_giovanni_2b | Study_Guard | 1 | 0 |
| sp_giovanni_3 | squad_zombie | 2 | 0 |
| sp_giovanni_3 | Zombies | 5 | 5 |
| sp_giovanni_3 | Zombies2 | 5 | 4 |
| sp_giovanni_4 | squad_zombie | 31 | 0 |
| sp_giovanni_5 | ChangBrothers | 4 | 0 |
| sp_soc_1 | squad_guard | 9 | 0 |
| sp_soc_1 | squad_guard2 | 6 | 0 |
| sp_soc_1 | squad_guard3 | 8 | 0 |
| sp_soc_2 | soc_int_squad_guard | 12 | 0 |
| sp_soc_2 | soc_int_squad_monk | 3 | 0 |
| sp_soc_3 | squad_guard | 2 | 0 |
| sp_soc_3 | squad_guard_2 | 7 | 0 |
| sp_soc_3 | squad_guard_patrol | 2 | 0 |
| sp_tutorial_1 | soc_int_squad_guard | 6 | 0 |
| sp_tutorial_1 | soc_int_squad_monk | 1 | 0 |
| sp_tutorial_1 | squad_warehouse | 2 | 0 |

## Classnames outside the census families (2026-09-19, 0018 story 2 review)

The census above counts a fixed classname list. A scan of every `ai_*`, `logic_*`, `npc_*` and
`info_node*` classname over the same 108 entity units, joined with the datamap replay and the
32 exported scripts' `CreateEntity*` calls, adds:

**Authored, with a retail class, registered by nothing in the port** (0018 story 18):

| classname | retail class | rows | maps | keyfields | inputs / outputs |
|---|---|---|---|---|---|
| `logic_npc_condition` | `CLogicNPCCondition 0x1057748c` | 3 | `hw_tawni_1` (2), `la_ventruetower_1b` | `condition`, `target_npc` | `Test` / `OnTrue`, `OnFalse` |
| `logic_squad_condition` | `CLogicSquadCondition 0x105775b0` | 1 | `la_ventruetower_1b` | `condition`, `squad_name` | `Test` / `OnTrue`, `OnFalse` |
| `ai_changetarget` | `CAI_ChangeTarget 0x1059e044` | 2 | `la_bradbury_1` | `m_iszNewTarget` | `Activate` |
| `info_node_link` | `CAI_DynamicLink 0x10608f58` | 6 | `sp_giovanni_4` | `startnode`, `endnode`, `initialstate` | `TurnOn`, `TurnOff` |

Authored values: both `hw_tawni_1` rows are named `check_condition`, target
`npc_tawni_boyfriend`, and test `COND_SEE_PLAYER` and `COND_HEAR_PLAYER`; `floor_2_vis_check`
tests `COND_SEE_PLAYER` over squad `floor_2`; both `ai_changetarget` rows set `!player`. No
body of these four classes is walked yet.

**Graph-node classnames carrying no `hinttype`** (0018 story 4's places, not hints):
`info_node_werewolf` (80, `sp_observatory_2`), `info_node_tzimisce` (19, `ch_temple_4` 18 and
`la_bradbury_1` 1; `CNodeEnt::Spawn 0x102d78d0` rewrites it to `info_node`).

**In the binary, authored by no map and created by no script:** `ai_goal_standoff` (factory
`0x1000a19b`, `CAI_StandoffGoal`), `ai_changehintgroup` (`CAI_ChangeHintGroup 0x1059e0e4`),
`ai_sound` (factory `0x10001398`). The scripts create only `inspection_node`, `prop_*`,
`item_*` and `npc_VHuman`. `ai_network` is created by code (`CAI_NetworkManager`), never
authored. `ai_relationship` does not exist in `vampire.dll`: its `ai_*` classname table is
`ai_changehintgroup`, `ai_changetarget`, `ai_goal_standoff`, `ai_hint`, `ai_network`,
`ai_sound`.

**NPC classnames.** The maps author 41 `npc_*` classnames (makers aside); the port registers 15. The
unregistered ones with more than a handful of rows: `npc_vcamera` 87 (a deliberate inert
record), `npc_vghoulcroucher` 58, `npc_vzombie` 48, `npc_vsabbatgunman` 21, `npc_vtzimisce` 10,
`npc_vtzimisceheadclaw` 10, `npc_vlasombra` 6, `npc_vbrujah` 5. None stands on
`sp_tutorial_1`, `sm_hub_1` or `sp_soc_3`.

## Patrol tokens against the retail lookup (2026-09-19, 0018 story 2)

`0x102d2840` resolves a `FollowPatrolPath` token to the first hint of type 10000 or 800, in
hint-list order (reverse BSP order), whose `Group` matches byte for byte; it has no targetname
path. Scanned over the 108 entity units and the 32 exported scripts:

- **Map wires:** 767 `FollowPatrolPath` tokens; 757 match a `Group` exactly. The other 10, all on
  `sm_warehouse_1` (rows 1108 and 1452: `b5 b6 b7 m2 m3 m5 z1 z2`), match nothing under either
  rule — no `Group`, no targetname, in any case.
- **Scripts:** 102 tokens; all match a `Group` exactly.
- **Duplicate exact-case Groups on patrol-type hints:** `sp_soc_3` `d1..d4` (rows 355–358 and
  362–365), `sm_beachhouse_1` `p3` (rows 670, 887); the later row wins. `hw_ash_sewer_1` (4 rows)
  and `sm_warehouse_1` (3) author type-10000 hints with an empty `Group` (retail's own
  `FUN_102d7d30` warning).

So the retail lookup changes no shipped patrol; only hand-built fixtures addressed points by
targetname.

## The query surface of the helper classes (2026-09-16, 0018 story 1)

_Computed by `uv run elysium research ai_infra_surface` from the recovered address-backed helper interface, joined with `graph.tsv`, `functions.md` and `index.md`._

### hint
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| stand position | 0x102d1180 | FUN_102d1180 | CAI_BaseNPC::StartTask / FUN_102961a0 / FUN_102968f0 | returns the point where the hint says this NPC should stand | The three hint validators — `0x10295ed0`, `0x102961a0`, `0x10296c40` (2026-09-13); Slot 533 `EyeOffset` — `0x10274db0`, `0x102b4ab0`; The cover-lean hint pair `0x102b6120` and `0x102b7110` |
| release | 0x102d1420 | FUN_102d1420 | CAI_BaseNPC::UpdateOnRemove / CAI_BaseNPC::StartTask / CAI_BaseNPCTroika::ClearHintNode | clears the owner and delays the hint's next use by the supplied seconds | `TaskFail` and stopped special navigation, walked (2026-09-08); CAI_StandoffBehavior's unnamed bodies `0x102c7410`, `0x102c7530`, `0x102c7960`, `0x102c79a0`; The hint node's own words — `CAI_Hint`'s datamap, and `OnRestore` `0x102d3ec0` |
| is unusable | 0x102d14c0 | FUN_102d14c0 | CAI_StandoffBehavior::vfunc5 / FUN_102d1760 / FUN_102d1af0 | true while disabled, reuse-delayed, or owned by a live entity | `CNPC_VWerewolf::IsValidTeleportHint` `0x103d8300`; `IsValidMoveHint` `0x103d8060`; `IsValidRandomMoveHint` `0x103d7dc0`; CAI_StandoffBehavior's unnamed bodies `0x102c7410`, `0x102c7530`, `0x102c7960`, `0x102c79a0`, +2 more |
| find near | 0x102d1af0 | FUN_102d1af0 | CAI_BaseNPC::StartTask / CAI_BaseNPCTroika::StartTask / CNPC_Crow::StartTask | returns the nearest admissible hint matching type, flags, group and radius | The navigation and reaction keyfields (2026-09-08); `CNPC_VVampireBoss::SelectHintNode` `0x103c59d0` — read as `present`; The two hint searches and the cover forwards — `0x10365780`, `0x103bfa50`, `0x10297430`, `0x102974f0`; `CNPC_VManBat`'s flight velocity — `0x1038b370`, `0x1038bec0`, and the four flap timers |
| find near target | 0x102d24b0 | FUN_102d24b0 | FUN_103bfa50 | returns the nearest admissible hint of the requested group around a target point | The hint node's own words — `CAI_Hint`'s datamap, and `OnRestore` `0x102d3ec0`; The two hint searches and the cover forwards — `0x10365780`, `0x103bfa50`, `0x10297430`, `0x102974f0` |
| find tactical | 0x102d2980 | FUN_102d2980 | FUN_102b6b50 / FUN_102b7110 | returns the tactical hint matching the requested search flags and range | The cover and kick chooser, and the combat leftovers (2026-09-08); The cover-lean hint pair `0x102b6120` and `0x102b7110`; The shoot-at hint search `0x102b6b50` |

### place
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| eligible | 0x102dad60 | FUN_102dad60 | BuildCandidates | answers enabled, group, capacity, type and visitor eligibility for one NPC | The navigation and reaction keyfields (2026-09-08); Interesting places: the selector, the programs, the wait (2026-09-08); Interesting-place eligibility |
| claim | 0x102da7c0 | ClaimMarker | FUN_102a9f40 | adds the NPC to the place's visitor set and records the active marker | Interesting places: the selector, the programs, the wait (2026-09-08); The interesting-place wait and its loop — `0x102a9f40`, `0x102aa210`, `0x1029f780` |
| release | 0x102da600 | FUN_102da600 | CAI_BaseNPCTroika::RunTask / FUN_102b53d0 | removes the NPC's claim and visitor record and performs the leave bookkeeping | `CAI_BaseNPCTroika::UpdateOnRemove` — `0x1028d6e0`; Patrol paths, walked (2026-09-12, story 10g) |
| disable visitor walk | 0x102daac0 | FUN_102daac0 | FUN_102d9b10 | walks current visitors: DISAPPEAR visitors are removed and all others TaskFail(0x23) | The think cadence, decoded (2026-09-08) |

### patrol
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| clear path | 0x10307aa0 | FUN_10307aa0 | FUN_1029f460 | clears the patrol object's node list and resets its path state |  |
| set type | 0x10307b40 | FUN_10307b40 | FUN_1029f460 | stores the authored patrol traversal type |  |
| append node | 0x10307bf0 | FUN_10307bf0 | FUN_1029f460 | appends one authored node id to the patrol path |  |
| reset point | 0x10307b60 | FUN_10307b60 | FUN_1029f460 | sets the current index to the first point for the active traversal type | Patrol paths, walked (2026-09-12, story 10g) |
| next point | 0x10307b80 | NextPoint | FUN_102aa9e0 | advances by type; returns true when repeats are exhausted, otherwise wraps or reverses | Patrol paths, walked (2026-09-12, story 10g) |
| first index | 0x10307c20 | FUN_10307c20 | FUN_10307b60 / NextPoint | returns min(node-count minus one, the traversal type's first-index cap) | Patrol paths, walked (2026-09-12, story 10g) |
| allocate | 0x10307d30 | FUN_10307d30 | FUN_1029f460 | returns a pooled patrol-path object, growing the pool when it is dry | Patrol paths, walked (2026-09-12, story 10g) |
| free | 0x10307db0 | FUN_10307db0 | FUN_1029f5d0 | returns a patrol-path object to the pool | Patrol paths, walked (2026-09-12, story 10g) |
| interest record | 0x1029f730 | FUN_1029f730 | FUN_1029f780 / CAI_BaseNPCTroika::StartTask | returns the current node's cached interesting-place record only after its chance roll wins | Patrol paths, walked (2026-09-12, story 10g); The scripted custom move and the patrol interest draw — `0x10289fe0`, `0x1029f650`, `0x1029f730` (2026-09-13); The interesting-place wait and its loop — `0x102a9f40`, `0x102aa210`, `0x1029f780` |

### squad
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| find or create | 0x10315800 | FindCreateSquad | CAI_BaseNPC::InitSquad / CAI_BaseNPCTroika::SetSquad / CNPC_VCamera::InitSquad | finds the named squad or creates it, then admits the NPC with retail's 16-member cap | Squads, decoded (2026-09-08); `0x10273d30` / `0x10369bd0` — `InitSquad`, the Troika line and the camera |
| remove member | 0x103158f0 | RemoveFromSquad | CAI_BaseNPC::Event_Killed / CAI_BaseNPC::UpdateOnRemove / CAI_BaseNPCTroika::SetSquad | removes the NPC and compacts the member array, including retail's overwrite defect | The comfort sweep `0x102b1a20`, walked; Squads, decoded (2026-09-08); `0x10273d30` / `0x10369bd0` — `InitSquad`, the Troika line and the camera |
| member count | 0x103160a0 | FUN_103160a0 | CNPC_VChangBros::CheckForJumpAttack / CNPC_VChangBros::PositionClearForTeleport / CNPC_VChangBros::GetFacingTimeToTeleport | returns the squad's current member count | The four player-relative facing bodies — `0x103aaf50`, `0x1036d600`, `0x1036dc60`, `0x1035e5f0`; `0x1036e2f0` — `CNPC_VChangBros::GetOtherBrother` |
| member | 0x103160c0 | FUN_103160c0 | CNPC_VChangBros::CheckForJumpAttack / CNPC_VChangBros::PositionClearForTeleport / CNPC_VChangBros::GetOtherBrother | returns the member at an index, or null for every index while member zero is disconnected | Squads, decoded (2026-09-08); `0x1036e2f0` — `CNPC_VChangBros::GetOtherBrother` |
| new enemy | 0x103161a0 | SquadNewEnemy | CAI_BaseNPC::FUN_1026f590 / CAI_BaseNPCTroika::StartTask / CAI_BaseNPCTroika::PreSelectSchedule | publishes a newly acquired enemy to squad members and their shared enemy memory | The navigation and reaction keyfields (2026-09-08); The comfort sweep `0x102b1a20`, walked; `GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`; Squads, decoded (2026-09-08) |
| set focus | 0x10316660 | SetSquadFocus | FUN_1027de00 | stores the squad focus entity and focus position | The comfort sweep `0x102b1a20`, walked; The door-blocked notice — `0x1027de00` (2026-09-13); Squads, decoded (2026-09-08) |
| get focus | 0x103166b0 | GetSquadFocus | FUN_1027de00 / CAI_BaseNPCTroika::FUN_102984a0 / SelectDoorObstructionSchedule | returns the squad focus entity and position | `CAI_BaseNPCTroika::OnObstructingDoor` `0x102984a0`; The door-blocked notice — `0x1027de00` (2026-09-13); Squads, decoded (2026-09-08) |
| shared enemies | 0x10273e10 | CAI_BaseNPC::FUN_10273e10 | CAI_BaseNPC::OnTakeDamage_Alive / CAI_BaseNPC::GatherConditions / CAI_BaseNPC::SelectIdealState | returns squad+8 while connected and the disconnected global enemy store otherwise | The enemy accessors and the `CAI_Enemies` store — `0x101a67e0`, `0x102b5360`, `0x10027020`, `0x10273e10`, `0x10273e40` (2026-09-13); The enemy memory — `CAI_Memory` (2026-09-08); Squads, decoded (2026-09-08) |
| disconnect | 0x1026d050 | DisconnectFromSquad | FUN_1026d130 / CAI_BaseNPCTroika::StartTask / DoPossession | increments the disconnect refcount and leaves shared memory on the zero-to-one edge | Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload (2026-09-08); The think cadence, decoded (2026-09-08); `TASK_MAKE_OBLIVIOUS` and `m_iIsOblivious`; Squads, decoded (2026-09-08) |
| reconnect | 0x1026d0c0 | FUN_1026d0c0 | FUN_10009601 (jump thunk) | decrements with a zero floor and rejoins shared memory when the count reaches zero |  |
| leave | 0x10316700 | LeaveSquad | DisconnectFromSquad / CAI_BaseNPCTroika::SetSquad | does nothing: retail's LeaveSquad body is an empty RET 4 stub | Ambient groups and interesting places; Squads, decoded (2026-09-08); `0x1029a930` — `CAI_BaseNPCTroika::SetSquad` |

### coordinator
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| has room | 0x1025db50 | FUN_1025db50 | FUN_102b5900 / FUN_10385d70 / FUN_103c1b10 | true while the registered count is below the cap (2); corrected 2026-09-19 — earlier read as "is full" | The `CNPC_VAndreiBlood` line's melee-slot bodies — `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70`; The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900` |
| admit | 0x1025db70 | FUN_1025db70 | FUN_1025dca0 / CAI_BaseNPCTroika::FUN_102b5650 / CNPC_VAndreiBlood::FUN_10385ab0 | already registered answers true; full forwards to the evicting add with the candidate's enemy distance; else appends | The melee quartet's species replacements — slots 599, 600, 601, 602; The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900` |
| request attacker slot | 0x1025dca0 | FUN_1025dca0 | FUN_1025db70 / FUN_102b57c0 / FUN_10385c30 | adds with room; when full evicts the member with the strictly greatest enemy distance above the threshold, or refuses | The melee quartet's species replacements — slots 599, 600, 601, 602; The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900` |
| release | 0x1025ddd0 | FUN_1025ddd0 | FUN_1025dca0 / FUN_102b5880 / FUN_10385cf0 | removes the NPC by moving the last entry into its slot | The `CNPC_VAndreiBlood` line's melee-slot bodies — `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70`; The melee quartet's species replacements — slots 599, 600, 601, 602; The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900` |
| is unregistered | 0x1025de90 | FUN_1025de90 | FUN_102b5900 / FUN_10385d70 / FUN_103c1b10 | returns true when a linear scan finds no coordinator entry for this NPC | The `CNPC_VAndreiBlood` line's melee-slot bodies — `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70`; The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900` |

### standoff
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| activate | 0x102c87a0 | CAI_StandoffGoal::vfunc241 | CAI_GoalEntity::InputActivate (slot 241) | clamps aggressiveness, resolves actors, marks active and enables the goal on each actor | `CAI_StandoffGoal`'s two inputs `0x102c87a0` / `0x102c8830` and `UpdateOnRemove` `0x102cdc50` |
| deactivate | 0x102c8830 | CAI_StandoffGoal::vfunc243 | CAI_StandoffGoal::UpdateOnRemove | clamps aggressiveness, disables the goal on each actor, then removes the listener | `CAI_StandoffGoal`'s two inputs `0x102c87a0` / `0x102c8830` and `UpdateOnRemove` `0x102cdc50` |
| remove | 0x102cdc50 | CAI_StandoffGoal::UpdateOnRemove | CBaseEntity removal dispatch (slot 180) | deactivates an active goal through slot 243 before base removal | `CAI_StandoffGoal`'s two inputs `0x102c87a0` / `0x102c8830` and `UpdateOnRemove` `0x102cdc50` |
| translate activity | 0x102c79e0 | CAI_StandoffBehavior::vfunc22 | CAI_StandoffBehavior::TranslateActivity (slot 22) | translates low-aim and cover activities from goal state, hint type and owned weapon | `CAI_StandoffBehavior::TranslateActivity` `0x102c79e0` |

### sound
| query | retail address | ledger name | caller (<=3) | what it answers | oracle section |
| --- | --- | --- | --- | --- | --- |
| insert | 0x101bac90 | CSoundEnt::InsertSound | FUN_101dfc20 / FUN_101e3560 / CAI_BaseNPC::OnTakeDamage_Alive | adds a typed, owned sound with origin, integer radius, insertion time and expiry | 1.6 The emit; Hearing, walked (2026-09-08) |
| sound by index | 0x101bb150 | CSoundEnt::SoundPointerForIndex | CAI_Senses::Listen / FUN_10310440 / FUN_10310480 | returns the active shared-list sound at an index, rejecting invalid indices |  |
| listen | 0x1030f940 | CAI_Senses::Listen | CAI_Senses::PerformSensing | links every newly inserted sound matching interests and CanHearSound, then stamps listen time | `CAI_Senses::PerformSensing` — `0x10310710` (2026-09-13); Hearing, walked (2026-09-08); The sense pass for a hated player, walked (2026-09-08) |
| can hear | 0x1030f7b0 | CanHearSound | CAI_Senses::Listen | answers freshness, owner, range, hearing scalar, stealth, occlusion and QueryHearSound gates | 2.8 Port notes (wave 2, B2); Hearing, walked (2026-09-08); `IsValidMoveHint` `0x103d8060`; `QueryHearSound` `0x102b35b0` |

### Counts
| object | queries | with a name | with >=1 caller | with an answer |
| --- | --- | --- | --- | --- |
| hint | 6 | 0 | 6 | 6 |
| place | 4 | 1 | 4 | 4 |
| patrol | 9 | 1 | 9 | 9 |
| squad | 11 | 8 | 11 | 11 |
| coordinator | 5 | 0 | 5 | 5 |
| standoff | 4 | 4 | 4 | 4 |
| sound | 4 | 4 | 4 | 4 |
