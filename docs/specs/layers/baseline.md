# Baseline before L0 — 2026-10-06

Branch `layers/l0` at `5074f6b4`: the code of `main` `8779bd51` (0002's V6 commit `bafe1e9e` included),
plus the VisualStudioTools plugin disabled for the project. Run with `uv run elysium build`, `test`,
`test arm`, `arena` (`$ELYSIUM_WORK_ROOT/build-order/baseline/`).

| run | result |
|---|---|
| build | ok, 57 s |
| default tier | 169 passed, 0 failed (Session 1, Substrate 168), 19 s |
| arm tier | 1,654 passed, 0 failed, 2 m 28 s |
| arena | 167 records: 150 pass, 14 expected-fail, 2 unexpected-pass, 1 fail; 24 min (report `20261006T044255.682267Z`) |

Not pass or expected-fail:

- `rollcall_vzombie` fail — H11 (no interesting places), the standing known fail.
- `hear_world_investigate`, `interest_mode_never` unexpected-pass — N4, the intermittent hearing race (V10).

This is also 0002's open V6 gate: every record that passed at V5b passes, and no V6 record fails.

## Stubs the current scenarios hit (runtime evidence for the lower layers)

The arena run fired lower-layer stubs these many times — live paths of the NPC behaviour already
built, so their stories go first in their layers:

| stub | address | hits |
|---|---|---|
| `CBaseAnimating::SetPoseParameter02` | `0x10091fe0` | 90,430 |
| `CBaseCombatCharacter::HeadDirection2D` | `0x10331cb0` | 3,823 |
| `CBaseCombatCharacter::HasUsableRangedWeapon` | `0x10336d70` | 1,207 |
| `CBaseEntity::IsViewable` | `0x100a9800` | 683 |
| `CBaseEntity::GetEnemy` | `0x10027020` | 376 |
| `CBaseCombatCharacter::MemberSync` | `0x10337ca0` | 67 |
| `CBaseCombatCharacter::CreateDamageEffects` | `0x10330d00` | 61 |
| `CBaseCombatCharacter::BeginVampHeal_HOT` | `0x10324ba0` | 61 |
| `CBaseCombatCharacter::Slot326` | `0x103482e0` | 58 |
| `CBaseCombatCharacter::Event_Dying` | `0x1032bdf0` | 19 |
| `CAI_BaseNPCTroika::Slot603` | `0x102b59e0` | 17 |
| `CBaseCombatCharacter::LookAtEntity` | `0x1033e370` | 5 |
| `CBaseCombatCharacter::Event_TookLife` | `0x1032c580` | 4 |
| `CBaseEntity::Hide` | `0x1009d2a0` | 4 |
| `CBaseCombatCharacter::Weapon_Switch` | `0x1032dde0` | 3 |
| `CBaseEntity::OnVictimHitByMe` | `0x10026590` | 2 |
| `CBaseCombatCharacter::FrenzyCheck` | `0x1033eb60` | 1 |
| `CBaseCombatCharacter::GetBestRangedWeapon` | `0x10337080` | 1 |
| `CBaseEntity::Unhide` | `0x1009d380` | 1 |

## Every record

| record | result |
|---|---|
| `anim_footsteps_walk` | pass |
| `anim_player_footsteps` | pass |
| `anim_player_weapon_event_firearm` | pass |
| `anim_player_weapon_event_melee` | pass |
| `anim_prop_event` | pass |
| `bound_trips` | pass |
| `chase_melee` | pass |
| `control_sequence` | pass |
| `corpse_fades` | pass |
| `corpse_kept_seen` | pass |
| `corpse_kindred_burns` | pass |
| `corpse_pedestrian_stays` | pass |
| `corpse_removed_unseen` | pass |
| `cover` | pass |
| `cover_armed` | pass |
| `cover_move_shoot` | pass |
| `cover_reclaim` | pass |
| `damage_cower_one_hit` | pass |
| `damage_high_health_control` | pass |
| `damage_idle_reaction` | pass |
| `damage_knockout_one_hit` | pass |
| `damage_last_record` | pass |
| `damage_lethal_death` | pass |
| `damage_record_branches` | pass |
| `dialog_choose_none` | pass |
| `dialog_use_hold` | pass |
| `dialogue_perception` | pass |
| `face_enemy_turn` | pass |
| `fail_route_unreachable_sound` | pass |
| `hear_world_investigate` | unexpected-pass |
| `hear_world_out_of_range` | pass |
| `hub_crosswalk_wait` | pass |
| `idle_lookaround` | pass |
| `input_changeschedule_reselect` | expected-fail |
| `input_clearpatrolpath` | pass |
| `input_disablethink` | expected-fail |
| `input_setrelationship` | pass |
| `input_startplayerdialogremote` | pass |
| `input_takedamage` | pass |
| `input_teleporttoentity` | pass |
| `input_tweakparam_vision` | pass |
| `input_useinteresting` | pass |
| `interest_mode_never` | unexpected-pass |
| `lifecycle_relationship_flip` | pass |
| `lifecycle_startnpc_ground_drop` | pass |
| `lifecycle_unhide_fall_to_ground` | pass |
| `lifecycle_unhide_fights` | expected-fail |
| `maker_refusal_measurement` | pass |
| `maker_respawn` | pass |
| `map_hub_idle` | pass |
| `map_tutorial_idle` | pass |
| `map_tutorial_sneak_past` | expected-fail |
| `map_tutorial_unhide_thug3` | pass |
| `melee_ally_in_the_way` | pass |
| `melee_enemy_blocked` | pass |
| `melee_same_team` | pass |
| `melee_swing` | pass |
| `memory_occluded_kept` | pass |
| `must_fail` | pass |
| `never_after_ignores` | pass |
| `never_at_most_holds` | pass |
| `never_at_most_trips` | pass |
| `never_within_holds` | pass |
| `never_within_trips` | pass |
| `patrol_monk_loop` | pass |
| `patrol_sentry2_pingpong` | pass |
| `persistence_missing_load` | pass |
| `persistence_refused_save` | pass |
| `persistence_world_rebind` | expected-fail |
| `place_marker_reservation` | pass |
| `places_pedestrian_visit` | pass |
| `places_thug_pt1` | pass |
| `player_reset_a` | pass |
| `player_reset_b` | pass |
| `range_bands` | pass |
| `ranged_enemy_dead` | pass |
| `ranged_enemy_dead_retarget` | pass |
| `ranged_fake_reload` | pass |
| `ranged_flamethrower_silent` | pass |
| `ranged_friend_in_line_of_fire` | pass |
| `ranged_open_fire` | pass |
| `ranged_real_reload` | pass |
| `ranged_step_back_holds` | pass |
| `ranged_sustained_fire` | pass |
| `restore_place_invalid_marker` | pass |
| `rollcall_payphone` | pass |
| `rollcall_vandreiblood` | pass |
| `rollcall_vanimal` | expected-fail |
| `rollcall_vasianvampire` | pass |
| `rollcall_vbach` | pass |
| `rollcall_vbrujah` | pass |
| `rollcall_vcamera` | pass |
| `rollcall_vcamerasecurity` | pass |
| `rollcall_vchangbros` | pass |
| `rollcall_vchangbrosblade` | pass |
| `rollcall_vchangbrosclaw` | pass |
| `rollcall_vcop` | pass |
| `rollcall_vdialogpedestrian` | pass |
| `rollcall_vdog` | expected-fail |
| `rollcall_vfrenzyshadow` | pass |
| `rollcall_vgargoyle` | pass |
| `rollcall_vghoulcroucher` | pass |
| `rollcall_vguard1` | pass |
| `rollcall_vhengeyokai` | pass |
| `rollcall_vhuman` | pass |
| `rollcall_vhumancombatant` | pass |
| `rollcall_vhumancombatpatrol` | pass |
| `rollcall_vhunter` | pass |
| `rollcall_vlasombra` | pass |
| `rollcall_vmanbat` | expected-fail |
| `rollcall_vmercurio` | pass |
| `rollcall_vmingxiao` | pass |
| `rollcall_vmingxiaotentacle` | pass |
| `rollcall_vnewscaster` | pass |
| `rollcall_vpedestrian` | pass |
| `rollcall_vplaceholder` | pass |
| `rollcall_vplayercontroller` | pass |
| `rollcall_vpronedialog` | pass |
| `rollcall_vrat` | pass |
| `rollcall_vsabbatgunman` | pass |
| `rollcall_vsabbatleader` | pass |
| `rollcall_vscurrying` | expected-fail |
| `rollcall_vsheriffman` | pass |
| `rollcall_vtaxidriver` | pass |
| `rollcall_vtzimisce` | pass |
| `rollcall_vtzimisceheadclaw` | pass |
| `rollcall_vtzimiscerunner` | pass |
| `rollcall_vvampire` | pass |
| `rollcall_vvampireboss` | pass |
| `rollcall_vwerewolf` | pass |
| `rollcall_vwolfmorph` | pass |
| `rollcall_vyukie` | pass |
| `rollcall_vzombie` | fail |
| `save_cine_possession_resume` | expected-fail |
| `save_restore_corpse_burn` | pass |
| `save_restore_corpse_fade` | pass |
| `save_restore_corpse_pedestrian` | pass |
| `save_restore_corpse_seen` | pass |
| `save_restore_corpse_static` | pass |
| `save_restore_corpse_unseen` | pass |
| `save_restore_hidden_unhide` | pass |
| `save_restore_interesting_place_visit` | pass |
| `save_restore_invalid_schedule` | pass |
| `save_restore_melee_coordinator` | pass |
| `save_restore_mid_path` | expected-fail |
| `save_restore_move_shoot` | expected-fail |
| `save_restore_place_activity` | expected-fail |
| `save_restore_single_round_reload` | pass |
| `script_aischedule_walk` | pass |
| `script_dialog_hold` | pass |
| `script_walk_to_mark` | pass |
| `sense_beyond_vision` | pass |
| `sense_bodies_transparent` | pass |
| `sense_cone_enter` | pass |
| `sense_cone_outside` | pass |
| `sense_enemy_facing_me` | pass |
| `session_load_saved_clock` | pass |
| `session_map_clock_revisit` | pass |
| `session_map_load_fresh_world` | pass |
| `session_time_rebase` | pass |
| `stage_failed_b` | pass |
| `team_damage_gate` | pass |
| `unknown_crouched_band` | pass |
| `verbs_feed_trance` | pass |
| `verbs_feed_victim_dispatch` | pass |
| `verbs_stealth_kill` | pass |
| `verbs_stealth_kill_scripted` | expected-fail |
