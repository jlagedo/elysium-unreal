# V1 inventory, merged (V2, 2026-10-04)

From `inventory-P.md`, `inventory-K.md`, `inventory-W.md`; results are the consolidated verdicts of
`triage.md` (full run `20261004T022053.584695Z` plus the re-runs named in `review.md`). The lane
files keep the per-story detail; this file is the one table and the stories left without a record.

## Landed stories and the records that prove them

| story | claim | records | result |
|---|---|---|---|
| 0019/8 = 0002/29e the loop | `NPCThink → RunAI → GatherConditions → MaintainSchedule → 437/438 → StartTask/RunTask`; damage 142/390, death 144 | `cover_reclaim`, `lifecycle_relationship_flip`, `damage_idle_reaction` pass; `range_bands` red 2; `ranged_open_fire` red 3 + N2; `damage_lethal_death` red 3 | the loop runs combat end to end |
| 0019/3 the schedule corpus | 691 texts from the deployed corpus | every record (task order matches the `.sch` texts in every trace read) | pass; the `CHASE_ENEMY_FAILED` witness has no record (H7) |
| 0019/5 the class tree | every live classname → its retail class | `rollcall_*` ×48 | 34 pass, 11 expected-fail (red 5 ×6, red 6 ×2, N10 ×3), `vzombie` fail (H11), cameras error (H1); the roll call's expectation is loose (`review.md`) |
| 0019/4 tunables | apex 40, melee 100, health 10, hint height 64 | `sense_beyond_vision` (vision 440), `sense_cone_outside` | pass (only the vision row discriminates) |
| 0018/2 baked infrastructure | hint, place, maker, NPC rows baked and adopted | every `from_map` record | adoption "by index" is `Elysium.Content.` |
| 0018/3 contents, NavMesh, agents | per-hull meshes | human hull: places, patrols, `script_walk_to_mark`, `cover*` | non-human hulls: no record (no wire gives one a goal in the arena) |
| 0018/4 the place set | places, node binding, cooldown | `patrol_*`, `places_*` | patrols red 1 (provisional), places red 6 |
| 0018/5 the navigator | outcomes, arrival | `cover`, `cover_reclaim` (goal → arrived twice), `chase_melee` | arrival pass; `0x0c`/`0x0d`, stale mark, hold need H7 |
| 0018/6 geometry | stand test, cover validity; sight's four arguments | `cover_reclaim`; `sense_bodies_transparent`, `sense_cone_enter` | movement half pass; sight half blocked by H2 |
| 0018/7 traversals | doors, crosswalk wait, jump links refused | `hub_crosswalk_wait` | red 6; doors no record (H7; no hub NPC routed over a door while red 6 stands) |
| 0018/8 hint nodes | searches, claim/release, cooldown | `cover`, `cover_reclaim`, `cover_armed` | claim → release → re-claim pass; `+5.0` release needs H7 |
| 0002/3a–c the stealth kill | rules, victim selection, commit, synchronized death | `verbs_stealth_kill` | fail, unclassified (H5) |
| 0002/20 the trance | `FeedInterrupt → SCHED_TROIKA_MESMERIZED` | `verbs_feed_trance` | pass (`DELAY_INTERRUPTS` not asserted) |
| 0002/5 enemy memory | `CAI_Memory`, `ChooseEnemy` | `memory_occluded_kept` | fail, H2 |
| 0002/6a sight and hearing | prefilter, cone, band, hearing delay, `ambient_generic` | `sense_cone_*`, `sense_beyond_vision`, `hear_world_*` | cone, band, 440 edge pass; block half H2; hearing N4 (intermittent) |
| 0002/6b the Troika cone | slot 363 arms | `sense_cone_outside`, `sense_enemy_facing_me` | pass; the enemy's cone is red 3 |
| 0002/1, 2, 4 light, gauge, observer | sight inside the light scalar | `unknown_crouched_band` | pass on the crouch (corrected); the light half needs H5 |
| 0002/8 the flag word | SET/CLEAR tasks | `hear_world_investigate`, `idle_lookaround` | flag tasks complete; masks not observable |
| 0002/9 interest predicate | `ShouldInvestigate 0x102b3270` | `interest_mode_never`, `hear_world_investigate`, `fail_route_unreachable_sound` | pass (mode 0 refuses, mode 3 admits) |
| 0002/10a sound sweep | `0x102b1cd0` | same three | pass; N4 makes the hearing intermittent |
| 0002/10b see-unknown sweep | `0x102b15c0`, Roll B | `unknown_crouched_band`, (`sense_beyond_vision` via `ATTACK_UNKNOWN`) | pass |
| 0002/13, 14, 25 TaskFail, failure route, `WAIT_RANDOM` | | `fail_route_unreachable_sound`, `idle_lookaround` | pass; "no storm" needs H3 |
| 10e / 10k investigation arms | | `hear_world_investigate` | the whole program runs when heard |
| 26 the pre-selector | `0x102ae920` | every combat record (`NEW_ENEMY → 0xea`, `ATTACK_UNKNOWN → 0x5b`) | pass where staged |
| 0002/10g, 27 patrol | `TASK_NEXT_PATROL_POINT`, `NextPoint` | `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`, `map_tutorial_idle`, `map_hub_idle` (cops) | program, order, types retail's; the walk too slow (red 1 provisional) |
| 0002/11 places (landed parts) | the selector arms and task bodies | `places_*`, `input_useinteresting` | red 6 |
| registered NPC inputs | each bound input does its handler | `input_*` ×9, `script_dialog_hold`, `patrol_*` | pass: SetRelationship, TeleportToEntity, TweakParam, TakeDamage; new: DisableThink (N5), ChangeSchedule '-' (N6), StartPlayerDialog/Remote (N8); red 6 UseInteresting; red 1 ClearPatrolPath |
| scripted sequence, dialogue hold | | `script_walk_to_mark`, `script_dialog_hold` | N7, N8 (V3); the dialogue's release needs H12 |
| makers | child spawn, re-spawn after death | `maker_respawn`, `rollcall_vhunter`, `rollcall_vmingxiaotentacle` | spawn pass; re-spawn N9 |
| 0019/2, 0019/6 save and restore | the generated walk, `OnPostRestore` | `save_restore_mid_path.json.parked` | parked (H8) |
| the two maps (V8's smokes) | tutorial and hub at idle, the sneak-past | `map_tutorial_idle`, `map_hub_idle`, `map_tutorial_sneak_past` | tutorial idle pass; hub and sneak-past red 6 |

## Landed stories with no record, and why

| story | why no scenario observes it | what does |
|---|---|---|
| 0002/15 think cadence | no trace kind carries a think, a gather or a stamp | H10; today only indirect `cond+` timing |
| 0002/7 obliviousness (beyond the trance) | every issuer is a discipline, a knockout or the trance; no arena action casts a discipline | `verbs_feed_trance` covers the trance's `TASK_MAKE_OBLIVIOUS` |
| 0002/10c comfort sweep | the list's only writer is the discipline applier `0x101de660` | needs a discipline action |
| 10j `CheckTarget` | needs `m_hTargetEnt` (comfort, scripted, follower) | V3's scripted records once `0xf2` runs |
| 21b cower / disoriented / lost, 13b | fear/law levels (R5), discipline-forced programs | R5 |
| 0018/4 the wander (`0x1f`) | issued only by `RUN_TO_SAVED` and `PLAYER_ON_HEAD_RUN` among Troika humans; neither stageable | a "player on head" action; divergence 8 is kept |
| 25b species `TranslateSchedule` | its witness is `CHASE_ENEMY_FAILED` | H7 |
| 25c the null-schedule arm | nothing in the arena selects a null schedule | observable only through the FAIL route (`fail_route_unreachable_sound`) |
| 0018/7 doors, jumps | no arena door; jump links refused for every shipped NPC | H7; V8 on the hub |
| 0018/3 non-human hulls | no shipped wire gives one a goal in the arena | `verify nav`, content tests |
| `StartPlayerDialogUnforced`, `StartTransformation`, `SetScriptedDiscipline` | not staged (same arm as StartPlayerDialog; species body unread; a pending seam) | V7's table test |
| known reds 4, 7, 8, 9, 10, 11 | see `triage.md` § the known reds | red 4 needs a record (player behind the shooter at 96 cm); 8 needs H8 |

## The 22 divergences

Unchanged from `divergences.md` (V3 2, V4 1, V6 1, R2 6, R3 1, R6 4, kept 7). Two rows gain a record
from this pass: row 1 (the arbiter) — `cover`, and `review.md` doubt 1 on whether the patrols' slow
walk is it; row 18 (the director's beat) — `script_walk_to_mark` (N7).
