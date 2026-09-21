# The NPC ConVars, named

Recovered 2026-09-21 (RE-BACKLOG WP19: a Codex worker's sweep,
`$ELYSIUM_WORK_ROOT/codex/re2/wp19-convars`; the constructor shape and two rows re-read by the
lead). The NPC oracle had left these globals as "a cvar by shape; not named" or "console name and
default unrecovered". Every one has a static-initialiser constructor call of the shape

```
PUSH help / PUSH flags / PUSH default-string / PUSH name / MOV ECX, object / CALL ConVar ctor
```

so name, default and help are literals in the image. The oracle's `DAT_…` pointer is the object
`+ 4`; the value is read as a float at object `+0x28` or an int at `+0x2c` (the Readers column says
which). **No shipped config, script or vdata file sets any of them**, so the DEFAULT is what retail
runs — which is what 0019 story 4's tunables table needs. All flags are 0.

The ones a port story reads: `debug_viewcone_back_dist` **40** (the cone's pulled-back apex,
`senses.md`; `ViewConeBodyOffsetCm`); `debug_view_cone_2d3d` **3** (the 3-D cone);
`debug_melee_advance_combatmove_dist` **100** (what `DIST:COMBATMOVE` and the melee advance
resolve to); `debug_allow_move_facing` **1**; `debug_allow_dodge` **0** (dodging with no cover node
is OFF as shipped); `debug_allow_fake_reload` **1**; `debug_turning` **0** with
`debug_turning_speed` 90 and `debug_turn_scalar` .15 (turn animations are OFF as shipped);
`debug_force_anim` 0; `debug_alert_aggressive` 0 and `debug_hunting_aggressive` **1**;
`npc_hit_buildup_amount` 2; `sk_basenpctroika_health` 10; `manbat_delta` 600.0.

| Reader pointer / object | ConVar | Default | Min / max | Help | Ctor call | Readers (member) |
|---|---|---:|---|---|---|---|
| `0x1093b7cc` · object `0x1093b7c8` | `manbat_delta` | `"600.0"` | — | NULL | `0x1038ad30` | `0x1038b370 FUN_1038b370`: `+0x28` float |
| `0x10923d3c` · object `0x10923d38` | `debug_allow_fake_reload` | `"1"` | 0 / 1 | “Allows or disallows faked NPC reloading.” | `0x1028c5e0` | `0x102b8620 FUN_102b8620`: `+0x2c` int |
| `0x10924a1c` · object `0x10924a18` | `debug_melee_advance_combatmove_dist` | `"100"` | — | “How close a melee combatant needs to be to player in order to do the combat move. Outside of this distance they will run to close the distance.” | `0x1028bb90` | `0x102702d0`, `0x102b5650`, `0x102b5900`, `0x102b6c30`, `0x102b8620`, `0x10361be0`, `0x1036d800`, `0x10385ab0`, `0x10385d70`, `0x10385e40`, `0x10396050`, `0x103aa060`, `0x103c1b10`, `0x103c3ab0`, `0x103c4430`, `0x103dda10`: `+0x28` float |
| `0x109248f4` · object `0x109248f0` | `debug_allow_dodge` | `"0"` | 0 / 1 | “Allows or disallows NPC dodging when no cover nodes are found.” | `0x1028c540` | `0x102b7cf0 FUN_102b7cf0`: `+0x2c` int |
| `0x1093f95c` · object `0x1093f958` | `werewolf_draw_hints` | `"40"` | — | NULL | `0x103cb440` | `0x103cb4b0`, `0x103cb590`: `+0x2c` int |
| `0x1093f73c` · object `0x1093f738` | `werewolf_show_debug` | `"0"` | — | NULL | `0x103c82c0` | `0x103cb590`, `0x103d4a60`, `0x103d4d60`, `0x103d93b0`: `+0x2c` int |
| `0x10923f14` · object `0x10923f10` | `sk_basenpctroika_health` | `"10"` | — | NULL | `0x1028b4c0` | `0x1029a0b0 NPCInit`: `+0x28` float |
| `0x1093c34c` · object `0x1093c348` | `andrei_force_awaken` | `"0"` | — | NULL | `0x103a5950` | `0x103a8990 RunTask`: `+0x2c` int |
| `0x10937a8c` · object `0x10937a88` | `debug_viewcone_back_dist` | `"40"` | — | “Change the distance we bump back the view cone.” | `0x1031a4f0` | `0x103264d0`, `0x1029c4a0`, `0x103e9e00`: `+0x28` float |
| `0x1093f8ec` · object `0x1093f8e8` | `werewolf_pursuit_unseen_time` | `"3.0"` | — | NULL | `0x103c86b0` | `0x103cf5f0`: `+0x28` float |
| `0x1093d574` · object `0x1093d570` | `werewolf_pursuit_distance` | `"800"` | — | NULL | `0x103c8620` | `0x103cf5f0`, `0x103cfc50`, `0x103d2810`: `+0x28` float |
| `0x10936f74` · object `0x10936f70` | `debug_view_cone_2d3d` | `"3"` | 2 / 3 | “Set the view cone tests to be in 2d or 3d.” | `0x10326170` | `0x103268e0`: `+0x2c` int; dispatched by `0x10326750` |
| `0x1093cf94` · object `0x1093cf90` | `tzimisce_voice_pitch` | `"100"` | — | “Set this var to override the voice pitch (100 = normal, 120 = high).” | `0x103b6af0` | `0x103b92a0`, `0x103b9380`, `0x103b9440`, `0x103b9500`, `0x103b9980`, `0x103b9a50`, `0x103b9b20`, `0x103b9be0`, `0x103b9ca0`, `0x103b9d60`, `0x103b9e30`: `+0x2c` int |
| `0x1093cfdc` · object `0x1093cfd8` | `tzimisce_voice_attn` | `"65"` | — | “Set this var to override the voice attenuation (75 decibels = normal).” | `0x103b6a60` | Same 11 Tzimisce sentence emitters: `+0x2c` int |
| `0x1093cebc` · object `0x1093ceb8` | `tzimisce_voice_volume` | `"1"` | 0 / 1 | “Set this var to override the voice volume (1 = normal).” | `0x103b69c0` | Same 11 Tzimisce sentence emitters: `+0x28` float |
| `0x10924f74` · object `0x10924f70` | `debug_allow_move_facing` | `"1"` | — | “If this is on, NPCs will move facing the NPC when they run for cover.” | `0x1028c720` | `0x10278cb0`, `0x10278d20`, `0x10278d90`, `0x10292de0`, `0x102b93c0`, `0x103854f0`: `+0x2c` int |
| `0x109247ec` · object `0x109247e8` | `debug_turning` | `"0"` | — | “Toggles turn animations.” | `0x1028b710` | `0x10297640`, `0x10297ce0`, `0x10374130`: `+0x2c` int |
| `0x1093c104` · object `0x1093c100` | `sabbat_gunman_speed_threshold` | `"0.1"` | — | “Determines how fast the guy needs to be going to scale the speed.” | `0x103a4e60` | `0x103a56f0`: `+0x28` float |
| `0x1093c14c` · object `0x1093c148` | `sabbat_gunman_speed_trails` | `"3"` | — | “Determines what level of motion trails to use for a speedy guy.” | `0x103a4ef0` | `0x103a56f0`: `+0x2c` int |
| `0x1093c1f4` · object `0x1093c1f0` | `sabbat_gunman_speed_scalar` | `"3.0"` | — | “Determines how much faster than normal a sabbat gunman will move.” | `0x103a4dd0` | `0x103a5650`, `0x103a56f0`: `+0x28` float |
| `0x1093f85c` · object `0x1093f858` | `werewolf_fastbreak_chance` | `"40"` | — | NULL | `0x103c8350` | `0x103d6000`: `+0x2c` int |
| `0x10924c94` · object `0x10924c90` | `debug_turn_scalar` | `".15"` | — | “Set this var to override turn animation speed.” | `0x10297c70` | `0x10297ce0`: `+0x28` float |
| `0x1093ad24` · object `0x1093ad20` | `debug_dog_turn_scalar` | `".15"` | — | “Set this var to override turn animation speed.” | `0x103740c0` | `0x10374130`: `+0x28` float |
| `0x1093c9fc` · object `0x1093c9f8` | `tzimisce_turn_scalar` | `".15"` | — | “Set this var to override turn animation speed.” | `0x103b6540` | `0x103ba020`: `+0x28` float |
| `0x10923e84` · object `0x10923e80` | `debug_turning_speed` | `"90"` | — | “Toggles turn animations.” | `0x1028b7a0` | `0x10297ce0`, `0x10374130`: `+0x28` float |
| `0x1093d694` · object `0x1093d690` | `werewolf_disregard_player_vision` | `"0"` | — | NULL | `0x103c7f60` | `0x103da230`: `+0x2c` int |
| `0x1093d52c` · object `0x1093d528` | `werewolf_translated_enemy_position_tolerance` | `"0"` | — | NULL | `0x103d9d90` | `0x103d9e00`: `+0x28` float |
| `0x1093d414` · object `0x1093d410` | `werewolf_teleport_out_time` | `"4.0"` | — | NULL | `0x103c83e0` | `0x103cc0d0`: `+0x28` float |
| `0x1093cc3c` · object `0x1093cc38` | `tzimisce_claw_left_x` | `"0"` | — | “Set this var to override the claw reach around attack pos.” | `0x103b6660` | `0x103bfd80`: `+0x28` float |
| `0x1093cbf4` · object `0x1093cbf0` | `tzimisce_claw_left_y` | `"25"` | — | Same claw-reach help | `0x103b66f0` | `0x103bfd80`: `+0x28` float |
| `0x1093cbac` · object `0x1093cba8` | `tzimisce_claw_left_z` | `"40"` | — | Same claw-reach help | `0x103b6780` | `0x103bfd80`: `+0x28` float |
| `0x1093ba8c` · object `0x1093ba88` | `ming_xiao_pickup` | `"1"` | — | “Set this to 1 to allow ming xiao to pickup and throw bodies.” | `0x10390b50` | `0x10398b20`: `+0x2c` int |
| `0x1093bb14` · object `0x1093bb10` | `debug_tentacle_mask` | `"-1"` | — | NULL | `0x10398790` | `0x10398800`: `+0x2c` int |
| `0x109241fc` · object `0x109241f8` | `dialog_facial_debug` | `"0"` | — | “Turns on debug text for the NPC you are talking to.” | `0x1028c410` | `0x102c0360`, `0x102c0520`: `+0x2c` int |
| `0x1093ca8c` · object `0x1093ca88` | `tzimisce_throw_power` | `".007"` | — | “Set this var to override the throw power.” | `0x103b6420` | `0x103be8e0`, `0x103bea90`: `+0x28` float |
| `0x1093ca44` · object `0x1093ca40` | `tzimisce_throw_hds` | `".0008"` | — | “Set this var to override the throw height to distance scalar.” | `0x103b64b0` | `0x103bea90`: `+0x28` float |
| `0x10924d6c` · object `0x10924d68` | `debug_force_anim` | `"0"` | — | “Set to 1 to force walk anims to use run instead, or 2 to force run to walk.” | `0x1028cb10` | `0x10295590`: `+0x2c` int |
| `0x10923f5c` · object `0x10923f58` | `debug_alert_aggressive` | `"0"` | — | “Set to true to have alert states use the aggressive animations.” | `0x1028c9f0` | `0x103854f0`: `+0x2c` int |
| `0x10924034` · object `0x10924030` | `debug_hunting_aggressive` | `"1"` | — | “Set to true to have hunting states use the aggressive animations.” | `0x1028ca80` | `0x103854f0`: `+0x2c` int |
| `0x109245e4` · object `0x109245e0` | `npc_hit_buildup_amount` | `"2"` | — | NULL | `0x1029fe50` | `0x1029fec0`: `+0x2c` int |
| `0x1090fc0c` · object `0x1090fc08` | `flex_minplayertime` | `"5"` | — | NULL | `0x1025e230` | `0x1025fa50`: `+0x28` float |
| `0x1090fc9c` · object `0x1090fc98` | `flex_maxplayertime` | `"7"` | — | NULL | `0x1025e2c0` | `0x1025fa50`: `+0x28` float |

## Not ConVars, though the oracle wondered

- `DAT_10739a4c` is the discipline-record/namespace object read by `0x101e3f50`, not a `ConVar`.
- `DAT_10924edc` is the melee coordinator/event singleton; melee bodies call its slot `+0x04`, for example at `0x10385ab0`, rather than reading a ConVar value.
- `DAT_10924984` is the disposition-table default index read by `0x1029a0b0`; it has no constructor or writer.
- `DAT_1070ba0c` is an engine/interface table written by `CWorld::Precache 0x1023c020`, not a ConVar.
- `DAT_106eb5d8` is the global entity-list table; `vtmb_globals` reports 333 readers, not a ConVar constructor.
- `0x101e8bf0` is a function returning `*(float *)(param + 0x27c)` from the Rules record, as shown by its body at `0x101e8bf0`; it is not a zombie-grapple ConVar.
- `CNPC_VVampireBoss::CausePlayerAOEDamage 0x103c7230` reads victim slots `+0x50c` and `+0x500`; those are virtual slots, not globals.
- `_DAT_10940490` is runtime KeyValues state written by `CNPC_VZombie::vfunc510 0x103e1080`, not a ConVar.
- `shape.md:3838` leaves condition `0x78` and `m_ePhase` unresolved; `DAT_10924a6c` itself is the already-recovered `ent_trace_conditions` ConVar from initializer `0x1028bde0`.
- The weapon `+0x19c` in `shape.md:4887` and NPC fields `+0x626c`/`+0x6268` in `shape.md:5156` are fields, not ConVars.
- The named-master entry at `social.md:759` concerns `+0x5f5c`, an output block, and RTTI descriptor `0x105947c8`; neither is a ConVar.
- The current `programs.md:1310` entry is an authored map/script wire, not a ConVar global.

## Still unrecovered

Condition `0x78`'s name and `m_ePhase`'s values (`shape.md`); the RTTI target at `0x105947c8`
(`social.md`); the engine slot-74 query behind `DAT_1070ba0c` (`lifecycle.md`). None is a ConVar.
