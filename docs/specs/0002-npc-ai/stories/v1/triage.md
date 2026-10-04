# V2 — the consolidated triage

From `triage-P.md`, `triage-K.md`, `triage-W.md`, the full run `20261004T022053.584695Z` and the two
re-runs (`review.md` § runs). Times are scenario seconds.

## The result table

96 records (93 of V1 + `control_sequence` + the two self-tests). Final verdict per record, from the
full run where it ran and from the re-run where the full run errored or the record was corrected.

| result | n | records |
|---|---|---|
| pass | 54 | self-tests 2, `control_sequence`; `cover_reclaim`, `damage_idle_reaction`, `verbs_feed_trance`, `lifecycle_relationship_flip`; `fail_route_unreachable_sound`, `hear_world_out_of_range`, `idle_lookaround`, `interest_mode_never`, `sense_beyond_vision`, `sense_bodies_transparent`, `sense_cone_outside`, `unknown_crouched_band`; `input_setrelationship`, `input_takedamage`, `input_teleporttoentity`, `input_tweakparam_vision`, `map_tutorial_idle`; roll call 34 |
| expected-fail | 35 | red 1: `cover`, `patrol_sentry2_pingpong`*, `patrol_monk_loop`*, `input_clearpatrolpath`*; red 2: `range_bands`; red 3: `sense_enemy_facing_me`, `ranged_open_fire`, `damage_lethal_death`; red 5: `lifecycle_unhide_fights`, `rollcall_vmanbat`, `_vmercurio`, `_vsabbatleader`, `_vvampireboss`, `_vpedestrian`, `_vwerewolf`; red 6: `places_pedestrian_visit`, `places_thug_pt1`, `input_useinteresting`, `map_hub_idle`, `map_tutorial_sneak_past`, `hub_crosswalk_wait`, `rollcall_vhuman`, `rollcall_vhumancombatpatrol`; new: `cover_armed`, `chase_melee`, `melee_swing`, `script_walk_to_mark`, `script_dialog_hold`, `input_startplayerdialogremote`, `input_changeschedule_reselect`, `input_disablethink`, `maker_respawn`, `rollcall_vanimal`, `rollcall_vdog`, `rollcall_vscurrying` |
| fail | 4 | `sense_cone_enter`, `memory_occluded_kept` (harness H2), `rollcall_vzombie` (harness H11, undetermined), `verbs_stealth_kill` (unclassified) |
| unexpected-pass | 1 | `hear_world_investigate` (N4 is intermittent: heard this boot) |
| error | 2 | `rollcall_vcamera`, `rollcall_vcamerasecurity` (harness H1) |

\* provisional (review doubt 1). The full run alone: pass 23, expected-fail 23, fail 5,
unexpected-pass 1, **error 44** (42 records poisoned behind `rollcall_vcamera`, H1); its arena boot
also ran every record after `verbs_stealth_kill` with a crouched player (H4), which turned
`sense_beyond_vision` red (`SEE_UNKNOWN` at 8.2 s, retail's answer for a crouched player).

## The known reds

| # | shown? | records | signature (trace line) |
|---|---|---|---|
| 1 arbiter | yes | `cover` (and behind other reds: `cover_armed`, `cover_reclaim`'s clips); provisionally the patrols (doubt 1) | `cover` 7.400 `arena_gunman sequence smith_lean_left_into rate=0` after `task_play_cover_outof`, no `seqfinished` by 17.4 |
| 2 attack conditions | yes, after the correction | `range_bands` | probe at 16.2 `TOO_CLOSE_FOR_RANGED` read **true** one gather after 16.100 `cond+ CAN_RANGE_ATTACK1 (0x4f)` (the tail `0x1026e0b0..0x1026e107` would have cleared `0x08`) |
| 3 anim chain / slot 363 | yes | `sense_enemy_facing_me`, `ranged_open_fire`, `damage_lethal_death`; every hostile trace | 0.000 `sight_guard cond+ BEHIND_ENEMY (0x57)` with the player facing it; `damage_lethal_death` end probe `on_ground` false after 3.017 `death none` |
| 4 `0xef` sink | **no** | no record stages `NOT_FACING_ATTACK` or `WEAPON_THROUGH_WALL` under `0xef` | — (V5 needs a record: the player behind the shooter's back at 96 cm) |
| 5 hidden / flipped | **hidden half yes; flip half no** | `lifecycle_unhide_fights`, 6 hidden roll-call rows; the animals (N10) | 0.000 `arena_hidden cond+ FLOATING_OFF_GROUND (0x73)` then `schedule FALL_TO_GROUND (0x3e)` before the 2.0 `ScriptUnhide` |
| 6 executor | yes, wider than written | the places, `input_useinteresting`, both hub records, the sneak-past, `rollcall_vhuman`, `rollcall_vhumancombatpatrol` | `map_hub_idle` end probe `pedestrian_female` schedule reads `SCHED_NONE`; `places_pedestrian_visit` no event from the NPC in 3 s |
| 7 reach | no | no record can show it in one run | — |
| 8 restart | no | `save_restore_mid_path.json.parked` (H8) | — |
| 9 one clip | no | a draw needs repeated runs | — |
| 10 19 inputs | no | no record fires an unregistered input (V7's table test) | — |
| 11 `UpdateTargetPos` | no | no record stages a live `m_hTargetEnt` | — |

**Red 2, the two passes.** `range_bands` staged the right case but probed it wrongly: `0x08` is set
at 96 cm, then at 10 m `0x4f` rises at 16.1 and `0x08` stays set beside it until the program
install at 16.3 wipes the set; the record's probes (7.5, 18.0) both sat after an install. Corrected
(`review.md`); now expected-fail. The red is real but narrower in what it costs: in this staging
every band change soon meets a program install, so the stacked pair lives ~0.2 s; it decides the
selection only when a selection falls inside that window (16.3 here chose `0xef` — the trace cannot
say whether from the `0x4f` arm's 20 % draw or from the stale `0x08`). "Steps back forever"
was not reproduced as stated; whether `TASK_STEP_BACK` moves the body is review doubt 3.

**Red 5, the flip half.** Both flip records pass: `lifecycle_relationship_flip` (`D_NU 5` → `D_HT 5`)
and the corrected `input_setrelationship` (`D_LI 0` → `D_HT 5`): SEE_HATE, NEW_ENEMY, OnFoundPlayer,
`-> Combat`, START_COMBAT one gather after the input. `input_setrelationship`'s red was a staging
error (the player outside Hunter1's cone). So "a flipped NPC never enters `Sighted()`" is narrower
than the spec says: not reproduced for an NPC with the player in its cone; the 2026-09-30 case was
likely a cone or `use_interesting` staging (red 6), not the flip. V6 keeps it only as a re-check on
the maps (V8). The dialogue-blinds and `map_load` halves have no record.

**Red 6, wider than written.** The spec line names the release in `ThinkSchedulePolicy`
(`ElysiumNpc.cpp:985-988`). The trace shows more: a `use_interesting 1` NPC with no patrol path never
runs a program at all — `RouteScheduleMaintenance` → `ThinkAutonomous` → `ThinkAmbient`
(`ElysiumNpc.cpp:993-1007`) replaces `MaintainSchedule` for it, so retail's `0xff` (case 1 step 4,
`0x102af6eb`) never installs. Lane W's "no gather" is **not** supported: `RunAI` gathers before
`MaintainSchedule` for every NPC (`ElysiumNpcThink.cpp:375` → `ElysiumNpcBaseRunAi.cpp:67`); the
records simply staged no stimulus that would change a condition. Same mechanism (the STORY8-TWIN
executor, `ElysiumNpc.cpp:899-904`), same owner (V3). It also silences, until something sets COMBAT,
the tutorial's `thug_1`, every hub pedestrian and any input fired at such an NPC.

## New reds and their placement (bug protocol step 4)

| # | red | records | retail | port | placement |
|---|---|---|---|---|---|
| N1 | the cover tail never reads the enemy's weapon: `0xa4` against a ranged player | `cover_armed` | `0x102b7690` `0x102b78a2..0x102b78ee`: slot 167 → `+0x9c` → `GetActiveWeapon` → weapon slot 360 `& 0x6000` → `0xa3` (`0x5cd9`) | `ElysiumNpcSchedule.cpp:484-495` stub ("no cross-entity weapon capability reader") | **(a) V5**, S: a reader "enemy's combat character → active weapon → slot 360 word" (`FElysiumWeapon` capability; the player's inventory active item); files `ElysiumNpcSchedule.cpp` + the weapon/inventory accessor. Caveat: the trace cannot show the player's wield (H5) |
| N2 | `TASK_WAIT_ATTACK_TIME1` completes on the frame it starts | `ranged_open_fire` (behind red 3); visible in `range_bands` (6.700 `task` and `taskdone task_wait_attack_time1`) | `StartTask 0x102a337d`: `m_flWaitFinished = weapon[+0x730+4i] (0x10252450, the next-attack stamp) + 0x102c5730 (RandomFloat(template +0x264, +0x268) scaled by range, 0x102c5570)` | `ElysiumNpcStartTask.cpp:575-579` seam answers `curtime` | **(a) V5**, S: read the weapon's next-attack stamp and the NPC rate; files `ElysiumNpcStartTask.cpp`, the weapon's next-attack word |
| N3 | slot 599 never admits melee: no NPC ever enters melee | `chase_melee` (0.500 `SCHED_TROIKA_WAIT_FOR_MELEE_ADVANCE (0xe7)`), `melee_swing` (0.500 `SCHED_TROIKA_WAIT_FOR_MELEE (0xe4)` at 47 units) | `0x102b5650` last term `0x1025db70(m_pAttackCoordinator +0x65e8, this)`: true when listed or `count < cap` (appends) | `ElysiumNpcTroikaHelpers.cpp:134-160`: all four coordinator seams (`0x1025db50/db70/dca0/de90`) answer false; "`+0x65e8` is an index of three globals with no object behind it" | **(b) planning bug.** R4 owns the coordinator, but gate 2's melee scenarios (and V4's melee-and-die) cannot go green without it. Pull **the coordinator's list** forward into step 2 (V5 or a new S–M story "the attack coordinator": the object behind `+0x65e8`, its cap, the four bodies), leaving squads and followers in R4. Behind it, already in the traces: `DIST:COMBATMOVE` resolved to `-1e+06` (`ResolveTaskDistance 0x102702d0` sentinel) and `task_choose_best_melee_weapon` failing `No weapon to choose (0x1f)` for a bat-armed NPC (lane K) |
| N4 | a heard sound is sometimes never heard (intermittent: red in 3 lane-P boots, heard at +0.92 s in the full run) | `hear_world_investigate` | `CanHearSound 0x1030f7b0` tests only "inserted after `m_LastListenTime` (+0x84)" and the radius, no expiry; `CSoundEnt` think `0x101ba890` prunes a sound at `expire + _DAT_10450aa0` | `ElysiumNpcSenses.cpp:713`: `Event.ExpireTime < Now` drops it in `Listen`; the bus cursor moves past it (`:697`) | **(a) new step-2 story "V10. A sound's life in Listen"**, S: drop the expiry test from `Listen`, prune the bus at `expire + grace`; plus a read of why no `Listen` ran within 1 s at 768 units (needs H10's think event). R1 keeps the producers (footsteps) and the list's other words. Files `ElysiumNpcSenses.cpp`, the game-sound bus |
| N5 | `DisableThink` with the wire's string `'1'` disables nothing | `input_disablethink` (5.07 `taskdone` while disabled) | `0x1029f2a0`: the variant (converted by `AcceptInput` to the datamap type) of type 5 → `SetDisableAI 0x1029f300(value)`, any other type → 0 | `ElysiumNpc.cpp:1387` accepts only a Bool variant | **(a) V7** (inputs), XS |
| N6 | `ChangeSchedule '-'` forces no reselection | `input_changeschedule_reselect` | `0x102c33f0`: a name other than `-` → `0x102ae7f0`; **every** call `flags2 |= 0x82000000` | `ElysiumNpc.cpp:1672` → `StartNamedSchedule` (`-` is no name) | **(a) V7**, XS |
| N7 | `BeginSequence` possesses nothing the kernel sees: no `NPC_STATE_SCRIPT`, no `0xf2` | `script_walk_to_mark` | `PossessEntity 0x101a7880` → state SCRIPT; `0x1028a380` case 4 → `SCHED_AISCRIPT 0x2e` → `0xf2` | the director's 0.05 s beat, `ElysiumScriptedSequence.h:24-31` (divergence 18) | **(a) V3** (its stated scope) |
| N8 | `StartPlayerDialog` / `…Remote` install no program; the dialogue opens at the input | `script_dialog_hold`, `input_startplayerdialogremote` | `0x1029ef80` forced `0x6d`, `0x1029f060` forced `0x6e`; `OnDialogBegin` from their tasks | `ElysiumNpcDialogue.cpp:66`, `:79` open synchronously | **(a) V3** (the dialogue hold as `0x6a RUN_DIALOG`) |
| N9 | the maker makes no second child after the first dies | `maker_respawn` (OnNPCDied 4.05, nothing by 9.55) | `MakerThink 0x1034bbf0` re-arms at `SpawnFrequency`; `DeathNotice 0x1034bc90` drops the live count | `MakerThink`/`DeathNotice` read retail-faithful (`ElysiumNpcMaker.cpp:557-571`, `:205-234`); the refusal must be `CanMakeNPC` (`:303-360`) — doubt 4 | **(a) V6 rider**, S, read first (the refusal arm, logged at Verbose `:387`). The maker is landed work; R6 keeps templates and hidden makers |
| N10 | hidden animals select nothing, before or after the unhide | `rollcall_vanimal`, `_vdog`, `_vscurrying` | red 5's chain, then the Animal line's select (`0x1035fb50`) | `ElysiumNpcSelectSpecies.cpp:266-302`; not walked | **(a) V6** with red 5, plus a read of the Animal select (doubt 2) |
| N11 | the stealth-kill target test admits only an IDLE or ALERT victim (found by wave H's coder C, reading the query for the `stealthkill` tap) | none yet: `verbs_stealth_kill`'s mark is IDLE, where both arms agree. A record needs a victim in another live state with neither `HEAR_PLAYER` nor `SEE_PLAYER` (`NPC_STATE_SCRIPT` under a `scripted_sequence`, which N7 blocks until V3) | `IsValidStealthKillTarget 0x102c2300` term 4: `GetNPCState` IDLE/ALERT only when `debug_allow_non_idle_auto_sk` (`0x10924af8`, read at `DAT_10924afc`) is 0; the image's initialiser gives it `"1"`, so the shipping arm is "any state but DEAD (7)" (`docs/vtmb/stealth.md:405`, corrected 2026-09-29, `kernel_tunables.tsv` `DebugAllowNonIdleAutoSk`) | `ElysiumNpc.cpp:2360` tests `Idle`/`Alert`; its comment (`:2345`) calls the relaxed arm "a developer arm, not ported" — the corrected doc says it is the shipping one | **(a) V7 rider**, XS: the state term reads "not DEAD", the ConVar's `1` default named at the line; the record lands with V3's scripted possession (N7). Landed work (the stealth-kill verb): the fix is proposed to the owner with V7 |

Every new red lands in step 2 except N3, the one planning bug among them. A second planning bug,
not a red yet: **V8's tutorial check needs heard footsteps** (`map_tutorial_sneak_past`'s hearing
half; the record's own note), and the footstep producers are R1's. Either pull R1's footstep
`CSound` producer forward into step 2 (with N4's story), or re-cut V8's tutorial clause to sight only.

**`verbs_stealth_kill` — classified by wave H: red 3.** With the `stealthkill` event, `player_crouch`
and the player probes (wave H, 2026-10-04): crouched and knife wielded at 1.5 s; `FindVictim` admits
the mark at 0.917 (`arena_mark admit gate=can_grapple`), `+use` at 2.0 opens the type-3 grapple
(`OnGrappleBegin` 2.017), the synchronized death, `OnDeath` and the corpse land at 5.367. Every
expectation is met; only the end probe `on_ground` reads false — red 3's corpse, as
`damage_lethal_death`. `known_red` red 3; the record is `expected-fail`. The V2 failure was the
harness's (the typed `+duck` latch; H4). The paragraph below is V2's reading, kept for the record.

*V2's reading (superseded).* First unmet `death` by 12.0; `+use` (2.0) and `+attack`
(4.0) produce nothing but `HEAR_PLAYER` from 4.8. What the sources allow: the crouch took (the same
`elysium.cmd +duck` crouched the player for every later record of the boot, H4), and `slot2` wields
the knife (`damage_idle_reaction`'s 40-point hit in the same boot). Left: whether `elysium.cmd +use`
reaches `PlayerUse 0x10167850`, and which `FindVictim 0x101be1f0` gate refuses (the ray over
`StealthKillDistMax` 70, `IsValidStealthKillTarget 0x102c2300`, `InDeafArc 0x101be500`,
`CanStartGrappleAttack(3)`; port `ElysiumGrapple.cpp:93`). The harness must expose (H5): a probe
of the player's posture and active weapon, and a trace event for the stealth-kill query naming its
answer and the refusing gate.

**Harness, not game**: `sense_cone_enter`, `memory_occluded_kept` (H2), `rollcall_vcamera`,
`rollcall_vcamerasecurity` (H1), `rollcall_vzombie` (H11), `unknown_crouched_band`'s light (H5).
Wave H closed H1, H2, H5's halves for these: `sense_cone_enter`, both cameras and the light now pass;
`memory_occluded_kept` moved to a new question (§ "Wave H", below). H1's camera half was a game
defect, fixed at its root: `CNPC_VCamera::Precache 0x103689c0` gives a model-less camera
`models/null.mdl`, and the character admission refused the geometryless model, so the barrier
waited forever (a real map with a camera, `ch_zhaos_1`, would hang the same way).

## Records whose verdict depends on occlusion (H2)

The arena's solids are `BlockAll` boxes (`ElysiumArenaBuilder.cpp:81`); `BlockAll` lists no custom
channel, so each takes the channel default, and `ElysiumSight` is `DefaultResponse=ECR_Ignore`
(`DefaultEngine.ini:63`). Confirmed: NPC sight passes through every arena wall and the block (the
baked `ElysiumPropSolid` profile, `:49`, blocks it; the arena does not use it).

- **Red because of it**: `sense_cone_enter`, `memory_occluded_kept`.
- **Pass that does not discriminate**: `sense_bodies_transparent`.
- **Possibly affected, re-read after the fix**: `cover`, `cover_armed`, `cover_reclaim` — the cover
  validity test does not trace sight (`IsValidCover 0x1028af20` is a stand test,
  `ElysiumNpcBasePositions2.cpp:77`), but the in-cover chooser arms read `ENEMY_OCCLUDED` (`0x9d`)
  and `0x102b5de0`, which an occluding block would change.
- Sight lines in every other arena record were checked against the room (`ElysiumArenaSpec.cpp`):
  `far_ne`→`cover_seat` clears the block by a `static_assert`; the old `north`→`start` line of the
  Hunter1 records crossed the block (moved, `review.md`); `hear_*` / `fail_route_*` rely on distance
  (290-unit sight), not occlusion. Map records are unaffected (baked brushes answer by contents).

## The harness wave (deduplicated, ordered by records unblocked)

| # | gap or fault | records | file it would change |
|---|---|---|---|
| H1 | a row with no `model` key never activates the stage, and a Failed stage errors **every later record of the boot** | 2 directly, 44 poisoned in the full run | `Private/Debug/ElysiumArenaStage.cpp` (residency wait without a model; a failed stage rebuilt per record), `ElysiumArenaScenarioRunner.cpp` |
| H2 | arena solids transparent to NPC sight | 2 red, 1 non-discriminating, 3 to re-read; every future occlusion record (R1's footsteps-behind-the-block) | `Private/Debug/ElysiumArenaBuilder.cpp:81` (profile `ElysiumPropSolid`, or `ElysiumSight` → Block) |
| H3 | `never` with a start (`after` a label / `from` a time) and a count (`at_most`) | the roll call's `taskfail` (48: a storm vs one legal fail), the churn (`_vtaxidriver`, `_vrat`), `fail_route_*` "no storm", `input_disablethink`'s window, `maker_respawn`'s second-spawn window, "nothing after death", the map smokes' other NPCs | `Private/Debug/ElysiumArenaScenario.cpp` (schema), `ElysiumArenaScenarioRunner.cpp`, `Arena/README.md` |
| H4 | **player state leaks across records in one boot** (posture: `verbs_stealth_kill`'s crouch stayed for every later record) | every arena record after a crouch, feed or grapple; `sense_beyond_vision` red in the full run | the seat's steps (`ElysiumArenaStage.cpp:408` `SeatPlayerAt`): reset duck, wielded weapon, grapple/feed state per record |
| H5 | probes of the player (active weapon, posture, grapple, stealth-kill eligibility), a stealth-kill trace event, a `player_crouch` action and a light pin (`debug_stealth_light`, `Substrate/ElysiumStealth.h:60`) | `verbs_stealth_kill`, `cover_armed`'s caveat, `unknown_crouched_band`'s light assumption, V8's light-scalar clause | runner probes; `Substrate/ElysiumGrapple.cpp:93` tap; `ElysiumStealth.h` |
| H6 | `from_map` by more than targetname (unnamed rows: patrol points, hub places) | the patrols as arena records, `input_clearpatrolpath`'s hand-placed points, places on retail rows | `ElysiumArenaStage.cpp` `AddFromMapRows` |
| H7 | an arena variant with an unreachable-but-seen spot, a sealed pocket, a blocked route, an unreachable cover hint, a door | 0 records today; the `CHASE_ENEMY_FAILED` witness, `0x0c`/`0x0d`, the stale mark, the `+5.0` hint release, door refusal | `Private/Debug/ElysiumArenaSpec.cpp` |
| H8 | `save` / `load` actions | `save_restore_mid_path.json.parked`, red 8 | runner actions |
| H9 | `fire` with an activator | a scripted `TakeDamage` with an attacker (`damage_idle_reaction`'s first form) | runner `fire` → `FElysiumInputArgs` activator |
| H10 | a think / condition-gather event kind (or a stamp probe) | the cadence story 0002/15 (no record), N4's cause | the trace seam (`stories/wave2/seam.md`) |
| H11 | `schedule` tap misses a spawn-time install (`rollcall_vzombie`) — or the install bypasses `SetSchedule` | 1 | the schedule tap / `ElysiumNpcSelectSpecies.cpp:313`'s install path |
| H12 | a dialogue answer / close action | `script_dialog_hold`'s release half | runner actions |
| H13 | ensure / assert counts and the script's actions in the index / trace | the map smokes' "0 ensure / assert" (V8) | `ElysiumArenaScenarioRunner.cpp` |
| H14 | `taskdone task_get_path_to_patrol_point` emitted twice per goal; README says `(0xNN)`, the trace prints `(0xN)` | — | the taskdone tap; `Arena/README.md` |
| H15 | the maker's refusal reason only at `Verbose` | `maker_respawn`'s diagnosis | the arena host's log verbosity for `LogElysiumNpcEnt` |

## Wave H (2026-10-04): what moved, and the two questions it opened

H1–H5 landed (the integrator's report: `spec.md` § Step 2, H). Full run `20261004T030430.860212Z`: 103 records, 61 pass,
35 expected-fail, 5 fail, 1 unexpected-pass, 1 error (the designed `stage_failed_a`, since parked).
After the by-name re-run (`20261004T031612.315382Z`; 102 records, 62 pass, 36 expected-fail, 3 fail,
1 unexpected-pass `hear_world_investigate`, N4) the fails are `cover_reclaim`,
`memory_occluded_kept`, `rollcall_vzombie` (H11, not this wave's). The two open questions were
settled 2026-10-04 against the listing: both are **record errors**, corrected and green in the
by-name run `20261004T033433.394856Z` (2 pass).

- **Q-H1 `cover_reclaim`: record error (verified).** `0x102b7110` brackets its search with
  `m_bForceCoverLOSCheck +0x6408`; `0x102d2980` admits a cover node through slot 566 `0x10295c20` →
  `0x10297430` / `0x102974f0` → `0x10296c40`, whose last arm under `+0x6408` is the hint LOS
  `0x102968f0` (hint position + hull `maxs.z` to the enemy's eye, mask `0x46804099`, fraction 1),
  "Failed hint LOS" otherwise. With flags 8 (bit 0 clear) the first admitted node in list order wins
  (head = `cover_corner_nw`). From `cover_seat` the lines from `_nw` and `cover_low_north` cross the
  96-unit block, so with the block opaque both are refused: `_ne` is claimed first, and once both
  corners are killed nothing is admitted, `0x102b7690` answers 0 (no `m_pHintNode`) and the gunman
  fires (`0xed`). Port: `FindTacticalHintNode` (`ElysiumNpcTroikaHelpers2.cpp:196-199`),
  `ValidateHintCoverRange` (`ElysiumNpcHints.cpp:289-296`), `HintLosCheck`
  (`ElysiumNpcKernelBaseHelpers.cpp:833-899`) follow it; `cover_low_north` can never pass the hint
  LOS in this room (every direction in its facing band crosses the block). Also corrected in the
  record: `Kill` on a hint is ScriptHide (`0x102d08c0` → slot 77), not the destructor; the release
  is NPCThink's slot-566 validation (`0x102930db`) → `ClearHintNode(5.0)` (`0x10293149`) +
  `SetCondition(0x29)` (`0x10293160`), ported at `ElysiumNpcThink.cpp:231-238`. Re-staged: the
  player at (-17.2, 308.1) Source units, where both corners pass (nw dot 0.513, line 16.9 units north
  of the block's corner; ne dot 0.589) and both low nodes fail their facing band; kill `_nw` only;
  the re-claim is `cover_corner_ne` in the same think as the break (8.750). The shift also explains
  `cover`'s move to `_ne` (its verdict unchanged).
- **Q-H2 `memory_occluded_kept`: record error (verified).** Retail has the gap. `GatherConditions
  0x1026ec30` runs slot 481 (`1026ee5b`: at the tenth miss `SetCondition(0x48)` and the two outputs)
  and then, in the same gather, the Troika half `0x1028e790` (`1026efa4`) →
  `0x1028e700(0x48, +0x62cc)`: the first pass with 0x48 standing arms the stamp at curtime +
  `m_flOccludedDelay` (`+0x62c8` = `m_flOccludedDelayNormal` without a hint, NPCThink `0x1029316b`;
  `rules.txt` `Npc_Combat_Info/OccludedDelayNormal` 0.50) and **clears** 0x48 while curtime < stamp.
  So the outputs fire at 2.0 and 0x48 is first reported at 2.5, exactly as traced. Port:
  `ElysiumNpcBaseConditions2.cpp:448-465`, then `Conditions19OcclusionReportUpkeep`
  (`ElysiumNpcConditions2.cpp:42-53`) → `RefreshOccludedCondition`
  (`ElysiumNpcConditionsBodies.cpp:501-521`); the tap (`ElysiumNpcBaseRunAi.cpp:75`) is right.
  Re-stated: outputs first (`by` 5.0, then `within` 0.1), `cond+ ENEMY_OCCLUDED` `within` 0.6.
  **Q-H3**, a lead, not settled: the guard's `task_get_path_to_enemy_lkp` goal (2.7) is the hidden player's
  real spot, not the last-seen (40, 250), and no `cond- SEE_ENEMY` appears after the teleport; it
  re-sees him at 4.6 by walking there. Needs a read of who wrote the LKP after 1.0 (slot 544 runs
  only with `+0x5b98 == 0`) and of OnLooked's SEE_* clear in the port.

Unchanged by wave H, stated for the next wave: the roll call's `never taskfail` stays at `at_most` 0
(the per-class bound needs each class's first program, `review.md`'s V1 follow-up); the churn
(`rollcall_vtaxidriver` 455 `IDLE_DISPOSITION` installs, `rollcall_vrat` 47 `LOITER`) is not stated
for the same reason; `maker_respawn`'s "no second spawn while the first child lives" needs a window
that closes at a label (`never` closes only at `until` seconds); "nothing after death" is not stated
(retail's dead NPC still runs `SCHED_DIE`, so the event set to forbid needs a read).

## The fix order — acceptance lists

| story | records that must turn green |
|---|---|
| H wave (H1–H5 first) | `rollcall_vcamera`, `rollcall_vcamerasecurity`, `sense_cone_enter`, `memory_occluded_kept`, `rollcall_vzombie`; classifies `verbs_stealth_kill`; re-check `cover*` and `sense_bodies_transparent` |
| V3 arbiter, scenes, dialogue, places | `cover` (with V4's `IsActivityFinished`), the patrols ×3 (if doubt 1 confirms red 1), `places_pedestrian_visit`, `places_thug_pt1`, `input_useinteresting`, `map_hub_idle`, `hub_crosswalk_wait`, `rollcall_vhuman`, `rollcall_vhumancombatpatrol`, `script_walk_to_mark`, `script_dialog_hold`, `input_startplayerdialogremote`; `map_tutorial_sneak_past`'s first half |
| V4 animation chain, slot 363 | `sense_enemy_facing_me`, `damage_lethal_death`, `ranged_open_fire` (with N2), `melee_swing` (with N3) |
| V5 attack conditions + N1 + N2 (+ N3 if pulled here) | `range_bands`, `cover_armed`, `ranged_open_fire`, `chase_melee`, `melee_swing`; red 4 needs a record first |
| V6 lifecycle + N9 + N10 | `lifecycle_unhide_fights`, `rollcall_vmanbat`, `_vmercurio`, `_vsabbatleader`, `_vvampireboss`, `_vpedestrian`, `_vwerewolf`, `_vanimal`, `_vdog`, `_vscurrying`, `maker_respawn`; `save_restore_mid_path` after H8 |
| V7 inputs + N5 + N6 | `input_disablethink`, `input_changeschedule_reselect` |
| V10 (new) a sound's life | `hear_world_investigate` green in every boot (run it 3× in different boot orders) |
