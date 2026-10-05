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
| 1 arbiter | yes; **its kernel half fixed by V3a (2026-10-04)** | `cover` (and behind other reds: `cover_armed`, `cover_reclaim`'s clips); ~~provisionally the patrols (doubt 1)~~ — not red 1: N13 | `cover` 7.400 `arena_gunman sequence smith_lean_left_into rate=0` after `task_play_cover_outof`, no `seqfinished` by 17.4. After V3a: 3.300 `smith_lean_left_into rate=1`, 4.400 `seqfinished`, `taskdone task_play_cover_outof`, 4.800 `task_range_attack1`, 5.300 its `taskdone`: `cover` green. The `Sequence` / `Ambient` / `Dialogue` / `ScriptedSchedule` claims remain for V3b–V3d |
| 2 attack conditions | yes, after the correction | `range_bands` | probe at 16.2 `TOO_CLOSE_FOR_RANGED` read **true** one gather after 16.100 `cond+ CAN_RANGE_ATTACK1 (0x4f)` (the tail `0x1026e0b0..0x1026e107` would have cleared `0x08`) |
| 3 anim chain / slot 363 | yes | `sense_enemy_facing_me`, `ranged_open_fire`, `damage_lethal_death`; every hostile trace | 0.000 `sight_guard cond+ BEHIND_ENEMY (0x57)` with the player facing it; `damage_lethal_death` end probe `on_ground` false after 3.017 `death none` |
| 4 `0xef` sink | **yes, closed by V5b** | `ranged_step_back_holds` | 0xef 6.100; behind teleport 6.400 at 517 cm; NOT_FACING_ATTACK 6.900; final task_wait_attack_time1 9.100; next 0xed breaks on one-point LIGHT_DAMAGE 9.700. Texts omit 0x61/0x3c/0x08; old 99 s was unfinished TASK_RANGE_ATTACK1. |
| 5 hidden / flipped | **hidden half yes; flip half no** | `lifecycle_unhide_fights`, 6 hidden roll-call rows; the animals (N10) | 0.000 `arena_hidden cond+ FLOATING_OFF_GROUND (0x73)` then `schedule FALL_TO_GROUND (0x3e)` before the 2.0 `ScriptUnhide` |
| 6 executor | yes, wider than written; **fixed by V3b (2026-10-04)**: every `use_interesting` NPC on every record now selects `0xff` and runs the programs; what stays red is N15, H16, Q-V3b1 (§ "V3b", below) | the places, `input_useinteresting`, both hub records, the sneak-past, `rollcall_vhuman`, `rollcall_vhumancombatpatrol` | `map_hub_idle` end probe `pedestrian_female` schedule reads `SCHED_NONE`; `places_pedestrian_visit` no event from the NPC in 3 s |
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
| N8 | `StartPlayerDialog` / `…Remote` install no program; the dialogue opens at the input | `script_dialog_hold`, `input_startplayerdialogremote` | `0x1029ef80` forced `0x6d`, `0x1029f060` forced `0x6e`; `OnDialogBegin` from their tasks | `ElysiumNpcDialogue.cpp:66`, `:79` open synchronously | **(a) V3** (the dialogue hold as `0x6a RUN_DIALOG`). **Closed by V3d (2026-10-04)**: the inputs install `0x6d` / `0x6e` through `0x102ae750(id, 0)` (packet R2: not forced) and open nothing; `OnDialogBegin` fires from `StartTalking 0x102c0270` inside the open; both records green (§ "V3d integration") |
| N9 | maker post-death occupancy was misclassified as a spawn failure | maker_refusal_measurement; corrected maker_respawn | 0x1034b692..737 /0x101cc9e0 has no alive test;0x1034bbf0 retries1..2 | V6 measurement: live0/global0; corpse#18 flags0x12000/life1 intersects. Normal Kill clears the box and child2 spawns | record correction proved; no maker branch repair |
| N10 | hidden animals select nothing, before or after the unhide | `rollcall_vanimal`, `_vdog`, `_vscurrying` | red 5's chain, then the Animal line's select (`0x1035fb50`) | `ElysiumNpcSelectSpecies.cpp:266-302`; not walked | **(a) V6** with red 5, plus a read of the Animal select (doubt 2). **Re-read by V3b (2026-10-04): N10 was red 6.** The three rows are `use_interesting 1` (`plus_cat`: groups 6), so the ambient executor held them; with it deleted they select `FALL_TO_GROUND (0x3e)` at 0.000 while hidden, which is red 5's hidden half (the records gained the hidden-row nevers V2 gave the others; `known_red` red 5). N10 is closed into red 5; doubt 2's read is not needed for it |
| N11 | the stealth-kill target test admits only an IDLE or ALERT victim (found by wave H's coder C, reading the query for the `stealthkill` tap) | none yet: `verbs_stealth_kill`'s mark is IDLE, where both arms agree. A record needs a victim in another live state with neither `HEAR_PLAYER` nor `SEE_PLAYER` (`NPC_STATE_SCRIPT` under a `scripted_sequence`, which N7 blocks until V3) | `IsValidStealthKillTarget 0x102c2300` term 4: `GetNPCState` IDLE/ALERT only when `debug_allow_non_idle_auto_sk` (`0x10924af8`, read at `DAT_10924afc`) is 0; the image's initialiser gives it `"1"`, so the shipping arm is "any state but DEAD (7)" (`docs/vtmb/stealth.md:405`, corrected 2026-09-29, `kernel_tunables.tsv` `DebugAllowNonIdleAutoSk`) | `ElysiumNpc.cpp:2360` tests `Idle`/`Alert`; its comment (`:2345`) calls the relaxed arm "a developer arm, not ported" — the corrected doc says it is the shipping one | **(a) V7 rider**, XS: the state term reads "not DEAD", the ConVar's `1` default named at the line; the record lands with V3's scripted possession (N7). Landed work (the stealth-kill verb): the fix is proposed to the owner with V7 |

| N12 | `TASK_WALK_RUN_PATH` chooses walk or run off the port's own distance, not `nav+0x14` (found by V3a's coder A2; filed by V3a's integrator, 2026-10-04) | none: no record reaches the task. One retail program runs it, `cnpc_vwerewolf/sched_vwerewolf_run_to_teleport.sch` (after `TASK_GET_PATH_TO_RANDOM_NODE`); `rollcall_vwerewolf` asserts only a first schedule | `0x102a4b4e` arm: `0x102a4b5c..0x102a4b62 FLD [nav+0x14]`, read unconditionally; `d*d <= nav+0x14` runs, else the uninitialised local. The word's writers: the arrival test `0x102f2ea0` (store at `0x102f2f21`) and the goal-less install `0x102ed430` | `ElysiumNpcStartTask.cpp` `TaskWalkRunPath` (~:1825) reads `bMoveIssued ? \|MoveGoal - Origin\|² : 0`; `Navigator.EndpointDistanceSqrUnits` is written only by `InstallPathNoGoal` (`ElysiumNpcBaseStartTask.cpp:2549`), the arrival test's store is a seam (`ElysiumNpcRunTask.cpp:263`, `ElysiumNpcStartTask.inl:95`). A2 switched the reader to the word; V3a reverted it (a behaviour change on an unported writer, outside V3a's scope) and named both addresses at the line | **(a) R2**, where the arrival tolerance `0x102f2ea0` already sits: the store and the reader land together. **Not a planning bug**: no step-2 record's verdict depends on the walk-or-run choice of this task |
| N13 | a patrol walks at ~0.44× retail's ground speed, with the walk clip playing (review doubt 1, settled by V3a: **not red 1**) | `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath` | `StudioFrameAdvance 0x1008f120` recomputes `m_flGroundSpeed +0x654` every tick (`0x1008f2fa` / `0x1008f306`) from `GetSequenceGroundSpeed 0x10091490`, weighted by the live `move_yaw` (`animation_and_movers.md` § the move_yaw fan); `move_yaw` is the angle between the facing queue's heading and the body's yaw, written by `0x102e19e0` (`shape.md:3003-3010`); `GetIdealSpeed 0x10091740` reads `+0x654` plainly (no playback rate); `MoveGroundStep 0x102e1760`. Retail `walk_0` 136.7 cm/s | V3a measured: the clip now plays (`walk rate=1`) and every leg time is unchanged to 0.1 s (`input_clearpatrolpath` 0.700 goal → 10.500 arrived, ~540 cm in 9.8 s on the flat arena floor, ≈ 0.55 m/s, before and after). The per-frame speed is `CommandedTravelSpeed()` (`ElysiumNpcBody.cpp:329-364`, written into `MaxWalkSpeed` each `AnimTick`, `:481`) = the walk fan read at the body's measured `move_yaw` (`ElysiumAnimationDriver.cpp:597-617`, `663-666`). 60.7 / 136.7 = 0.44 is the `walk_90` cell's ratio: the lead (not verified live) is a body walking ~90° off its path, its yaw turned to a facing target (`MotorHandFacingTarget`, `ElysiumNpcBaseFacing.cpp:170-187` → `ApplyFacingTarget`, `ElysiumNpcBody.cpp:1085-1087`) instead of retail's facing queue. Next step: a live read of `move_yaw` / ground speed on sentry2 (`ElysiumMcpTools.cpp:1281`) | **(a) V4**, facing (`GetIdealYawSpeed`, `FacingIdeal 0x10278c80`) and `StudioFrameAdvance`'s ground speed, both already V4's. **A plan correction, stated**: V3's plan (`stories/v3/README.md` §4) expected V3a to turn the three patrols green; they move to V4's acceptance list. Not a later-phase bug: V4 is the next story of step 2. Also seen, pre-existing and unchanged: `patrol_monk_loop` 33.033 `move fail 12` on the leg to pod_1, then a re-issued goal. **Measured 2026-10-04 (V4r reader R1, `stories/v4/packets-R1.md` § 4, lab on `input_clearpatrolpath`; the map record is refused by the lab): the lead is refuted** — mid-leg `move_yaw_vel` 0, the body faces its path, `speed2d` 101.278 cm/s, which is exactly the **female** bank's `walk_0` (sentry2's model; 136.7 is the male cell, so "0.44×" compared two bodies). The lost time is the arrival: the body reaches the node's last ~30 cm at 5.8 s and creeps exponentially for ~4.1 s (10.5 cm at 6.5 s, 1.4 cm at 9.8 s; `arrived` 10.428) — the crowd follower's slowdown at goal against a 1 cm acceptance radius (`Visual/ElysiumNpcBody.cpp` `ResolveFollowerRequest`, `ApplyCrowdState`; mechanism read in code, not toggled). Not baked data. The `known_red` texts' 136.7 is a record error for a female body |
| N14 | marker writer/save/consistency chain was incomplete; not an always-loses-place rule | place_marker_reservation passes; live visit/invalid-marker restore remains red | PickSpotFor0x102da0d0 -> AddMarker0x102da860 writes occupant/bounds before ClaimMarker0x102da7c0; ops0x102d9240/0x102d9320, LAST scan0x102db5e0 and validation0x10299a80 | raw writer/capacity controls landed; apply fence still lacks the held-place identity in live records | V6 remains open; cursor+0x5c40, failure+0x5c50 |
| N15 | `ClaimAmbientSpot`'s eligibility carries port terms retail does not have (the place type and the visitor class) (found by V3b's coder B1; filed by V3b's integrator, 2026-10-04) | `places_thug_pt1`, `map_tutorial_sneak_past` (first half): `thug_1` is `npc_VVampire`; `pt1`'s type `Idle` lists only `npc_VBrujah`, `npc_VGangrel`, `npc_VPedestrian`; 0.533 `task_find_interesting_place`, `taskfail No interesting places were available to go to. (0x22)` | `0x102dad60` (read 2026-10-04): NPC non-null, `+0x57c` enabled, `+0x57d` clear, free capacity `+0x584 - +0x58c - +0x588 > 0`, `+0x574 & npc+0x62dc`, distance² ≤ `[0x1049d28c]`; **no term reads the place's type or the visitor's class**. The type parser `0x102dd0f0` stores `AcceptedClasses` at type `+0x1a0` / `+0x1a4`; its one lookup `0x102dd630` has no caller in the corpus (`vtmb_callers`, `vtmb_grep`), so retail never gates a visit on it | `ElysiumNpc.cpp` `ClaimAmbientSpot` (~:1088): `TypeRow == null`, `TypeRow->Activities.IsEmpty()`, `!TypeRow->Accepts(Classname, StatTemplate)` refuse a place retail admits (a type with no INTO/idle activity is retail's `"Can not find interest"` arm in `0x102a9f40`, not a refusal) | **(b) planning bug, stated to the owner**: it blocks V3b's own acceptance, so it cannot wait for R2. Proposed: a **V3b follow-up wave** (XS, one coder + the integrator, one build) — delete the `Accepts` term and the two type terms (a live place always has a type: `Spawn` removes one without), check that `IsAvailable` reads `+0x57d`; with it H16 and Q-V3b1. Landed work (the place selector, 0018/10), so the fix is proposed before it runs |
| N16 | a pedestrian route never carries a crosswalk curb, so no pedestrian ever waits at a red crossing (Q-V3b1, settled 2026-10-04; § "V3b") | `hub_crosswalk_wait` (`known_red` N16 / V13) | the pedestrian chain `0x102fcd00` puts both curbs of a crossed pair on the route (`4 \| 0x20`, start node included); at the first curb `0x102f0400` → `0x102a0bc0` → `0x102a0b90` latches `AT_CROSSWALK`; `0x102a0d20` raises `CROSSWALK_DONTWALK`, which breaks `0x100`, and `SelectSchedule 0x102af660` answers `0x102` (`0x102af763..76c`) | the downstream chain is ported (`ElysiumNpcSelect.cpp:599-602`, `ElysiumNpcDialogueBodies.cpp:337-459`, `ElysiumNpcBaseAdvancePath.cpp:58-62`), but `NavLayPedestrianLegs` (`ElysiumNpcCrosswalk.cpp:101-194`, the named modernization of `0x102fcd00`) laid no curb on any of the hub's first routes, at least five of which cross the road between the pairs: every `MoveTo` goes straight to the place, no `waypoint passed`. Which test drops the pair (the 48-unit capture against the NavMesh `PointsCm`, the consecutive-pair rule, the start-node arm, the pedestrian filter's pricing of the crossing) is **undetermined**: it needs the route points logged against the six curb positions | **(a) V13 (new, proposed, awaiting the owner): "The pedestrian nav area in the hub's bake"** (re-placed by the V3b follow-up's integrator, 2026-10-04). The V3b follow-up's read settled the cause as **baked data, not the splice**: on `sm_hub_1`'s baked Recast meshes no roadway polygon carries `UElysiumNavArea_Pedestrian` (`NavAreaAt` answers `NavArea_Default` inside all 9 priced slabs), so the pedestrian filter's ×5–10 price applies to nothing, routes cut the road diagonally and the splice's 48-unit capture rightly finds no curb (§ "V3b follow-up"). A fault in landed work (0018/3's NavMesh bake, 0018/7's crosswalk) that needs pipeline work and a re-bake, outside V3. Not a planning bug: V13 is a step-2 story placed before the second V2 run. Caveat for the record: only first walks can cross (`TASK_WAIT_PVS` holds every pedestrian outside the player's PVS after its first visit, `0x102aad7e`, as retail). **Re-stated by the V13 wave (2026-10-04), the cause measured: the marks never reach Recast.** `UElysiumNavAreaComponent` (`ElysiumNavAreaActor.h:34`, a `USceneComponent` + `INavRelevantInterface`) never enters UE 5.8's navigation octree, so neither its pedestrian slabs nor its door cuts are ever offered to the generator; the floating-floor lead is not the cause (§ "V13 wave"). **Closed by the V13 final pass (2026-10-04)**, the chain measured: registration → the pedestrian area on the baked mesh → the ×8 route over curbs 258 then 259 → the wait (23.917 `0x102`) → the crossing (43.550 `break CROSSWALK_WALK`), § "V13 final pass" |
| N17 | a place authoring `max_npcs 0` admits one visitor: the port floors the capacity at 1 (flagged by the V3b follow-up's coder F1; filed by its integrator, 2026-10-04) | none: no record stands such a place. The corpus (`exports_v2/maps/*.entities.glb`, 2026-10-04, 5.8 s): 1293 `intersting_place` rows on 22 maps, every one authors `max_npcs` (`population.md:794`); values 1 ×1009, 2 ×128, 4 ×91, 3 ×30, 5 ×13, **0 ×9** (`la_empire_2` 1, `la_skyline_1` 3, `sp_soc_2` 4, `sp_soc_3` 1), 50 ×6, 8 ×4, 6 ×2, 15 ×1; `sp_tutorial_1` 29 × 1; `sm_hub_1` 1 ×45, 2 ×22, 4 ×6, 6 ×2, 8 ×1 | `0x102dad60` `0x102dad8c..0x102dada4`: `m_iMarkersAllocated +0x584` (key `max_npcs`, datamap offset 1412) `− +0x58c − +0x588 > 0`, no floor: a 0 row is refused by every NPC | `ElysiumInterestingPlace.cpp:86` `IsAvailable`: `Claimants.Num() < FMath::Max(1, MaxNpcs)` (and the debug line `:122`); the member's default `MaxNpcs = 1` (`ElysiumInterestingPlace.h:17`). **Not changed** by the follow-up: nine shipped rows author 0, so the floor changes behaviour on four maps; first read retail's value for an absent key (the constructor `0x102d99d0`) and what the bake writes for an authored `0` (`ElysiumNpcKernelBindings.cpp:1250`, the infra actor's key rebuild, `ElysiumInfraActorsTests.cpp:251`) | **(a) R2** (places and patrols): delete the floor once both defaults are read, cite `0x102dad8c`. Not a planning bug: no step-2 record reaches a `max_npcs 0` row (the witness maps author none) | V6 dependency measurement: place_marker_reservation proves raw capacity0 refusal through0x102da0d0; no general R2 selector closure claimed.
| N19 | a missed scene lookup's sequence 0 plays nothing and finishes on its first advance (found by the V3c integrator, 2026-10-04, once `StartSequence` writes `m_nSequence`) | `script_walk_to_mark` (7.900 `sequence seq 0 rate=0`, `seqfinished seq 0` in the same think); inferred, not traced: `Elysium.Substrate.ScriptedSequenceFlags` (a held 0x100 beat with no `m_iszPlay` fires `OnEndSequence` twice within 0.6 s: `TASK_PLAY_SCRIPT` sees the zero-length post-idle finish at once and runs `SequenceDone` again) and `Elysium.Substrate.Dialogue.BodyScene` (the fixture's model authors no `waveover01`, so the scene ends in one think) | `StartSequence 0x101a82d0`: lookup −1 → warning, `m_nSequence := 0` (`0x101a833d`), `m_flCycle := 0`, `ResetSequenceInfo 0x10090950`, which plays the model's own sequence 0 at `m_flPlaybackRate 1.0` (`0x10090a23`) and `StudioFrameAdvance 0x1008f120` raises `m_bSequenceFinished` only when that sequence's cycle reaches 1. Five shipped scene labels miss this way (`entity_io.md:2469-2476`, `animation_and_movers.md:2103-2107`: `pre_fight_bow`, three `ACT_COWER`, `ACT_DOORKNOCK`) | the sequence bridge's row 0 "plays nothing" (`ElysiumNpcBaseAnim.cpp` `ResetSequenceInfo` ~:111-117: cycle rate 10.0 for row 0, so it finishes on the first advance; the trace prints `rate=0`); the bridge numbers clips by name and has no notion of the model's first studio sequence | **Proposed (a) V4** (the animation chain: `ResetSequenceInfo` / `StudioFrameAdvance` are V4's), for the owner or the judge: row 0 must be the body's model's sequence 0 (its first `$sequence`), which needs the bake to carry the studio order (undetermined whether it does; if not, pipeline work, so the judge rules). Not a V3c regression: before V3c the cine played through the montage and never wrote `m_nSequence`. Event order differs only in time (retail's `OnEndSequence` waits sequence 0's length). **Ruled by V3c's closing integrator (2026-10-04): filed, on V4's judge list** (`stories/v4/brief-J-judge.md` item 4). The studio order IS baked: the body table's `rawIndex` is the global flat number (`sequenceBase` + the owner's position, `importers/body_data.py:83-89`), carried to `FElysiumNpcClip::RawIndex` (`ElysiumBodyData.cpp:14`), so no re-bake; what is missing is the embodiment query for the `RawIndex 0` clip, the bridge's row 0 from it and the trace's naming — a cross-layer change in V4a A2's files, not taken under the close's one-build cap. The tests seed the named clip instead (`ScriptedSequenceFlags`, `Dialogue.BodyScene`) |
| N20 | with the door cuts reaching Recast, the Rat mesh loses door 339's doorway and one bridging route (found by V13's second wave, 2026-10-04: `verify nav` on `sp_tutorial_1`, 2 new findings) | none (bake gate): `ground-links-hull-19` link 41 (15 → 48) "both ends on the mesh, no path"; `bridging-hull-19` link 105 (44 → 69) "does not path on its own agent's mesh" | retail's AIN link 41 carries GROUND for hull 0 AND hull 19 (`fields[1]=1`, `fields[20]=1`), nodes (−1288, 213, −101) → (−1300, 917, −101), through door 339 (`func_door_rotating`, box (−1369, 739, −84)..(−1227, 752, 201)): the graph builds through a standing door (`0x2000b` has no `MOVEABLE`, `navigation-jump-links.md:2400-2406`), so the rat walks it. Link 105 is a hull-19-only link (`fields[20]=2`) joining node sets the human links keep apart; nodes (691, −142, 285) → (645, 526, −101); it crosses no door box | `map_nav_doors.crossings` (`pipeline/src/elysium_pipeline/importers/map_nav_doors.py:157-176`) grows the door box by the hull's lateral radius on all three axes, Z included: the rat segment at z −101 passes 1.8 cm under the rat-grown floor (−84 − 15.24 = −99.2), so 339 is staged "human alone" (`ElysiumNavAreaTests.cpp` pins it) and its smart link carries only the Human, while the cut (per mesh) removes the doorway from the Rat mesh too. Link 105: inferred, not measured — the Rat mesh's previous walk route from 44 to 69 ran through a door that is now cut; which door is not measured | **Proposed: a V13 follow-up fix story** (landed work, 0018/7's door staging; a pipeline change and a tutorial re-bake, so it goes to the judge): the crossing test sweeps the hull's own vertical extent from the node (retail's hull boxes, `docs/vtmb/data/hull_table.json`) rather than growing the door's floor by the lateral radius, re-stages 339 for both agents, and the pinned link-agent table in `Elysium.Content.NavArea.Tutorial` follows; then the bridging route is re-measured. Until then the baked `sp_tutorial_1` on disk (2026-10-04 09:07, gitignored) carries these two Rat findings. **Closed by the V13 final pass (2026-10-04)**: the door test grows by the hull's own box (339 → both hulls, link 41 no longer a finding); link 105 is jump-only for the rat (`motion` 2) and `bridging_errors` now reports it under `jumpOnly` (no NPC plans a jump link, `0x102ff960` step 2), reversing 0018/7's decision (a) for the verdict; `verify nav` clean, § "V13 final pass" |

Every new red lands in step 2 except N3, the one planning bug among them, and N12, which stays in
step 3's R2 because no step-2 record needs it. (V3b, 2026-10-04: N14 lands in V6; N15 is a planning
bug — it blocks V3b's acceptance — and is pulled into a V3b follow-up wave, § "V3b".) A second planning bug,
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
| H16 | the arena's eight `intersting_place` anchor rows author `type Stand`, which `vdata/system/interestingplacetypelist.txt` does not carry (its 49 names start `Idle`, `Citizen_Idle`, ...), so `CAI_InterestingPlace::Spawn 0x102d9c20` removes every one (`"Could not find InterestingPlaceType Stand"` ×8 per arena boot) and the arena has no place at all (found by V3b's integrator, 2026-10-04) | `places_pedestrian_visit` (and any arena record that means to use the anchors as places) | `Private/Debug/ElysiumArenaBuilder.cpp:44` (`AnchorType`; its comment calls `Stand` a shipped row): an authored type whose `AcceptedClasses` admit the records' visitors while N15 stands (`Idle` admits `npc_VPedestrian`) |

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

## V3b (2026-10-04): what moved, what is left

The ambient executor is deleted; a `use_interesting` NPC runs retail's programs on every record.
Seen: `input_useinteresting` (the input at 2.017, then at the program's end 5.083 `0xff` →
`task_find_interesting_place` → `task_set_preserve_path 1` → `0x100` → `goal` → 22.933 `arrived` →
`task_face_interest`) and `map_hub_idle` (the end probe reads an `INTEREST` program) turned green;
on the hub eight pedestrians ran 17 visits `0xff` → `0x100` → `0x103` in 300 s with no task failure.
Verdicts per record: `stories/v3/report-B.md`. Left red, each placed:

- **N15** (above): the port's `AcceptedClasses` term refuses `thug_1` at `pt1` →
  `places_thug_pt1`, `map_tutorial_sneak_past`'s first half. V3b follow-up.
- **H16** (harness, above): the arena has no place (`Stand`) → `places_pedestrian_visit`. V3b
  follow-up (the harness is fixed, the record untouched).
- **Q-V3b1 `hub_crosswalk_wait`: settled 2026-10-04, game red N16 (above)**, not a record error and
  not the harness. Retail's chain, read off the listing (verified): `SelectSchedule 0x102af660` case
  1, `use_interesting`: `0xff` while the path type is not 8 and `+0x62ec` is empty (`0x102af713`),
  else `HasInterruptCondition(0x13 CROSSWALK_DONTWALK)` → `0x102` (`0x102af763..76c`), else `0x106`
  / `0x105` / `0x100`. `0x100`'s text interrupts on `COND_CROSSWALK_DONTWALK`, `0x102`'s on
  `COND_CROSSWALK_WALK`; `0x102a0bc0` (from the advance `0x102f0400`) latches `AT_CROSSWALK` and
  the link on a node waypoint whose next node closes a red pair; `0x102a0d20` raises 0x13 on the
  next think. The port follows each step (`ElysiumNpcSelect.cpp:592-611`,
  `ElysiumNpcDialogueBodies.cpp:337-459`, `ElysiumNpcBaseAdvancePath.cpp:58-62`). What breaks it is
  upstream: **no curb leg is ever laid**. A by-name run with `LogElysiumNpcEnt` Verbose
  (`20261004T053818.652082Z`; a temporary `console` action, removed) shows every pedestrian
  `MoveTo` issued straight to its place (filter `ElysiumNavQueryFilter_Pedestrian`, x5–x10) and not
  one `Move: waypoint passed`; yet at least five of the sixteen first routes cross the road the
  pairs span (installed map, Source units: curbs 258/259 at y 423/181, x −998; 260/261 at x −1350;
  spawns north of y 423 — `pedestrian_north` ×3, `male_asylum_patron` (−972, 474),
  `female_asylum_patron` (−1404, 475), the `bum_north` rows — picking places south of y 181:
  `conversation_spot`, `phone_spot`, the asylum spots, `bum_huddle_north` (−1640, −204)). Why the
  splice captures no pair is **undetermined, needs** the route's `PointsCm` logged against the six
  curb positions in `NavLayPedestrianLegs` (`ElysiumNpcCrosswalk.cpp:101-194`: the 48-unit capture,
  consecutive curbs of one walkable pair, the start-node arm). The record's window is right but
  narrow, and that is retail's too: after its first visit `0x103` ends in `TASK_WAIT_PVS`, which
  completes only for spawnflag `0x400`, `0x102c2430`, or the player's PVS (`RunTask 0x102aad7e`;
  port `ElysiumNpcRunTask.cpp:511-541`); with the player at the map's start only `prostitute_1`
  ever leaves it, so every other pedestrian crosses at most once, on its first walk (issued
  0.05–0.33 s). At retail's 136.7 cm/s a north spawn 700 units off curb 260 reaches it at ~13 s,
  inside `crosswalk_south`'s red (11.8–40.8 s; `streetlight_timer`: `Walk` +0, `DontWalk` +12)
  — inferred, the node routes are not walked. N13's slow walk (V4) moves arrival times but does not
  explain zero curbs.
- **N10 was red 6.** The three hidden animal rows are `use_interesting 1`; with the executor gone
  they select `FALL_TO_GROUND` while hidden (red 5's hidden half). Their records lacked the
  hidden-row nevers V2 gave the others (a record error, corrected); `known_red` red 5, V6.
- **Record errors corrected** (bug protocol step 1): `rollcall_vhuman`, `rollcall_vhumancombatpatrol`
  stated "no task fails" for a `use_interesting` row the arena gives no place in its groups; retail
  selects `0xff` and `TASK_FIND_INTERESTING_PLACE` fails `0x22` (`0x102a1f34`) once per fail-schedule
  cycle (~5.1 s). Re-stated: `0xff` by 3.0, the `0x22` failure, `taskfail` `at_most` 2 in 10 s.

## V3b follow-up (2026-10-04): N15, H16, N16

Three lanes and an integrator (`stories/v3/brief-B-followup.md`). Build 112 s; default 176 / 0
failed; arm 1551 / 0 failed; suite `20261004T063241.669369Z`: 105 records, 71 pass, 32
expected-fail, 1 fail (`rollcall_vzombie`, H11), 1 unexpected-pass (`hear_world_investigate`, N4),
both as before the wave. The one verdict that moved: `places_thug_pt1`, expected-fail → pass.

- **N3 closed** (V11, 2026-10-04). The coordinator is three objects on the entity world (`0x1025d880`); `melee_swing` is green and `chase_melee` meets every expectation (its end probe alone is red, placed on V4c C1: the swing clip's movement never moves the body).
- **N15 closed.** `ClaimAmbientSpot` (`ElysiumNpc.cpp` ~:1056) makes only retail's tests in
  retail's order: `0x102db470` (rating 5 → 0, `m_iRating +0x578`, not the last place `+0x62fc`,
  under 0x100 candidates per rating), then `0x102dad60` (enabled `+0x57c`, `+0x57d` clear,
  `max_npcs +0x584 − +0x58c − +0x588 > 0`, `+0x574 & npc+0x62dc`, distance² ≤ `[0x1049d28c]`); the
  last place by `0x102dad60` alone; `0x102db590` draws `RandomInt(0, n−1)`. The type and
  `AcceptedClasses` terms are deleted; no test pinned them. `places_thug_pt1`: 0.533 `0xff`,
  `task_find_interesting_place`, `0x100`, `goal 683 619 0` (pt1) → 3.667 `arrived` → 3.867
  `SCHED_TROIKA_DO_INTEREST_ACTIVITY (0x103)`, `OnInterestingPlaceArrived` → 54.167
  `OnInterestingPlaceLeft`, `0xff` again. `map_tutorial_sneak_past`'s first half is green (0.633
  `0xff` → 3.733 `arrived` → 3.933 `0x103`); its `known_red` names only V12 (below). Undetermined:
  whether `World->Entities()` order matches retail's newest-first place list (`DAT_10927194`,
  next `+0x540`); it changes only which candidate a draw lands on.
- **H16 closed** (harness). The arena's anchors are typed `Idle`, copied from `sp_tutorial_1`'s
  `pt1` (row 419, `programs.md:1485-1489`), with the keys every retail place row carries
  (`testflags 4`, `min_bounds -16 -16 0`, `max_bounds 16 16 72`;
  `ElysiumArenaBuilder.cpp` `AnchorRow`). `group_id 0` folds to group 1 (`0x102d9c20`), which
  `pedestrian_female`'s `1 31` admits. `places_pedestrian_visit` now runs the whole program and is
  red only on **N13** (V4): 0.000 `0xff`, `goal 0 305 0` (cover_east, ~1350 cm round the block),
  `walk rate=1`, no `arrived` by 20 s. A scratch copy with the bound lifted
  (`20261004T063102.216778Z`, deleted) holds every later step: 0.350 `goal 305 0 0` → 14.800
  `arrived` (~0.7 m/s; retail 1.367), 15.300 `0x103`, 23.300 `OnInterestingPlaceLeft`, `0xff`,
  `goal 0 305 0` → 33.700 `arrived`, no `taskfail`. Retargeted, not loosened.
- **N16 re-placed onto V13** (proposed, awaiting the owner; `spec.md` § Step 2). Lane F3's
  diagnosis (three by-name runs of a scratch copy, `20261004T060632.490887Z`,
  `20261004T061008.978113Z`, `20261004T061139.600446Z`; copy deleted): the cause is baked data, not
  the splice. On `sm_hub_1`'s baked Recast meshes no roadway polygon carries
  `UElysiumNavArea_Pedestrian`: `NavAreaAt` answers `NavArea_Default` inside all 9 priced slabs, so
  the pedestrian filter's ×5–10 price applies to nothing, routes cross the road diagonally, and the
  48-unit capture rightly finds no curb. The likely reason, measured but not proven: each slab
  convex spans z −298..−39 while the road surface is at z −303, so the convex floats 5 units over
  the road. The fix is in the mark staging (`pipeline/.../importers/map_collision.py`,
  `ElysiumNavAreaActor.cpp`) and a re-bake, outside V3. F3 kept one `Verbose` line per pedestrian
  route in `NavLayPedestrianLegs` (corners, each curb's closest approach, the curbs laid). Open:
  whether the floating convex is the whole cause, and whether the splice lays both curbs once the
  area is present (V13's two content tests).
- **N17 filed** (above, R2): `IsAvailable`'s `FMath::Max(1, MaxNpcs)` floor is not retail's, but 9
  of 1293 shipped rows author `max_npcs 0`, so it was left until both defaults are read. The
  header comment is corrected: `+0x584` is `m_iMarkersAllocated` (key `max_npcs`, offset 1412),
  `+0x578` is `m_iRating` (offset 1400).
- **Q-V3bf1, for V12's reader.** With the first half green, `map_tutorial_sneak_past` reaches the
  hearing half: thug_1 hears the player walk at 7.633 (`OnHearPlayer`, `HEAR_PLAYER`, break
  `INVESTIGATE_SOUND`, Idle → Alert) and runs `0x4c SCHED_TROIKA_ALERT_TURN_TO_SOUND` within 1 s,
  where the record expects a program matching `INVESTIGATE`. Retail's ladder `FUN_102b8980`
  answers `0x4c` at alert level 0 unless `m_bFullInvestigate +0x6340` is set
  (`programs.md:396-399`): whether `investigate_mode 4` sets it decides whether the match is a
  record error. Stays on V12.

## V13 wave (2026-10-04): the senses bugs landed, V13 not established

Two lanes and an integrator (`stories/v13/brief.md`). One build (1m47s), one bake of `sm_hub_1`
(1m35s, `NAV_AREA_ACTOR_SHAPE` 1 → 2, `verify nav` clean). Default 176 / 0; arm 1551 / 0 (one
lane test fixed, below). Suite `20261004T071616.934975Z`: 105 records, 70 pass, 32 expected-fail,
2 fail (`rollcall_vzombie` H11, as before; `map_tutorial_idle`, below), 1 unexpected-pass
(`hear_world_investigate`, N4, as before). Every other record passed or failed at the same point
as the V3b follow-up's suite; `memory_occluded_kept` passes with its two new SEE drops.

- **Q-V13a `map_tutorial_idle` is boot-dependent (not this wave).** The one verdict that moved
  (pass → fail). sentry2 (`use_interesting`) selects `0xff` at its first think, before its patrol
  wires (2.0), and the record's `never` forbids any `taskfail` until 1.8. On this one binary, four
  boots: first think 0.300 and 0.217 drew places at (−4341, −1189, −508) and (−4333, −843, −508),
  `move fail 12`, `Don't have a route (0xc)` → fail; first think 0.083 and 0.167 drew
  (−19370, −8057, 17432), the place the previous suite's pass drew → pass. The seed does not fix the
  map host's first-think time or the place draw (`0x102db590`), so the verdict is a draw. Open, for
  the harness / V2: why the first think moves per boot under `seed 1`, and whether retail refuses
  those two places (`programs.md` § "The failed walk": a refused interesting-place route is
  retail's), which would make the 1.8 s `never` a record error. Not loosened here.
  **Settled by V3c's integrator: harness (H17) + record error** (§ "V3c integration").

- **Q-H3 closed.** The LKP is retail's (above, "Filed by the coordinator"); the two old bugs the
  read found are fixed. `GatherSight` (`ElysiumNpcConditions.cpp:329-347`) clears the six SEE
  conditions at its head as `OnLooked 0x1026a2c0` does (`ClearConditions(0x105c979c, 6)`,
  `1026a2cf`: `43 45 46 44 5b 5a`); `Conditions19LastKnownPosition` returns the record's `+0xc`
  (`Anchor`) as `GetLastKnownPosition 0x102dfed0` does (`102dff66..7f`), and so do the four other
  `0x102dfed0` / `0x102e0290` readers (`ElysiumNpcSelect.cpp:1193`, `ElysiumNpcSelectSpecies.cpp:856`,
  `:1312`, `ElysiumNpcAnim10.cpp:392`, `ElysiumNpcStartTask.cpp:414`; `0x102e0290` copies `+0xc` at
  `102e032a..3a`). `memory_occluded_kept` re-stated with retail's cadence (record § `about`):
  SEE_HATE and SEE_PLAYER fall at 1.200 (teleport 1.000; worst case 1.0 + the 0.15 s kept-list
  rescan + one 0.1 s think = 1.25, deadline 1.3), SEE_ENEMY with `OnLostPlayerLOS` at 2.000 (the
  tenth miss, deadline 2.1), the LKP hunt's `goal -447 -152 0` at 3.000 (the hidden spot, written
  by the pass after the teleport). The lane's arm test staged its D_NU turn at priority 0, which
  `SetEntity` refuses under the D_HT 5 row (a staging error, corrected to 5).
- **Two readers flagged, not changed.** `ElysiumNpcStartTask.cpp:414-417`: `0x102e0290`'s second
  out is the record's `+0x18` (last known velocity, `senses.md` § The enemy memory); the port hands
  the position again. `ElysiumNpcBoss.cpp:130`: `0x102dfc10`'s two vectors to owner slot 56 are
  UNRECOVERED at the line. Both filed to **R2** (the enemy memory's remaining words); no step-2
  record reads either.
- **V13 not established; N16 re-stated with the measured cause.** `Elysium.Content.NavArea.Hub`
  on the OLD bake: the pedestrian area is listed (id 2); the road probe (−1700, −760, −303) lands on
  area 63 (`RECAST_DEFAULT_AREA`) — red; the three crosswalk-gap midpoints are unpriced — green; the
  ×8 route is `(-2294 -1071 -291) (-1976 -988 -298) (-1368 -494 -298) (-776 205 -278)`, 95.1 u from
  curb 258 and 259.7 u from 259 — red. After the bake with `SetIncludeAgentHeight(true)` on the
  slabs (convex floor lowered by the Human agent height, ~1.8 m below the road): **identical**,
  assertion by assertion and corner by corner. So the floating floor is not the cause, and G1's
  doubt 2 does not apply to this test (it reads the raw polygon id and the saved area row by name,
  not `NavAreaAt`'s class fallback). The cause, read in the engine (UE 5.8): the marks never enter
  the navigation octree at bake time. `UElysiumNavAreaComponent` is a `USceneComponent` +
  `INavRelevantInterface`; in 5.8 actors and components reach the octree only through the
  `UNavigationObjectRepository` (`NavigationSystem.cpp:5052-5057`, `AddLevelToOctree` adds BSP
  only), fed by `FNavigationSystem::OnComponentRegistered`. A plain scene component calls that only
  from `HandleCanEverAffectNavigationChange` (`ActorComponent.cpp:3274-3296`; the engine's own
  `UNavRelevantComponent::OnRegister` calls it, `NavRelevantComponent.cpp:59`); otherwise only
  `OnActorRegistered` notifies an actor's registered components (`NavigationSystem.cpp:4056-4072`,
  `ComponentShouldWaitForActorToRegister` true). The bake spawns the marks actor (registered with
  only its root) and `AElysiumNavAreaActor::AddArea` then creates and registers each component
  (`ElysiumNavAreaActor.cpp:93-138`): nothing notifies the navigation system, so `Nav->Build()`
  (`ElysiumNavBakeLibrary.cpp:424`) gathers no modifier from them (`RecastNavMeshGenerator.cpp:2138`,
  `AppendModifier :2522`). A level LOAD would register them (the actor registers with its saved
  components), which is why nothing looked wrong at runtime. **Inferred by the same path, not
  measured:** the door cuts (0018/7; hub 54 convexes, tutorial 60) are in the same components and
  have never cut a baked mesh either. `hub_crosswalk_wait` (expected-fail, unchanged): 11.833
  `crosswalk_south` `DontWalk`; in 280 s no `Move: waypoint passed`, no `AT_CROSSWALK`, no
  `SCHED_TROIKA_WAIT_AT_CROSSWALK`. Stopped as the brief says: no second bake, the fix not widened.
  **For the owner / the judge** (a bake and a behaviour change on every map's doors): register the
  components with the navigation system as they are added (what `UNavRelevantComponent::OnRegister`
  does), then re-bake `sm_hub_1` and `sp_tutorial_1` and re-run `Elysium.Content.NavArea.` and the
  tutorial records (doorways the graph has no link through become walls in the mesh). G1's
  `SetIncludeAgentHeight(true)` and its test extension are kept: its own question (whether the
  slab's floor needs it, `RecastNavMeshGenerator.cpp:4885` vs the cell rounding of the road's span,
  `dtMarkConvexArea` `DetourTileCacheBuilder.cpp:2583-2610`) can only be answered once the marks
  reach Recast. `Elysium.Content.NavArea.Hub` stays red on N16 until then.
- **The six other maps' slabs filed to R2** (`hw_hub_1` 13, `la_hub_1` 16, `ch_hub_1` 6,
  `hw_asphole_1`, `la_parkinggarage_1`, `sp_theatre` 1 each): they take the registration fix and
  `NAV_AREA_ACTOR_SHAPE` 2 at their next bake.

## V3c integration (2026-10-04): the scene runs as retail's program; not committed (build budget)

C1, C2, C3 and H17 (the map-host seed) integrated; cross-lane lines applied (the deleted
`ClaimScriptBody` block in `ElysiumAiScriptedScheduleTests.cpp`; `ElysiumNpcGait.h`'s `Script*`
constants and `TravelCapSeconds`, no readers). Ledger regenerated, `kernel --check` clean. Build 1
(2m00s) green; build 2 was the arm tier's own compile (`test` switches it on and builds: the
integrator should have used `build --arm` for build 1), so the four test fixes below need a third
build, which the brief forbids: **the wave stopped uncommitted, V3c not ticked.**

- **Default tier 167 / 4 failed of 171; families 94 / 4 failed of 98** (the scene, cine, choreo,
  dialogue-body, translate, anim, `AiScriptedSchedule`, `BaseHold` prefixes; `ChoreoScene`, every
  `Scene*`, `NpcKernelScript19.*`, `NpcKernelTranslate`, the new `AiScriptTranslate` green). Each
  failure classified, none a game regression of V3c:
  - `Elysium.Arm.NpcKernelDirector.Removal` (`:631`) — **test staging**: jack stays `Quiet`, so he
    never enters `NPC_STATE_SCRIPT`, and `ScriptEntityCancel` cleans up only a state-4 NPC
    (`0x101a71b8`). Fix: wake jack as C3 woke five others.
  - `Elysium.Substrate.ScriptedSequence` (`:441-464`) — **test staging** (inferred from the code):
    the fixture stands no services, so no motor; `TASK_FACE_SCRIPT`'s run arm completes only on
    `FacingIdeal 0x10278c80`, and `MotorUpdateYaw` turns nothing without a motor
    (`ElysiumNpcBaseRunTask.cpp:206`), so the 90° mark never completes. Fix: a motor reporting
    `Reached`, or delete (`script_walk_to_mark` is the live proof).
  - `Elysium.Substrate.ScriptedSequenceFlags` (`:647, :655`) and `Elysium.Substrate.Dialogue.BodyScene`
    (`:1744-1749`) — **N19** (above, inferred): a missed lookup's row 0 is zero-length. BodyScene's
    `PlayNpcClip … waveover01` and the `ScriptOwner` line also assert the beat's montage and claim
    (README §6 gives lines 1747-1756 to V3d's D1): re-stage with `waveover01` in `KnownNpcClips` and
    assert the bridge, not the montage.
  - `Elysium.Substrate.Npc.TravelSpeed` (`ElysiumNpcTests.cpp:1148-1255`) — **port mechanism**: its two
    scripted halves pin the deleted scripted-move seam's speed (M11). No lane owned the file; delete
    the two blocks.
- **`script_walk_to_mark`: the program is retail's** (by-name run `20261004T082341.856046Z`): 2.033
  `Idle -> Script`, `SCHED_TROIKA_SCRIPTED_WALK (0xf2)`, `task_walk_to_target`, `goal 279 -947 0`,
  7.150 `arrived`, `task_plant_on_script`, `task_face_script`, `task_enable_script`, 7.900
  `taskdone task_wait_for_script`, `OnBeginSequence` (never before 2.5 s), `sequence seq 0 rate=0`,
  `seqfinished seq 0`, `OnEndSequence`, `Script -> Idle`, `SCHED_TROIKA_IDLE_DISPOSITION (0x6b)`;
  a scratch copy (deleted) showed `input SetRelationship player D_HT 5` at 10.883. **Two record
  errors corrected**: `pre_fight_bow` is a content miss (the model spells `prefight_bow`,
  `entity_io.md:2471`), so retail plays sequence 0 (`0x101a833d`); the `OnBeginSequence` wire carries
  a 3.0 s delay (`AsianVamp,SetRelationship,player D_HT 5,3,-1`, the Unofficial Patch's
  `sm_vamparena.bsp` and retail's alike), so `flips` moved last with `within 3.2`. The `taskfail`
  `never` is bounded to 12.0 s: after the scene the boss selects `SCHED_VASIANVAMPIRE_JUMP_UP (0x15a)`
  and `task_vasianvampire_find_ledge_node` fails `No Target (0x1)` at 12.883, because the arena stages
  no ledge nodes. Now `expected-fail` on **N19** (`plays` expects `seq 0 rate=1`). **N7 is fixed in
  the tree** and closes with the V3c commit.
- **N11's record staged**: `combat/verbs_stealth_kill_scripted.json`, `expected-fail` (0.533
  `Idle -> Script`, `0xf8`, 0.917 `arena_mark refuse gate=valid_target`).
- **Suite** `20261004T082753.975867Z`: 105 records, 71 pass, 32 expected-fail, 1 fail
  (`rollcall_vzombie`, H11), 1 unexpected-pass (`hear_world_investigate`, N4). Moved:
  `map_tutorial_idle` fail → pass (below). The dialogue records and `script_aischedule_walk` stay
  red on V3d; the patrols and `places_pedestrian_visit` on N13; `cover` and `control_sequence` green.
- **Q-V13a — verdict: harness (H17) + record error.** H17 seeds `ElysiumRng::SeedAll`,
  `FMath::RandInit` and `SRandInit` from `-ArenaSeed=` at New Game (arena boots only). Three separate
  boots of `map_tutorial_idle` produced **identical traces, all 886 lines** (first think 0.283).
  The seed-1 draw is the refused place: 0.283 `move fail 12`, `taskfail Don't have a route (0xc)`
  (printed twice in the same instant). The record error: sentry2 and `mercenary_upstairs` compete for the
  same places, the loser draws one with no graph node in reach (`navigation-jump-links.md:157`), and
  retail answers `TaskFail(0x0c)` once, then waits 5.1–10 s (`programs.md:207-220`). Both
  `map_tutorial_idle` and `patrol_sentry2_pingpong` keep the pre-wire `never` (its `until`) but
  exclude that one failure (`^(?!Don't have a route \(0xc\))`, regex), with the caveat in `notes`
  that the docs do not confirm the exported graph is the one retail loads. pytest
  `test_arena_suite.py`: 30 passed with `NO_COLOR`; in a colour terminal
  `test_the_cli_registers_the_arena_command` fails because colour codes split the help text (not this
  wave's, left).
- **For the next integrator (one build)**: the four test fixes above, then the default tier, the arm
  tier and the families, then commit; also the brief's divergence-18 close and the K1 row
  (`stories/v1/divergences.md`), not written while uncommitted.
- **Closing pass (2026-10-04): the four fixed, two more found, still uncommitted.** Build
  `build --arm` 58 s, green. Default tier **171 / 0**. The four, each test staging:
  `NpcKernelDirector.Removal` (jack now thinks into SCRIPT; asserts state 4 before the kill);
  `ScriptedSequence` (the mover stands a model, a recording motor whose walks arrive and whose body
  settles a commanded turn, `FElysiumRecordingNpcMotor::bSettlesFacing`; the face task still waits on
  `FacingIdeal 0x10278c80`); `ScriptedSequenceFlags` and `Dialogue.BodyScene` (the named clips seeded:
  `Converse_Normal_Talk_A` on damsel, `waveover01` on smiling_jack; both BodyScene assertions kept, they
  read the bridge's play and `m_hCine`); `Npc.TravelSpeed` (only its two scripted-move blocks deleted;
  the patrol / fan / classifier halves pass and stay for V4 B1 to judge). **Arm tier 1548 / 2 failed**,
  both V3c staging, neither in the families the first pass ran: `NpcKernelMaintain19.SetSchedule`
  (`:273`, `:285`: the quiet subject is possessed but never enters SCRIPT, so `ForceScheduleChange`'s
  `CancelScript` → `ScriptEntityCancel 0x101a71b8` rightly cleans nothing) and
  `NpcKernelMaintain19.TaskMovementComplete` (`:421`: stages `ScriptOwner`, but `0x10273f01..14` read the
  NPC's `m_scriptState` ∈ {4,5,6}, `IsScriptDriven`). **Fixes written, NOT built** (no build left):
  `N.SetState(4)` before each `ForceScheduleChange`; `N.SetScriptState(4)` / `(0)` around the
  `TaskMovementComplete` call. Arena by name (`20261004T085623.382491Z`): `cover`, `control_sequence`,
  `map_tutorial_idle` pass; `script_walk_to_mark` (N19), `script_aischedule_walk`, `script_dialog_hold`,
  `dialog_use_hold` (V3d), `verbs_stealth_kill_scripted` (N11), `patrol_sentry2_pingpong` (N13)
  expected-fail. No runtime code changed, so the first pass's suite stands. `kernel --check` clean
  after a regenerate; pytest `test_arena_suite.py` 30 passed (`NO_COLOR=1`). **Next: one build, the two
  Maintain19 tests and `test arm`, then the commit, the V3c tick, divergence 18 and K1.** N19 ruled
  (row above): filed to V4's judge list. **Done (second authorized build, 12.7 s):** default 171 / 0,
  arm 1550 / 0; V3c committed and ticked, divergence 18 closed, K1 row 23.

## V13 second wave (2026-10-04): the marks reach Recast; stopped on `verify nav` (not committed)

Integrator only. The tree: `UElysiumNavAreaComponent::OnRegister` / `OnUnregister` call
`FNavigationSystem::OnComponentRegistered` / `OnComponentUnregistered`, as `UNavRelevantComponent`
does; `NAV_AREA_ACTOR_SHAPE` 3; door probes in `Elysium.Content.NavArea.Hub` / `.Tutorial`. Build
27.8 s (`--arm`); one bake of `sp_tutorial_1` + `sm_hub_1`, 147 s: `sm_hub_1` verify clean,
`sp_tutorial_1` **2 new findings (N20)** → stopped as the judge's rule says: no records, no suites,
no commit, V13 not ticked. Two read-only measurements on the new bake (no build):

- **`Elysium.Content.NavArea.`, old bake → new bake.** Road probe (−1700, −760, −303): area 63 →
  **pedestrian (green)**. Three gap midpoints: unpriced → unpriced (green). Linked door cuts: hub
  2566 (convex 52, probe (−5481, −1440, −284)) and tutorial 183 (convex 7, (−3263, −940, −508)):
  area 63 → **nothing walkable (green)**. `Tutorial` green whole. ×8 route: still red, but changed —
  `(-2294 -1071 -285) (-1976 -1121) (-1197 -1197) (-1083 -1197) (-1007 -1121) (-1007 -418)
  (-1064 -266) (-1064 -171) (-988 -57) (-776 205)`, 2761 cm: it now keeps off the priced slab and
  crosses the road at x ≈ −1007, along row 17's grown east edge (−1036.3 + 33.02), not at 258–259;
  both curb distances (95.1 u, 259.7 u) are to its start point. **Q-V13b (open):** is that east-end
  crossing unpriced road retail's volume leaves bare (the test's ×8 route then needs another
  start/end pair, a test error), or a slab the staging misses? Not settled here.
- **The unlinked-door probe chose hatches.** "Largest X·Y footprint" picked the flat
  `func_door_rotating` 1990 (hub, 196 × 122 × 15) and 152 (tutorial, 173 × 193 × 5), and passed on
  the OLD bake too, so it proves nothing. Rewritten, **unbuilt**: every standing leaf (≥ 150 cm tall,
  ≤ 30 cm thick) ≥ 300 cm from every link, with the Human mesh on both sides 40 cm past its grown
  cut, must project nothing walkable at its centre.
- **`hub_crosswalk_wait`** (`20261004T091158.533635Z`, expected-fail; moved): 11.833
  `crosswalk_south` `DontWalk`; **23.917 `male_asylum_patron` `break CROSSWALK_DONTWALK (0x13)`,
  `SCHED_TROIKA_WAIT_AT_CROSSWALK (0x102)`**, `task_pause_moving`, `task_face_next_node` (no
  `taskdone`); 40.833 `crosswalk_south` `Walk` (×4); no `break CROSSWALK_WALK` by 42.833, the
  first unmet. No `Move: waypoint passed` and no `cond+ AT_CROSSWALK` line in the trace. **Q-V13c
  (open):** why the green does not raise `CROSSWALK_WALK` (`0x102a0d20`) for the waiting walker
  (`ElysiumHint.cpp:183-203` → `SetCrosswalkWalk`): which pair it waits at is not traced. Not N13.
  `known_red` not changed (stopped).

## V13 second wave, closing (2026-10-04): committed and measured; V13 not ticked

Integrator only, no fix. One `build --arm`, 16.0 s (the rewritten door probe compiled clean); no
bake (both maps as the second wave baked them). `kernel --check` clean (7 tools).

- **`Elysium.Content.NavArea.`** (`reports/tests/20261004T091543.672196Z-…`): `PedestrianFilter`
  green; `Tutorial` green whole: door 183's link present, its span crosses cut convex 7, nothing
  walkable at (−3263, −940, −508); the rewritten unlinked-door probe finds **19 doorways with the
  mesh on both sides, every centre unwalkable**. `Hub` red on 5 assertions, each for a stated cause:
  door 2566's link present, its span crosses cut convex 52, nothing walkable at (−5481, −1440,
  −284) (green); the road probe pedestrian, the three gaps unpriced (green); **3 unlinked doorways,
  1 cut** — convex 26, `tattoodoor` (`func_door_rotating` 2105), green — and **2 still walkable
  (N21, below)**; the ×8 route's three curb assertions (258 at 95.1 u, 259 at 259.7 u, the order)
  red on **Q-V13b**, the same route as the second wave. The probe's selection rule stands: it picks
  standing leaves only, and the tutorial's 19 and the tattoo door show a cut that reaches the mesh
  answers it.
- **N21 (new) — two hub gates with no link stay walkable under their cut.** Convex 2,
  `junkyardgate` (`func_door_rotating` 1301, `*12`), box (−2418, −4829, −279)..(−2416, −4234, 63),
  and convex 22, `gasstationgate` (`func_door` 1676, `*39`, lip 64), box (−3307, −5464,
  −246)..(−2791, −5462, 64): the Human mesh area 63 on both sides and at the centre. No door link's
  span within 300 cm of either (the probe's filter), so retail's rule makes both walls to every agent
  (0018/7). Both are the hub's only unlinked leaves wider than 5 m, and both leaves stand above the
  floor: the cut keeps the brush's own floor, lowered by one cell height only
  (`RecastNavMeshGenerator.cpp:4885` `OffsetZMin = ch`; `ElysiumNavAreaActor.cpp` "The door cut is
  not this rule"), and the floor near them reads lower (`info_node` 1537 at z −262, ~21 cm above
  the ground, by the junkyard gate; `Caine` 2073 standing at z −264 by the gas-station gate, whose
  leaf floor is −246). **Lead, not proven:** a raised leaf's cut floats over the floor it should
  wall. Not measured: the floor's z under each leaf. Fix belongs with N20's door-staging work
  (pipeline), not here.
- **Default tier** 171 / 0 failed; **arm tier** 1550 / 0 failed. No test reads a broken bake.
- **Arena by name** (`20261004T091805.466210Z`): `map_hub_idle`, `map_tutorial_idle` pass;
  `hub_crosswalk_wait`, `map_tutorial_sneak_past` (V12), `patrol_monk_loop`,
  `patrol_sentry2_pingpong` (N13) expected-fail — none moved. `hub_crosswalk_wait` reproduces the
  second wave's trace line for line (11.833 DontWalk; 23.917 `break CROSSWALK_DONTWALK (0x13)`,
  `0x102`; 40.833 `crosswalk_south` `Walk` ×4; no `break CROSSWALK_WALK` by 42.833): its
  `known_red` now names **Q-V13c**, its expectations unchanged.
- **Suite** `20261004T092238.744978Z`: 106 records (V3c's `verbs_stealth_kill_scripted` is new,
  expected-fail on N11), **70 pass, 34 expected-fail, 2 fail** before the record change below. Moved:
  `hear_world_investigate` unexpected-pass → expected-fail (N4, its own `known_red`: no HEAR_WORLD by
  4.517); `interest_mode_never` pass → fail (no OnHearWorld / `cond+ HEAR_WORLD` by 3.517; heard at
  2.833 in every suite through `082753`). **Intermittent, as N4 is:** a solo boot
  (`20261004T093022`) and a three-record boot without the new record (`20261004T092952`) miss it the
  same way; run second in a boot after `hear_world_investigate` (`20261004T093237`) it hears at
  2.417 (`unexpected-pass` under the new `known_red`). **Not this wave:** the
  arena stage stands no nav-area mark, so the registration fix cannot reach it, and the idle
  cadence has moved since the baseline (`idle01` `seqfinished` 1.933 → 1.900, 2.033 → 2.017; V3c's
  closing pass — two builds and a ledger regenerate, which rewrites generated C++ — landed after
  suite `082753` and no suite ran on it; which change moved the cadence is not traced). Classified **N4 (inferred)**: the same staging and miss as `hear_world_investigate`;
  `Listen` drops a sound at its authored expiry (`ElysiumNpcSenses.cpp`, `ExpireTime < Now`) where
  retail's `CanHearSound 0x1030f7b0` has no expiry test, so a moved think tick can miss the window.
  The record's `known_red` now names N4; its expectations unchanged. Which `Listen` tick misses it
  needs H10's think event (V10). `rollcall_vzombie` fail (H11), as before. With the record change:
  70 pass, 35 expected-fail, 1 fail. No door, link-hold, custom-link or slot-531 verdict moved; no
  tutorial record moved on N20.
- **Open:** N20 (with the judge), N21, Q-V13b, Q-V13c. V13 is not ticked.

## V3d integration (2026-10-04): dialogue as a program; the arbiter deleted whole; committed

D1, D2, D3 integrated. Cross-lane lines applied: the `bInDialog` raisers in tests that meant an
`IsInDialog 0x102c1170` term now raise `m_szDialogQue +0x64ec` (`Social10`, `Dialogue` payphone arm 7,
`Damage2`, `SpeciesLifecycle10`'s no-partner arm) or `m_hDialogPartner +0xfe8` through
`SetDialogPartner 0x10107050` (`Anim`'s Silence arm, `RunAi19.BaseRunAI.DialogPartner`,
`Closure.MaintainEyeDirection`, `SpeciesLifecycle10`'s partner arm, whose `N.DialogPartner` named the
payphone's deleted member); `SaveTests` → `NpcMindOwnerRetired`; comments in `CogWindow_Npc.h`,
`EntityDebugStateTestHelpers.h`, `Motor10Tests`, `FeedingTests`, `NpcCombatTests`, `DialogueUITests`,
`NpcFootstepTests`. Ledger regenerated before build 1 (docs only, no C++), `kernel --check` clean.
Build 1 (`--arm`) 2m12s green; build 2 (test fixes) 25.1 s.

- **Tests.** Default first pass 168 / 3 failed, arm 1541 / 0. The three, each in a file no lane owned
  or in D1's: `Substrate.Dialogue.BodyScene` `:1776` — **port-only pin**, cut: it asserted the old
  dialogue think's hold on a line clip; packet R1 item 1 settles that `0x102c1400` holds no base
  (`m_Activity`, or the slot-611 sequence it commits itself once `m_bSequenceFinished`).
  `Substrate.Npc.Classes` `:992` — **corrected to retail**: Jack authors no `dialogname`, so
  `CDialog::Acquire 0x100e05f0` answers 0 before `StartTalking 0x102c0270` and `OnDialogBegin` does
  not fire (the old assertion pinned the open at the input). `Substrate.Feeding` `:402` — **test
  staging**: the same no-`dialogname` victim; the conversation is now opened at the NPC's own door
  (`StartTalking`) and closed at `0x102c0360`. After build 2: default 171 / 0, arm 1541 / 0 (the 9
  deleted: D3's 8, D1's `DialogueCamera.BodyOwnerLifecycle`). The 43 `BeginScriptedSchedule` test
  calls running the real `SetState 0x1026e340` moved nothing.
- **Arena.** By name (`20261004T100202`): the three dialogue records pass (unexpected-pass);
  `script_aischedule_walk` red at `gait`. **Record error, corrected**: the executor's
  `ScheduledMoveToGoalEntity 0x102800c0` installs `0x46` and calls `SetGoal 0x102ecd20`, whose route
  build completes the current task when it is not a continuous move (slot 529: `0x6e`, `0x0b`,
  `0x72` only) through the navigator's slot 2 (`0x102f1e2c..0x102f1e38` → `0x102623c0` →
  `TaskComplete 0x10273e80`), so `TASK_PATROL_PATH` never starts in retail either: the trace's
  `taskdone task_patrol_path` with no `task` is retail's. `gait` now expects that `taskdone`, then
  `task_wait_for_movement`; green (arrived 23.27, reselects). All four lose `known_red`. Suite
  `20261004T100815`: 106 records, 74 pass, 30 expected-fail, 1 fail (`rollcall_vzombie`, H11), 1
  unexpected-pass. Moved: the four (expected-fail → pass); `hub_crosswalk_wait` expected-fail →
  unexpected-pass (43.550 `break CROSSWALK_WALK` 2.7 s after the green, inside the V13 third wave's
  restated `within 17.0`: that record is the nav wave's, left uncommitted for its own pass);
  `interest_mode_never` fail → expected-fail (its N4 `known_red`, added by V13's closing pass). Every
  other red fails where it did (N13 ×4, N19, N11, V12, the V4/V5/V6/V7 rows).
- **The arbiter's names** (`EElysiumBodyOwner`, `FElysiumBodyOwnerToken`, `Mind.Owner`,
  `SuspendedOwner`, `AcquireSequenceBody`, `BeginDialogueBodySession`, `RouteScheduleMaintenance`,
  `ThinkInDialog`, `EndScriptedSchedule`, `"Body owner"`, and the brief's longer list): 0 uses in
  `Source/`; one V3c comment (`ElysiumNpc.cpp:544`) names the deleted `Sequence` claim beside what
  retail does instead.
- **Stand-ins left, compiling and answering** (they read the world's open session where retail reads
  the PLAYER's `+0xfe8`, which the start-dialog tail now writes at the same instants):
  `ElysiumNpcZombie.cpp:135-140`, `ElysiumNpcSounds.cpp:235`, `ElysiumNpcBaseSounds.cpp:207, 229`,
  `ElysiumCameraShots.cpp:604-616`. Production readers of `Dialogue.bInDialog || IsTalking` as
  `IsInDialog` remain (`ElysiumNpcConditions10.cpp:78`, `ElysiumNpcSounds.cpp:77`,
  `ElysiumNpcSaveRestore10.cpp:47`, `ElysiumNpcThinkCadence.cpp:82`, `ElysiumNpcZombie.cpp:113`,
  `ElysiumNpcNewscaster.cpp:237`, `ElysiumChoreoScene.cpp:687`): two of the four retail terms; not
  V3d's lanes, filed for the reader of each family (V7's inputs pass is the natural owner).
- **Unrecovered, named at their lines (filed, D1):** the scene's `+0x498` byte (`0x102c1400` step 2
  removes nothing, `DialogSceneReportsDone`); `CDialog::ShowPlayerChoices 0x100e13d0` (a seam
  answering nothing); `+0xa8` on the partner (inferred the player); the silent close
  (`ReleaseWithoutOutput`: replacement, teardown, death clear the partner without `OnDialogEnd` —
  whether retail reaches `CDialog::Release` there is unread); `0x102c0520`'s line sound and
  `scripted_scene` (only its talking half, `OnDialogFilePlayed`, is ported); the bark's early
  `OnDialogEnd` (`+0x30e9`: `Acquire` sends one line and releases); constructor / restore writes of
  `+0xfe8` (not searched). **(D2):** dormancy's state write (`Mind.Invalidate`, Idle — no retail
  writer named); `CDialog::Release` on the owner's death (`ReleaseOnDeathOrDormancy`, keyed on
  `GetOpenDialogOwner() == Handle`). Recorded in `docs/vtmb/game_runtime.md`: `0x104454c4` = 0.0f,
  `StartTalking`'s place in `Acquire` (call instruction not located: `vtmb_callers` lists none), slot
  306 = `LookAtEntity 0x1033e370`.
- **Q-V3d1 (open, V4):** with the dialogue think's hold gone, a speaking NPC's base sequence
  (`m_Activity`, the slot-611 commit) plays under the line's VCD gesture. Whether the port's body
  composes the gesture as a layer over that base (Source's choreo gesture is an overlay layer) or the
  base's clip replaces it is the animation chain's question; `BodyScene` shows the stance clip
  submitted twice during a live line (`PlayNpcClip … Stance_Neutral_Idle_1`). Not measured live.

## V13 final pass (2026-10-04): the door test by the hull's own box; V13 ticked

Integrator only; no C++ build (the test file was in the last build's binary). The tree: the staging's
door test grows a door box by the hull's own box (`map_nav_doors.hull_swept_box`, `lo − maxs` ..
`hi − mins`, `hull_table.json`), the link's clip box takes the same Z (`link_box`); `verify nav`'s
`bridging_errors` fails only ground-carrying bridges and reports jump-only ones under `jumpOnly`
(`0x102ff960` step 2: no NPC holds the jump bit, so no NPC plans one; 0018/7's decision (a) reversed
for the verdict, the key unchanged).

- **Pipeline tests.** `test_map_nav_doors.py` 14 pass, `-m corpus` 7 pass; `test_nav_acceptance.py`
  + `test_map_jump_links.py` 65 pass (one new case: a jump-only bridge reported, a ground one failed),
  `-m corpus` 3 pass. The staging old → new on both witness maps, every door row compared: only
  tutorial door 339 moves, `crossedByHulls` `[0]` → `[0, 19]`, witness link 41 for both, clip
  unchanged; partial-by-agent 3 → 2; the hub's 29 rows and link 958 identical. No other witness link
  moved (the human box now reaching 183 cm below a door picked no new one).
- **Bake** `sp_tutorial_1` only, 66 s wall: 8 door smart links over 8 doors of 36, 2 partial; 60 cut
  convexes. **`verify nav` clean on both maps**: tutorial 0 findings (link 41 no longer one;
  `bridging-hull-19` 5 bridges, 0 missing, 3 jump-only reported: 2 and 4 (path on the rat mesh), 105
  (none)); hub (not re-baked) 0 findings, 9 bridges, 3 jump-only reported, 2 pinned reproduced.
  **N20 closed.**
- **`Elysium.Content.NavArea.`** (`reports/tests/20261004T102054.796148Z-…`): `PedestrianFilter`
  green; `Tutorial` green whole (door 339 Human|Rat; 6 both / 0 human-only / 2 rat-only; door 183
  cut; 19 unlinked doorways walls); `Hub` green but 2 assertions, both N21: door 2566 cut, the road
  pedestrian, gaps unpriced, `tattoodoor` a wall; the ×8 route node 240 → node 230, 1044 cm,
  `(-2238 -1213) (-2470 -1121) (-2481 -327)`, passes curb 258 at ~25 u then 259 at ~25 u (green,
  in order); the ×1 control `(-2238 -1213) (-2265 -1121) (-2470 -418) (-2481 -327)`, 920 cm, ~97 u off
  258 (green). **Q-V13b settled (test error):** the old pair crossed at x ≈ −920, over AIN links
  1035 (481 → 236) and 1034 (236 → 480), flags `0x0` — retail's brushes leave that strip unpriced, so
  retail's route crosses there too; on the new pair only the diagonal 240 → 259 (link 1050) carries
  `0x2000`, and the priced route takes 240 → 482 → 258 → 259 → 230 as retail's would.
- **Default tier** 171 / 0. Arm tier not re-run (no C++ outside the built test file).
- **`hub_crosswalk_wait` green in two boots** (`arena/20261004T102327`, `…T102849`), the same trace:
  11.833 `crosswalk_south` `DontWalk`; 23.917 `male_asylum_patron` `break CROSSWALK_DONTWALK (0x13)`
  → `SCHED_TROIKA_WAIT_AT_CROSSWALK (0x102)`; 40.833 `crosswalk_south` `Walk`; **43.550 `break
  CROSSWALK_WALK (0x12)`** → `0x100`, `task_get_path_to_interesting_place` (goal −4135 1524 −262),
  `task_walk_path`. **Q-V13c settled (record error):** the old `within 2.0` missed by 0.717 s;
  `0x102a0d20` raises `CROSSWALK_WALK` only on a normal think, capped at 16 s out of PVS
  (`docs/vtmb/npc-ai/lifecycle.md:340-343`). `known_red` removed. **N16 closed**, the chain measured
  end to end: the marks register → the pedestrian area on the baked mesh → the priced route lays
  curbs 258, 259 → the wait at the red → the crossing on the green.
- **Arena by name, the rest:** `map_hub_idle`, `map_tutorial_idle`, `places_thug_pt1` pass;
  `map_tutorial_sneak_past` (V12, first unmet expect[3] at 8.633), `patrol_monk_loop` (N13, expect[10]
  at 37.233), `patrol_sentry2_pingpong` (N13, expect[10] at 38.783) expected-fail — every tutorial
  record identical to the V3d suite (`20261004T100815`): the re-bake moved none.
- **N21 — measured cause.** The door cut keeps the leaf's own floor lowered by one cell height
  (Human cell height 5 cm, `ElysiumNavBakeLibrary.cpp:401`; `ElysiumNavAreaActor.cpp:61-63, 77`:
  `SetIncludeAgentHeight` only for the pedestrian area), and Recast marks a span by its top. The floor
  under each gate, from the staged map rows (`sm_hub_1.dispcol` / `.hulls`, five points along each
  leaf): `junkyardgate` 1301 leaf z −279..63, cut from −284, **terrain (displacement) at −299..−294**,
  10–15 cm below the cut; `gasstationgate` 1676 leaf −246..64, cut from −251, **terrain at −285**,
  34 cm below it. Both cuts float over the ground, so nothing is marked and the Human mesh stays
  walkable under both. Retail: no AIN link crosses either gate, so no NPC plans through; a human
  (72 u tall) standing on the terrain meets either leaf. The fix is the rule this pass put in the
  staging — the hull's own box in Z — applied to the cut: the door cut includes the agent height, as
  the pedestrian area does (`SetIncludeAgentHeight(true)`, per mesh: the Human's 182.88 cm, the
  Rat's 25.4 cm — exactly `hull_table.json`'s `maxs.z`; the rat then still fits under the
  gas-station leaf's 39 cm gap and not under the junkyard's 15–20 cm, as a rat's box would). **Not
  fixed here:** it is a C++ line (the brief allows no build) and it changes every cut on every map,
  so both witness maps re-bake. **Placed:** an old bug in landed work (0018/3, 0018/7's door cut)
  that needs a build and two re-bakes → the adversarial judge, proposed as a V13 follow-up fix story
  (S: one line in `ElysiumNavAreaActor.cpp`, one build, re-bake `sm_hub_1` + `sp_tutorial_1`,
  `Elysium.Content.NavArea.Hub`'s two gate assertions turn green; check no cut then reaches a
  walkable floor < 183 cm under a door's leaf, and that `verify nav` stays clean).
- **Filed to R2, at their next bake:** the six other maps carrying slabs (`hw_hub_1` 13,
  `la_hub_1` 16, `ch_hub_1` 6, `hw_asphole_1`, `la_parkinggarage_1`, `sp_theatre` 1 each) and every
  other baked map's door cuts (registration and the hull-box door test reach a map only when it
  re-bakes; with N21's line, if it lands first).
- **Open:** N21 (the judge). V13 ticked: `verify nav` clean on both witness maps,
  `Elysium.Content.NavArea.*` green but N21's two named assertions, `hub_crosswalk_wait` green.

## V13 follow-up (2026-10-04): N21 -- every nav mark includes the agent height; closed

The judge's ruling (§ "Judge's rulings filed by the coordinator"), one agent. `UElysiumNavAreaComponent::
GetNavigationData` (`ElysiumNavAreaActor.cpp`) now calls `SetIncludeAgentHeight(true)` on every
modifier, door cuts as well as the roadway: Recast lowers each mesh's cut by that mesh's own agent
height (`RecastNavMeshGenerator.cpp:4885`, `OffsetZMin = ch + AgentHeight`) -- the Human's
182.88 cm, the Rat's 25.4 cm, each hull's `maxs.z`, so the hull-swept box in Z mesh by mesh.
`NAV_AREA_ACTOR_SHAPE` 3 → 4 (`bake_map_v2.py`) re-authored both levels. The linked-door probe
(`ElysiumNavAreaTests.cpp` `ProbeDoorCuts`) takes a list and asks tutorial door 339 beside 183.

- **Build** 23.6 s (plain). **Bake** `sp_tutorial_1` + `sm_hub_1`, 147 s wall, one bake.
- **Counts, before → after:** door-cut convexes tutorial 60 → 60, hub 54 → 54 (+ 9 pedestrian);
  door smart links tutorial 8 → 8 (8 doors of 36, 2 partial), hub 1 → 1 (link 958).
- **`verify nav`** clean on both, identical to the final pass: tutorial `bridging-hull-19` 5 bridges,
  0 missing, 3 jump-only reported; hub 9 bridges, 3 jump-only reported, 2 pinned findings reproduced;
  0 holes / walls on every ground-link check.
- **`Elysium.Content.NavArea.`** (`reports/tests/20261004T113235.495782Z-…`) green whole, 3 / 3.
  `Hub`: unlinked convex 2 (`junkyardgate` 1301) and convex 22 (`gasstationgate` 1676) **centre −1,
  sides 63 / 63 -- walls (red → green)**; convex 26 (`tattoodoor`) a wall as before; door 2566 cut
  (convex 52); road pedestrian, three gaps unpriced; ×8 route 1044 cm and ×1 control 920 cm, the
  same corners as the final pass. `Tutorial`: door 183 cut (convex 7); **door 339 cut** (convex 39,
  box z −79..196, probe at the link's z −101: nothing walkable); 19 unlinked doorways, all walls;
  links and agents unchanged.
- **Door 339, before:** the same probe run on the old bake after the build
  (`reports/tests/20261004T112841.422104Z-…`) was already green -- nothing walkable within 100 cm
  of the link's height at the cut's centre. The judge's lead (the mesh running under the closed leaf)
  is **not borne out at the probe point**; the probe now pins it.
- **Default tier** 171 / 0. **Arena** (`arena/20261004T113319.449343Z`): `hub_crosswalk_wait`
  (crossing 43.550), `map_hub_idle`, `map_tutorial_idle`, `places_thug_pt1` pass;
  `map_tutorial_sneak_past` expect[3] 8.633, `patrol_monk_loop` expect[10] 37.233,
  `patrol_sentry2_pingpong` expect[10] 38.783, expected-fail -- every record unchanged.
- `kernel --check` clean. **N21 closed.** Still filed to R2, at their next bake: the six other slab
  maps and every other baked map's door cuts and slabs (they take this rule when they re-bake).

## The fix order — acceptance lists

| story | records that must turn green |
|---|---|
| H wave (H1–H5 first) | `rollcall_vcamera`, `rollcall_vcamerasecurity`, `sense_cone_enter`, `memory_occluded_kept`, `rollcall_vzombie`; classifies `verbs_stealth_kill`; re-check `cover*` and `sense_bodies_transparent` |
| V3 arbiter, scenes, dialogue, places | ~~`cover`~~ (green at V3a), ~~the patrols ×3~~ (doubt 1 settled by V3a: not red 1 but N13, moved to V4), ~~`places_pedestrian_visit`~~ (H16 closed by the V3b follow-up; now N13, V4), ~~`places_thug_pt1`~~ (green at the V3b follow-up), ~~`input_useinteresting`~~, ~~`map_hub_idle`~~ (green at V3b), ~~`hub_crosswalk_wait`~~ (N16, moved to V13), ~~`rollcall_vhuman`, `rollcall_vhumancombatpatrol`~~ (green at V3b, record errors corrected), `script_walk_to_mark` (program retail's since V3c's tree; red on N19, the row-0 sequence), ~~`script_dialog_hold`, `input_startplayerdialogremote`, `dialog_use_hold`, `script_aischedule_walk`~~ (green at V3d); ~~`map_tutorial_sneak_past`'s first half~~ (green at the V3b follow-up; the hearing half on V12) |
| V4 animation chain, slot 363 | `sense_enemy_facing_me`, `damage_lethal_death`, `ranged_open_fire` (with N2), `melee_swing` (with N3); N13: `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath`, `places_pedestrian_visit` |
| V5 attack conditions + N1 + N2 (+ N3 if pulled here) | `range_bands`, `cover_armed`, `ranged_open_fire`, `chase_melee`, `melee_swing`; red 4 needs a record first |
| V6 lifecycle + N9 + N10 (red 5 since V3b) + N14 | `lifecycle_unhide_fights`, `rollcall_vmanbat`, `_vmercurio`, `_vsabbatleader`, `_vvampireboss`, `_vpedestrian`, `_vwerewolf`, `_vanimal`, `_vdog`, `_vscurrying`, `maker_respawn`; `save_restore_mid_path` after H8 |
| V7 inputs + N5 + N6 | `input_disablethink`, `input_changeschedule_reselect` |
| V10 (new) a sound's life | `hear_world_investigate` green in every boot (run it 3× in different boot orders) |
| V12 the footstep producer | `map_tutorial_sneak_past`'s hearing half (Q-V3bf1 read first) |
| V13 the hub's pedestrian nav area (ticked 2026-10-04, § "V13 final pass") | ~~`hub_crosswalk_wait` (N16), with `Elysium.Content.NavArea.Hub` (road probe, route)~~ (green; the Hub test red only on N21's two gates, green whole since the V13 follow-up) |

## Filed by the coordinator, 2026-10-04 (unattended run)

- **N18 `dialog_use_hold` — game red, the use focus (old bug in landed work; fixed in V3d, lane
  D1 item 10). Closed by V3d (2026-10-04)**: `FindAnchor` counts a hit on a pawn component the
  anchor's visual hangs from as that anchor's owner; `+use` reaches `PlayerUse`'s NPC arm and the
  record is green (§ "V3d integration"). Retail `PlayerUse 0x10167850` → `0x10167470` / `FindEntityFOV 0x10341c30` resolves
  the NPC itself. The port's `AElysiumMapActor::QueryPlayerUse` (`ElysiumMapActor.cpp:1312-1361,
  1429-1437`) counts any non-anchor hit as a blocker, and the NPC's own Pawn capsule
  (`ElysiumNpcBody.cpp:94-99`) blocks the use channel, so the press is refused before the
  conversation opens (inferred: no verbose log confirms which hit; the feed query already handles
  the case with `HitBelongsToCandidate`). The staging is valid against retail (Hunter1 has a
  dialogue and passes `CanTalk`; 37 units, facing) and the harness delivers `+use`
  (`verbs_stealth_kill`). Not a planning bug: V3d's own record needs it and V3d's lane owns the
  dialogue open.
- **Q-H3 settled — the last-known position is retail's** (closed by the V13 wave, § "V13 wave" above). `OnLooked 0x1026a2c0` walks the player
  list `LookForPlayers 0x1030fff0` kept from its last 0.15 s scan and writes the entity's current
  origin through slot 544 → `UpdateMemory 0x102df700` (`+0x0` and `+0xc`), so one pass after the
  teleport still writes the hidden spot. Two old bugs found by the read, fixed in the V13 wave
  (lane G2): `OnLooked` never clears the SEE family (`0x105c979c`, 6), and
  `GetLastKnownPosition 0x102dfed0` reads `+0xc` where the port returned `+0x0`
  (`ElysiumNpcBaseConditions2.cpp:328, 345`).
- **Judge's ruling, V13 (N16): implement now**, ahead of V3c. For: gate 2 and V8's hub clause
  need pedestrians to cross; `hub_crosswalk_wait` is the only live proof of `0x102a0bc0` →
  `0x102`; the content test counted convexes and never asked the baked mesh; one build and one
  ~4-minute bake of `sm_hub_1` settle the lead, rewriting gitignored files only; N13's slow walk
  still reaches the curb inside the red window (inferred). Against (did not carry): the cause is
  unproven and it is bake work outside the kernel — one bake proves or refutes it, capped at two.
  Six other maps carry the same slabs (`hw_hub_1` 13, `la_hub_1` 16, `ch_hub_1` 6, `hw_asphole_1`,
  `la_parkinggarage_1`, `sp_theatre` 1 each): R2, at their next bake.

## Judge's rulings filed by the coordinator, 2026-10-04 (unattended run, continued)

- **The nav marks' registration (N16's true cause): implement now** — every mark, pedestrian slabs
  and door cuts. For: an old bug in landed work (the marks landed 09-20 and were probably lost when
  the imports were folded into one bake session; 0018/7 was accepted on convex counts that never
  asked the mesh); 27 hub doors and 28 tutorial doors that retail treats as walls were walkable,
  which changes routes; gate 2 and V8 play these two maps. Against (did not carry): doorway routes
  run through the door links and slot 531 for the first time on a baked mesh — V8 needs that
  machinery anyway and only six records stand on the baked meshes. Landed `d75a980c`, `faaa3880`.
- **N20 (the tutorial's two Rat findings): implement now.** The door test grew the door box by the
  agent's lateral radius where retail's walk sweeps the hull's own box; replayed offline the fix
  moves one row (tutorial door 339, `[0]` → `[0, 19]`). Link 105 is jump-only for the rat and
  `0x102ff960` step 2 refuses jump links to every NPC: `verify nav` now reports jump-only bridges
  and fails only ground-carrying ones (reversing 0018/7 decision (a)). Against (did not carry): no
  step-2 record uses a tutorial rat. Landed `faaa3880`.
- **N21 (two unlinked hub gates still walkable): implement now, as a V13 follow-up first after
  T6b and before V4.** The fix is the engine flag on door cuts (`SetIncludeAgentHeight(true)` for
  every area in `ElysiumNavAreaActor.cpp`), not a pipeline change: the flag lowers each mesh's cut
  by that mesh's own agent height, which is the hull's `maxs.z` per agent — `hull_swept_box` in Z,
  mesh by mesh — while one staged convex would give one height to every agent. Measured over all
  65 doors of both maps: no cut reaches another storey; the newly reached surfaces lie under a
  floating leaf or beside a raised threshold. A lead, not measured: tutorial linked door 339's
  cut floor is ~13 cm above the floor, so the mesh may run under the closed leaf and bypass its
  link's hold — the follow-up adds 339 to the linked-door probe. Who reaches the gates: no graph
  node, place, patrol point or maker stands beyond either; nothing in the step-2 records or V8's
  idle is shown to cross one. Against (did not carry): nothing observable breaks before R2 — but
  the fix is one line, V4's acceptance runs patrol and place records on these meshes so a later
  re-bake would reopen them, and the same flaw probably sits under door 339 on the map V8 plays.
  Acceptance: both gate assertions and the 339 probe green; `verify nav` clean on both maps;
  door-cut convex counts 54 and 60, link counts 1 and 8, every other door probe unchanged; the six
  map records unchanged. Stop: a new bridging error, a lost link, a doorway flipping from wall to
  walkable, or a moved record. **Closed by the V13 follow-up** (§ "V13 follow-up"): acceptance held,
  no stop rule fired.

## Judge's rulings, V4 (2026-10-04, after packets R1 and R2; before V4a's seam)

One read-only agent, no build, no run. Sources: `stories/v4/README.md`, `packets-R1.md`,
`packets-R2.md`, the port and the pipeline as they stand. Marks: *(verified)* checked this session
in code, the staged data or the listing; *(packet)* taken from R1/R2; *(inferred)*. Each item: the
argument against the obvious recommendation, then the ruling, the cost and the proving record. No
query ran over 10 s. Brief J's item 1 (the corpse fall) is withdrawn: V4d.

### J1. N19 — a missed lookup plays the model's sequence 0: **implement now, V4a lane A2**

- **Against doing it now.** It is a cross-layer interface change (a new `IElysiumEmbodiment` query,
  `AElysiumMapActor`, the recording double) inside a lane already carrying the clock words under a
  two-build cap; only one record witnesses it; and "no re-bake" rested on an unverified per-model
  claim — `skeletal_stage/unit.py:226-229` drops empty-label, duplicate-label and
  invalid-base-cell descriptors, so a model whose first descriptor is dropped would have no row 0.
- **What the check found** *(verified)*. `rawIndex = sequenceBase + the descriptor's index in its
  own .mdl` (`importers/body_data.py:83-89`), and `first_reference_bases` counts the RAW
  declarations, dropped ones included (`:28-29`), the root first at base 0. Over the staged
  character tree (`$ELYSIUM_WORK_ROOT/import/characters`, 1,430 body tables) **none lacks a
  `rawIndex 0` row**. The runtime loads it (`Public/ElysiumBodyData.h:32`,
  `Private/ElysiumBodyData.cpp:14` → `FElysiumNpcClip::RawIndex`, `Public/Visual/ElysiumNpcClips.h:68`).
  So no pipeline change and no re-bake. Not checked: that every baked `DA_` is current with its
  staged table (the importer's `body-data` fingerprint says so; not opened).
- **Ruling.** Implement now in A2, with the row words: the embodiment accessor "this body's
  `RawIndex 0` clip" (the double answers none), the bridge's row 0 resolved from it with the clip's
  own `STUDIO_LOOPING`, rate 1.0 (`0x10090a23`), the trace naming `seq 0`. Where a body answers
  none, row 0 stays today's and logs once per model. The stale comment at
  `Visual/ElysiumAnimationResolve.cpp` ("the character export writes no raw index") is corrected by
  the same lane. An old bug in landed work: fixed without waiting.
- **Cost.** XS–S: `ElysiumNpcAnim.cpp`, `ElysiumNpcBaseAnim.cpp` (A2's already), plus the
  embodiment interface, `ElysiumMapActor` and the recording double (added to A2's file list). No
  build beyond V4a's two. **Proof:** `script_walk_to_mark` (`plays` `seq 0 rate=1`, then the
  timing after it re-measured by the A integrator; `known_red` removed).

### J2. Slot 247's bbox (README §8 Q4): **implement now — the bbox on the `UElysiumBodyData` row; the body in V4c lane C1**

- **Against the README's "leave the named seam".** R2 shows the slot live on every Troika NPC
  (`CAI_BaseNPCTroika::Spawn 0x10298d30` → `AddFlag2(4)`), so every `ResetSequenceInfo` rewrites
  `m_vecAttackExtents +0x50` — a word the bytecode's host does read: the sleep arms save it through
  slot 16 (`0x102a29fa`, `0x102a710e` → `+0x65d0`) and `TaskFail` / `OnScheduleChange` restore it,
  and the attack partition (engine `CEnumRay`, per `ElysiumMapActor.cpp:1513-1518`) is the
  collision box grown by it. The port's one live reader is the feed/use target box
  (`ElysiumNpcAccess::AttackBounds`), which today stands on `TroikaNPCInit`'s `(-1,-1,-1)` for the
  NPC's whole life where retail overwrites it at the first sequence change. "No record observes
  it" is rule 2's "out of scope", not a reason. And the costly option is not needed *(verified)*:
  `clip_data.py`'s `descriptor` drops the bbox, but `records.Seq` carries `bbmin`/`bbmax`
  (`skeletal_stage/unit.py:234`) into the same `unit.sequences` that `body_data.project_body`
  walks, and the importer stamps body data on its own fingerprint
  (`pipeline/unreal/import_characters.py:478`, `fingerprint("body-data", …, bodyDataSha256)`),
  separate from the animations' (`:227-229`, `payload` + `clipDataSha256`). A bbox on the body row
  leaves `clipDataSha256` untouched: the animation packages are reused, only the `DA_` assets
  re-author.
- **Against doing it now** (did not carry). No step-2 record observes a hit the extents decide; the
  body of `0x10090c80` is read only as far as README §1 (radial excess over the collision's maxs);
  the import still walks 1,207 entries and its reuse-path wall time is unmeasured (the README's
  "partial ~1,172 s" suggests about 20 minutes even when reused).
- **Ruling.** Implement now. Pipeline: `bboxMinCm` / `bboxMaxCm` on each `body_data.py` row (the
  `UE_` coordinate contract applies: centimetres, the Y reflection), `FElysiumBodySequence` and
  `FElysiumNpcClip` carry them; C1 ports `0x10090c80` whole against a bridge accessor
  `SequenceBounds(int32)` and fills slot 247. A body with no row answers "no descriptor" and the
  slot writes nothing, as retail with no seqdesc. The C integrator runs the one import. **Stop
  rule:** if the import reports any animation package rebuilt rather than reused, stop and file
  the data half for V4d's bake window; the body then stands on the accessor answering none.
- **Cost.** `body_data.py`, `ElysiumBodyData.{h,cpp}`, `ElysiumNpcClips.h`, `ElysiumNpcAnim.cpp`
  (accessor), `ElysiumAnimatingSlots.cpp` (slot 247); one character import, body-data only
  (~20 min, unmeasured; capped at one), inside V4c's one build. **Proof:** no existing step-2
  record observes it — stated, not hidden. Arm test `Elysium.Arm.NpcKernelAnim.AttackExtents`
  (`0x10090c80`), and A0 adds `attack_extents` to H19's `elysium_entity_get` so the C integrator
  reads it live on `ranged_open_fire`'s shooter before and after a sequence change.
  *Unrecovered:* retail's readers of slot 16 beyond the two sleep saves (the corpus lookup
  resolved the address to `client.dll`; not pursued).

### J3. The player's anim-event dispatch (K3): **implement now in V4a — the dispatch site and order (new lane A4); the player's own sequence clock is filed to the player story (0015, layer 0)**

- **Against "it needs a player story".** The owner ruled the poll goes for every entity. What
  retail's order needs is small and local *(verified)*: the world tick already has the point that
  stands for `PostThink` — after `SyncFromBody` / `TickStepClock`, before `RunThinks`
  (`ElysiumEntityWorld.cpp`, `AdvanceAnimEvents()` then `TickStealthKill()`), and the player is the
  first entity simulated in retail's frame. A player-owned `PostThink` step there — dispatch (base,
  then the layers: slot 258 is the overlay body), then the slot-312 stand-in (`TickStealthKill`,
  the melee sweep) — is retail's order `0x1016be10`: advance → slot 258 → slot 312. The poll
  already walks Base and UpperBody for the player (`ElysiumAnimatingImpl.cpp`
  `GPolledEventChannels`), so the layer's 3031 is reachable today.
- **Against doing it all now.** The player has no kernel sequence words (R2): its cycle is the pose
  layer's phase. Giving it `m_nSequence` / `m_flCycle` / `StudioFrameAdvance` means `SetAnimation
  0x10164240` and the action classifier on the kernel — a player story, not a seam. And the
  retail window (0.1 s look-ahead, `+0x658`) moves every player shot's commit earlier: the weapon
  tests and `CommitArrivesFromAnimEvent` (`ElysiumWeaponClasses.cpp:1259-1282`), the feed timeline
  (`ElysiumFeed.cpp:1158-1164`) depend on the poll's cursor.
- **Ruling.** Split. **Now (V4a):** A1 writes the base dispatcher `0x10091880` and the per-layer
  body `0x10098cd0` as bodies over a small words struct (sequence, cycle, cycle rate × playback
  rate, loops, last-event-check, finished, past-half) so one body serves the NPC, the player and
  the camera; A1 deletes `FElysiumEntityWorld::AdvanceAnimEvents` and
  `FElysiumAnimating::AdvanceAnimEvents` whole and leaves one call at the same place in the tick,
  `FElysiumPlayer::PostThinkAnimation()`. **New lane A4** (files disjoint from A1's:
  `ElysiumPlayerEntity.cpp`, `ElysiumGrapple.cpp`'s call site, `ElysiumCameraAnimated.cpp`) fills
  it: the player's words per channel, the cycle **read from the pose layer's phase** — a named
  seam, "stands for `m_flCycle` until the player's `StudioFrameAdvance` is ported (0015 layer 0)" —
  the retail window and look-ahead, the handler gate of `0x10178a10`, then the slot-312 stand-in.
  **Filed:** the player's kernel sequence clock, owner 0015 (the player entity's layer 0). The
  cycle source is the existing K2-shaped residue, now named at its line; it is listed for the
  owner, not adopted as new.
- **Cost.** One more coder in V4a (6 agents; rule 8's cap is the coordinator's to check), no extra
  build; a moved player-shot timing is triaged under README §8 Q5, never loosened. **Proof:**
  `anim_player_footsteps` — `animevent` 2050 then 2051 `who: player` while walking (dispatched;
  the handler swallows them, the sound stays the step clock's), `never animevent` standing; the
  cycles from the PLAYER model's baked table (R2 cited the cast's banks: A0 reads the player's).
  `anim_player_weapon_event` — two records: firearm, `animevent 3031` on the player before the
  `damage` it causes; melee, **`never animevent` in 3000..0xfa2** with the `damage` still landing
  (the sweep is slot 312's). 4050/4051 belong to the stealth-kill clips, not these records.

### J4. Props and the camera: **the prop poll is deleted with nothing in its place (A1); the camera's dispatch is implemented now (lane A4)**

- **Props — against a plain delete.** The owner asked for a record with an event that fires; a
  `never` on a prop whose clips author nothing passes before and after and proves nothing. And
  deleting the poll removes the only caller of the prop handler default (`ElysiumEntity.h`
  `HandleAnimEvent`), so anything that was quietly riding it goes silent.
- **Ruling.** Retail is the default: `CDynamicProp`'s think `0x10190850` advances and never
  dispatches, and slot 258 has four call sites in the whole image, none a prop (R2, *(packet)*).
  The port's poll for props is deleted, no replacement, no seam — there is no retail input to
  stand for. **The record states the negative and must not be vacuous:** `anim_prop_event`, in the
  Green Room, a hand-written `prop_dynamic` on the one non-character model whose clip table
  authors an event — `models/items/walkie_talkie/walkie_talkie.mdl`, sequence
  `Crooked_Cop_Walkie_Talkie_Into` (4100 @ 0.225) — started by `SetAnimation`; `expect` the
  sequence plays (the prop animates), **`never animevent`** on it. With H21's tap on the poll it is
  red today and green once the poll is gone. `about` cites `0x10190850` and says no retail map row
  exists to copy (R2 (i)). If that model has no baked prop body, A0 says so and writes the record
  on any animating scenery prop with `notes` stating it is vacuous by data (284 scenery, 19 gibs,
  10 editor, 2 worldcraft and the cinematic lane author zero events).
- **Camera — against doing it now.** It needs the camera's own cycle words, and its end moves 0.1 s
  of clip time earlier (the look-ahead finish), which shifts `OnCameraComplete` on every cutscene.
- **Ruling.** That shift is retail's (`0x10071840`: advance → dispatch → the finish test on
  `m_bSequenceFinished`), and today's `Think` already says the advance and dispatch "are the
  world's own animation pass" (`ElysiumCameraAnimated.cpp`, the comment above `IsSequenceFinished`)
  — deleting the poll without this would leave the camera with no events at all. Lane A4, in
  `FElysiumCameraAnimated::Think`: the words (cycle from the sequence's start and length), A1's
  base dispatcher (no layers), handler `CBaseAnimating::HandleAnimEvent 0x10091da0` (2070, 2071,
  4005), the finish from the dispatcher's flag replacing the time compare.
- **Cost.** Inside A1 and A4; no build beyond V4a's. **Proof:** props — `anim_prop_event`. Camera —
  arm test `Elysium.Arm.CameraAnimated.ThinkOrder` (`0x10071840`); no step-2 arena record stages
  an animated camera (not searched beyond `Arena/scenarios` names) — the A integrator names one if
  it exists.

### J5. The move-and-shoot overlay `0x102e8560`: **stub now (A1's layer seam and the existing counter, named); a red record now; the overlay stack filed to 0015, the wire to 0002 step 3 (R3)**

*V4o ported it (2026-10-04): the overlay `0x102e8560`, the four layers and the event shot are landed; `cover_move_shoot` is the record.*

- **Against the stub.** It is reachable on step-2 paths: `cover`, `cover_armed`, `cover_reclaim`
  and `range_bands` run a gunman to cover or to a line of fire, and with `COND 0x4f`, flags2
  `0x400`, a `0x6000` weapon and `CAP_MOVE_SHOOT` slot 575 passes and retail fires from the
  layer's 3031 while running. A shot lands damage and raises sound conditions on the player's
  side: the bytecode can observe it. `cover` is green today against a port that never fires on
  the move, so that green is not retail's. Rule 2: "0015's" is not a divergence.
- **Against implementing now** (carried). The port's `RunTaskOverlay` is a counter
  (`ElysiumNpcBaseMaintain.cpp`, `++MoveAndShootOverlay.UpdateCalls`) *(verified)*; the body needs
  `AddGesture 0x100991b0`, the four `CAnimationLayer`s with their own cycle advance and weights,
  `Weapon_SetActivity`, the `0x47`/`0x48` gestures and slot 575's whole gate — the NPC overlay
  stack, which no V4 lane owns and which is M on its own. None of the four records asserts a shot
  on the move (grep: no `animevent` expectation in any cover record) *(verified)*.
- **Ruling.** Stub: A1's four-layer seam answers "no layer", named for `CAnimationLayer`
  (`+0x734`, stride `0x30`) and for `0x102e8560` as its only step-2 pusher; the counter stays.
  **Bug protocol step 2 now:** A0 writes `combat/cover_move_shoot.json`, red — the `cover_armed`
  staging with the player in view during the run; `expect` `animevent 3031` on the gunman between
  the move task's start and its `taskdone`; `known_red` "0015 (the NPC overlay stack); the wire
  0002 R3". **Step 4:** this is a planning correction, stated to the owner here: step 2's `cover`
  family cannot prove the run-and-gun; gate 2's claim for cover is re-cut to "reaches cover and
  fires from it", and R3 (cover and the goal selectors) takes the overlay's wire with 0015's stack
  as its dependency. *Unrecovered:* slot 273's 23 callers (R2).
- **Cost now.** One record, no code beyond A1's seam comment. **Proof:** `cover_move_shoot` (red).

### J6. `ContactEventCycle` on the NPC path: **implement now (remove), V4c lane C1 — for the bodies that were read; a named guard for the ones that were not**

- **Against removing it.** R2 did not read the species weapon bodies (`0x103ed200`, the
  flamethrower, thrown). A species whose attack clip authors no 3030..3044 and whose retail fire
  comes from a body nobody read would go silent with no record to notice; three NPC bodies do call
  a weapon's slot 326 directly (the Troika melee arm, `CNPC_VBach::StartTask 0x103645a0`,
  `CNPC_VManBat::RunTask 0x1038d130`).
- **Ruling.** Remove the estimate on the NPC path for `CWeaponRanged` (`0x10238160` →
  `0x10238320` → `ModeDispatch(1)` → `Shot 0x102387b0`): no retail path fires a Troika human's
  shot without the event *(packet, L)*. **The guard, three parts:** (a) the removal is keyed on
  the operator body, not on "is an NPC" — an operator body not in R2's read set keeps today's
  estimate behind a seam named for that body's address and "unread", so nothing read as retail
  changes for it; (b) README §8 Q6's line, at Warning, once per (model, sequence): an NPC attack
  clip with no fire event; (c) the C integrator lists, from the baked event tables, every NPC
  class whose ranged attack activity has no 3030..3044 clip — that list is the reading owed, filed
  to V5 (the ranged attack's story) with the three slot-326 callers. The player's use of the
  estimate (`ElysiumWeaponClasses.cpp`, "its commit stays on the estimate") is not touched here:
  it goes with the player's weapon events (J3's A4 proves 3031 arrives; removal is the player
  story's).
- **Cost.** C1's files as briefed; `Tests/ElysiumWeaponTests.cpp`'s estimate assertions on an NPC
  wielder deleted (they pin a port mechanism). **Proof:** `ranged_open_fire` — `shot_event`
  corrected to match `3031` (J7), plus `never` a `damage` on the player before it.

### J7. Record errors found by the readers: **confirmed as record errors (bug protocol step 1); corrected by the seam agent A0, each with its retail source**

- **Against.** A record corrected while its behaviour is red looks like moving the goalposts
  (README §8 Q5: "never loosened"), and R1's speed is one body measured on one record.
- **Ruling.** They are record errors: a record states what retail does, and these state something
  retail does not do.
  - `melee_swing` *(verified in the record)*: `hit_event` (`animevent`, `match ""`) and the
    `about`'s "the clip's hit event reaches HandleAnimEvent" — no attack clip in the male
    `baseball` bank authors an event (R2 3c); contact is `MeleeSwingUpdate 0x10346cd0` from slot
    312. Replace `hit_event` with a `damage` on the player after `task_melee_attack1` (the kind
    exists: `input_takedamage`), add `never animevent` on the brawler between swing and `taskdone`,
    rewrite the `about` and the `known_red`'s tail ("the hit event rides the world-tick poll").
    Still red on N3 (V11).
  - `ranged_open_fire`: `shot_event` matches `""`; it must match `3031`.
  - N13's four `known_red` texts (`patrol_sentry2_pingpong`, `patrol_monk_loop`,
    `input_clearpatrolpath` *(verified: all three name 136.7 and "0.44×")*,
    `places_pedestrian_visit`) and H18's acceptance: the speed is **each body's own `walk_0`** —
    sentry2 (`vampire_hunter_chick`, the female bank) 101.278 cm/s; 136.683 is the male cell. The
    cause text becomes R1's (the arrival, J9), marked inferred until toggled.
  - **Limit on A0:** a deadline is changed only where it was computed from 136.7 for a body on
    another bank, the arithmetic shown in `about`; `patrol_monk_loop`'s and the pedestrian's banks
    are checked first (R1 did not). No other expectation moves.
- **Cost.** Record edits only, in A0's commit. **Proof:** the records themselves parse and stay
  `expected-fail` on their real causes.

### J8. The doc conflicts: **each fixed by the lane that must code from it, before it codes**

- **Against.** Coders touch only their brief's files; a doc rewritten on one fresh read can trade
  one error for another.
- `docs/vtmb/npc-ai/lifecycle.md` § "The ordered chain" steps 5–6 — **lane C2**. Partly checked
  here *(verified)*: `CreateCorpse 0x1032c0e0` at `0x1032c404` pushes `(0x10009c9b, 0, 0)` into the
  think setter and then takes `curtime + _DAT_1044e664` (10.0), so an arm of it does replace the
  think; that it is *every* ordinary arm (the branch at `0x1032c400` skips it) was not walked. C2
  owns the `ACT_DIERAGDOLL` seed, which lives on exactly the path in question (state 7's
  `BecomeClientRagdoll(vec3_origin, -1, 0)`, `0x1028a8ec`): it walks the arms first, rewrites
  steps 5–6 and § "A death sounds more than once" (its count depends on the second
  `Event_Killed`), and ports the seed only as the arm retail reaches. If an ordinary kill never
  reaches `SCHED_DIE`, the seed's arm test states who does reach it.
- `docs/vtmb/player-entity.md` § "Recovered `PostThink` body" (`-> Weapon_FrameUpdate`, `:450`
  *(verified)*; the call is slot 312 `UpdateCharacter`, `Weapon_FrameUpdate 0x1032aa40` has one
  caller, `PostRun`) — **lane A4**, which codes that order.
- README §1's `RunAnimation` gate (`m_Activity +0xfec == 1`; the pick on
  `m_TranslatedActivity +0xff4`; and R2's other four corrections) — **the coordinator, before A0**:
  the briefs quote README §1, and a coder must not find the correction only in a packet.
  `shape.md` § "The activity commit" already holds it (R2).
- **Cost.** Doc edits; no build.

### J9. R1's arrival finding: **B1's change stands under K1 only as the whole retail stop; switching the slowdown off alone is a new divergence, recorded and not adopted. The toggle is measured first.**

- **Against "it is just K1".** K1 keeps the motor on the body's tick *with retail's contract*.
  Retail's contract for the stop is `0x102630b0`: ideal speed from slot 248 (50.0 when it answers
  0), acceleration = ideal + `_DAT_104493c0`, each waypoint's speed `ideal × clamp(dot(in, out) +
  _DAT_10449198, 0, 1)`, **the last waypoint's 0**, limited forward and backward by constant
  acceleration; the step is the trapezoid `(|v| + new) × interval × 0.5`. Turning Detour's
  slowdown off gives a body that arrives at full speed and stops dead — not the crawl, and not
  retail either: the `arrived` instant, the footstep events of the last metre and the corner
  speeds all differ. And the three constants are unread (R1), so the profile cannot be ported yet.
  The cause itself is inferred from code, never toggled *(verified: no `SetCrowdSlowdownAtGoal`
  call exists in `Source/`; `FollowerArrivalFloorCm = 1.0`, `Visual/ElysiumNpcBody.h`)*.
- **Ruling.** (1) Not a new divergence **if** B1 ports the velocity script's profile as the body's
  commanded speed (K1's "retail's input" extended from the cruise speed to the whole speed
  curve) and the slowdown goes off because retail's deceleration replaces it. (2) Slowdown off
  with no retail deceleration is a new divergence: recorded, not adopted. (3) Before B1 is final,
  a short read (R1b, no build) recovers `_DAT_104493c0`, `_DAT_10449198`, `_DAT_10457f60` and the
  retail arrival tolerance against the port's 1 cm follower floor (whether that floor is retail's
  is unverified). (4) The B integrator's first build measures the toggle on
  `input_clearpatrolpath` (R1's table: `arrived` at 10.428 against ~5.6 s of travel) before
  anything else is trusted; if the creep survives the toggle, stop — the cause is unread again and
  B1 does not land on a guess.
- **The 13.4 s `task_face_enemy`.** R1 refuted README §2 M4's cause (the turn ladder is closed for
  a human, `0x2000` is never tagged, the bound is 0.5 s) and left the port's cause unread. It must
  be found **before B2 is final, by measurement, not by the next coder**: A0 writes
  `face_enemy_turn` red and adds to H19 the three words that decide it (the `MaxYawSpeed` answer,
  `m_afMemory`, `m_Activity` during the task), runs it on the seam build and reports the trace;
  R1b names the arm that reaches the 1.0 floor from that trace. Note for A2: with `+0x560` real it
  is still 0 on shipped data (`GetSequenceTurnYaw` answers 0, R1 §2), so V4a does not fix this by
  itself, whatever README §4 says.
- **Cost.** R1b: one reader, no build. B1/B2 unchanged in files. **Proof:** `input_clearpatrolpath`
  and the three other N13 records (each on its body's own `walk_0`, J7); `face_enemy_turn`
  (`taskdone` within 0.5 s).

### Briefs to amend (the coordinator, before the lanes start)

- **README §1 / §4 / §7**: R2's five corrections; K3's text (props never dispatch; the player and
  camera on A4); V4a gains lane A4; the cut's agent count.
- **A0**: `anim_prop_event` as a non-vacuous `never` (J4); the two player records as J3;
  `cover_move_shoot` red (J5); `melee_swing`, `ranged_open_fire`, the N13 texts and H18's
  acceptance (J7); H19 gains `attack_extents` (J2) and the three yaw words (J9); `packets.md` →
  `packets-R1.md` / `packets-R2.md`.
- **A1**: the dispatcher over a words struct shared by NPC, player and camera; the poll deleted
  whole, one `PostThinkAnimation()` call left in the tick (J3, J4); the layer seam named for
  `0x102e8560` (J5).
- **A2**: N19's row 0 and the embodiment accessor, three files added (J1); `+0x560` is 0 on
  shipped data (J9).
- **A4 (new)**: the player's `PostThink` dispatch and slot-312 order, the camera's think,
  `player-entity.md` (J3, J4, J8).
- **B1**: the velocity script's profile, not the toggle alone; final after R1b and the measured
  toggle (J9). **B2**: final after `face_enemy_turn`'s cause is named (J9).
- **C1**: slot 247 whole against `SequenceBounds` (J2); the estimate removed per operator body
  with the three-part guard (J6). **C2**: walk `CreateCorpse`'s arms and fix `lifecycle.md` before
  the seed; `RunAnimation`'s gate as R2 read it (J8).
- **C integrator**: the body-data import with its stop rule (J2); the silent-class list (J6).
- **D**: none.

## Judge's rulings, V4 — second sitting (2026-10-04, after packets S1–S3; under "testable first" and "settle first")

One read-only agent, no build, no run. The reads are in `stories/v4/packets-S4.md` (items a–f);
marks as there: *(verified)* this session in the listing, the DLL's bytes, the staged manifest or
the port; *(packet)* S1–S3; *(inferred)*. Each item: the strongest argument against the ruling
first, then the ruling, the cost, the proving record. No query ran over 10 s.

### J2b. Slot 247's bbox, re-ruled: **the body now (C1), on a named seam; the data half and its import filed with the story that ports the player's acquire cone. J2's import is withdrawn.**

- **Against (for keeping J2).** Rule 2 is strict: retail rewrites `m_vecAttackExtents +0x50` at
  every sequence change on every Troika NPC, the port leaves `TroikaNPCInit`'s `(−1, −1, −1)` for
  the NPC's life, retail *can* be followed (the bbox is in `records.Seq`), and the price is one
  ~20-minute import. "Nobody reads it" is the argument J2 already refused.
- **What changed** *(verified / packet)*. J2 rested on the engine's `CEnumRay` growing the box for
  the attack partition. S2 item 7 and S4 item c close the list: the engine copy is read only by
  queries carrying Troika's flag, and those are the player's acquire cone (`0x1040f550`,
  `0x1040f080`: the pick-up cone of `physics-interaction.md` § "The acquire chain") — two sites;
  `0x102fb4e0` passes 0. Game code reads `+0x50` only to save and restore it (three sites). **The
  acquire cone is not ported**: no line under `Source/` cites either address, and no `spec.md`
  names that document. So there is no observer in the port: no record, no arm test on real data,
  no live read can tell a right value from a wrong one. "Testable first" then decides: work that
  cannot close on a real test does not run now, and the thing that would make it testable (the
  acquire cone) is the player's, not pullable into V4.
- **Ruling.** Split. **Now, C1**: slot 247's body `0x10090c80` whole, against `SequenceBounds`,
  which stays a **named seam** answering "no descriptor" — "stands for the seqdesc bbox
  `+0x1c..+0x30`; filled when the acquire cone is ported". With no descriptor the slot writes
  nothing, as retail with no seqdesc. This is CLAUDE.md's seam for an input with no source, not a
  divergence. **Filed**: `bboxMinCm` / `bboxMaxCm` on the body row, the runtime fields and the one
  import — owner: the story that ports `0x1040f550` / `0x1040f080`. No spec owns it today; the
  coordinator names one (0015 is the player's nearest). **Also filed, same owner**: the port's
  feed-target box grows by the extents citing `CEnumRay` (`ElysiumMapActor.cpp:1513-1518`), which
  retail does only for a flagged query *(inferred: `FindEntityFOV 0x10341c30` is not one of the
  two)*; with the seam it moves the box by one unit. Not changed now.
- **Cost.** None beyond C1's code item; no pipeline run in V4c. **Proof:** arm test
  `Elysium.Arm.NpcKernelAnim.AttackExtents` on the `SequenceBounds` double. No arena record, and
  that is stated: retail gives the bytecode nothing to observe here.

### J10. `0x102a11d0`, "a non-hated NPC blocks the swept slab": **implement now, V11 lane V11-1**

- **Against.** V11-1's brief lists it as "not yours (R3 / R4)"; it touches the selectors of four
  species; the seam's `false` is the right answer in every record that stages one brawler, so
  nothing red asks for it; and the packet said it "needs an entity sweep against NPC bodies".
- **What the check found** *(verified)*. The sweep exists: `IElysiumEmbodiment::TraceRetail` takes
  a box, a retail mask and an ignore list and returns the character bodies it met, nearest first,
  when the mask carries `MONSTER 0x2000000` (`ElysiumWorldServices.h:703-759`); the MONSTER-alone
  recipe is already there for the idle gate `0x102b5de0` (`ElysiumRetailMaskRecipe.cpp:33-38`). The
  seam is one function returning `false` (`ElysiumNpcSchedule.cpp:583-588`), three call sites. And
  it is on a witness map: the tutorial warehouse stands `thug_2` (bat) and `thug_3` in one squad,
  and V11 is the coordinator's story — with two attackers this gate is what sends the second one
  to circle (`0xe1`) instead of advancing (`0xcb`).
- **Ruling.** Implement now in V11-1, as S2 item 6 states it: hull `(2·mins.x, 2·mins.y, −6)` /
  `(2·maxs.x, 2·maxs.y, +6)`, from `WorldSpaceCenter` to the point, mask `0x2000000`, ignoring
  self; true when the nearest body met is an NPC and `IRelationType(it) != D_HT`. The player's
  body ends the trace and answers false (no NPC pointer). Renamed (it is not a reach test).
- **Cost.** S: `ElysiumNpcSchedule.cpp` (that function; V11-1's list has the file for another
  function), `ElysiumNpcSchedule.inl`, the three call sites' name (`ElysiumNpcHuman.cpp:480`,
  `ElysiumNpcMingXiao.cpp:366`, `ElysiumNpcSchedule.cpp:388`); inside V11's build. **Proof:** new
  record `melee_ally_in_the_way` — two bat-only combatants hostile to the player and neutral to
  each other, in line; the rear one `expect` `0xe1`, `never` `0xcb` while the front one stands
  between; plus an arm test on the trace double.

### J11. The four species with task-code fire paths: **verified absent from both maps; each filed on the spec's "on demand" line, no stub needed**

- **Against filing.** "No unknown is left for later", and J6 removes the estimate for every NPC
  wielder: a species whose port body leaned on it goes silent with no record to notice.
- **What the check found** *(verified)*. No `npc_VChangBros`, `npc_VFrenzyShadow`, `npc_VBach` or
  `npc_VManBat` row in the staged entity data of `sp_tutorial_1` or `sm_hub_1`, no maker names one
  (`NPCType` is `npc_VCop`, `npc_VHuman`, `npc_VRat`, `npc_VVampire`), and the reach tables carry
  no such `class` row. These are not unknowns: S2 item 1 read where each fires.
- **Ruling.** Filed, owner the spec's "on demand, not in the sequence" line ("the species outside
  the six maps' reach"), one row each, to run when a map that reaches the class enters scope:
  ChangBros — `RunTask 0x1036bfc0` → `SpawnEnergyBall 0x1036dd20`; FrenzyShadow — `StartTask
  0x10375f50`; Bach — `StartTask 0x103645a0` (weapon slot 326), the camper pass `0x10365a90`
  already ported; ManBat — `RunTask 0x1038d130` (slot 326). Each row's first step: show the port's
  task body calls the weapon's fire and not the estimate. J6's Warning line (an NPC attack clip
  with no fire event, once per model and sequence) stays as the tripwire.
- **Cost.** Four lines in the spec. **Proof:** none in step 2; each row names its record when run.

### J12. The NPC clip: **ordinary Shot never spends; fake reload after template sets; V5b ports real reload finish**

- **Against.** The spend sits in the commit the player shares; touching it risks the player's
  ammunition and the weapon tests; "NPCs never run dry" reads like a cheat; and a gunman reloading
  is something players remember.
- **What the read found** *(verified, S4 item a)*. Retail fills the NPC's clip once, at equip
  (`Inventory_Insert 0x10334e70`: `max(Default_Size, 1)`), and `Shot 0x102387b0` subtracts only in
  the player block. The port subtracts `Ammo_Cost` for every wielder and refuses the shot when the
  magazine does not cover it (`ElysiumWeaponClasses.cpp:2313-2323`), and its `TASK_RELOAD` finish
  fills nothing while clearing `0x40` (`ElysiumNpcBaseRunTask.cpp:259-264`, `:598`). A port gunman
  with the .38 (`Size 6`) fires six times, raises `NO_PRIMARY_AMMO`, reloads to no effect and
  loops; retail's keeps firing. That is on step-2 paths (`ranged_open_fire`'s shooter, `cover_armed`,
  the tutorial's `thug_3`), and it changes which programs run: the bytecode observes it.
- **Ruling.** Implement now in O3, which ports the NPC's `Shot`: for a non-player wielder the sets
  are capped by the clip (`sets = clip / Ammo_Cost` when short) and the clip is not written; the
  player's branch is untouched. Equip: the NPC's clip is `max(Default_Size, 1)`. **The reload**:
  `WeaponFinishReload` stays a seam (unreachable once the spend is gone), its comment replaced by
  the body read — `m_bInReload = 1`; slot 322 `0x10255050`; single-round weapons: player only;
  bulk: `clip += min(Size − clip, owner's reserve)`, reserve untouched, both next-attack words
  `= curtime` — owner V5b. Any V5 record that expects a reload after emptying a gun is a record
  error.
- **V5b close.** The reload seam described in the historical ruling is now ported:
  TASK_RELOAD sets bInReload before slots322/323; bulk completion reads the live
  deadline and leaves NPC reserve unchanged. Ordinary Shot still cannot raise
  NO_PRIMARY_AMMO through clip spend; TutorialThug fake-reloads after six sets.
  Presence belongs to spec0006, retained single-round bInReload to V6. The owner's
  flamethrower witness uses the explicit no-bake fallback: standing attack event
  absent; complete class/model/sequence/Attack-entry evidence is recorded below.
- **Cost.** XS in O3's file; weapon tests that pin an NPC wielder's spend are deleted (they pin a
  port mechanism). **Proof:** new record `ranged_sustained_fire` — the `ranged_open_fire` staging,
  `expect` eight `animevent 3031` on the shooter (two more than `Size`), `never` `cond+
  NO_PRIMARY_AMMO (0x40)`, `never` `task_reload`.

### J13. The corpse's removal, by body: **all four clocks are state, so all four are settled inside V4 — C2 the thinks, D the body that must survive them; only the burn's look is filed**

- **Against.** V4d is the visual half of a modernization; removal timing is lifecycle (V6); the
  Kindred, pedestrian and fade arms multiply V4d's trace checks by four for bodies its spike never
  staged.
- **Ruling.** An entity that is gone answers no name lookup and fires no output: the bytecode
  observes it, so it is not V4d's to modernize and not V6's to wait for.
  - **Ordinary mortal** — `SUB_PVSRemove`, re-armed every 10 s while the player's cone, PVS and
    `FVisible` all pass. Ported *(verified, `ElysiumNpc.cpp:597-611`)*. Record now (C integrator):
    `corpse_removed_unseen` — the `damage_lethal_death` staging with the player turned away;
    the entity is gone at +10 s; a second run facing the corpse, still there at +12 s.
  - **Kindred or `Has_Burning_Death`** — `SUB_Remove` at +10 s, seen or not. The think is ported
    *(verified, `ElysiumCombatCharacter.cpp:1518-1527`)*. Record now: `corpse_kindred_burns` — a
    tutorial `npc_VVampire` row by `from_map`; gone at +10 s while watched. **V4d lane D**: a
    simulating body removed at +10 s must not ensure, and the burning-death sound is emitted (one
    call). **Filed to 0014**: `BurnModel`'s look (render fx `0x1b`, the per-bone particles, the
    skeleton `dynamic_prop` for 3.5 s) — a named seam at the burn arm; nothing reads it.
  - **Pedestrian** — no think at all, `SOLID_NONE` (S4 item e). **V4c lane C2, item 9, now**, with
    one addition: the port's `Think` runs `SUB_PVSRemove` on any committed death whatever the
    think name (`ElysiumNpc.cpp:645`), so wiring slot 301 alone changes nothing — the cleared think
    must win there (`ElysiumNpc.cpp` joins C2's files for that test only). Record:
    `corpse_pedestrian_stays` — a hub pedestrian by `from_map`, the player turned away; still
    present at +25 s.
  - **Fade** — see J14.1: implemented now in C2. Record: `corpse_fades` — a child of the tutorial's
    `stealth_victim_maker` by `from_map`; present at +13 s, gone by +14.5 s, watched.
  - If the arena has no event kind for an entity's removal, A0's harness adds one (`removed`); the
    fallback is H19's `elysium_entity_get` answering none.
- **Cost.** Four small records and one harness kind; C2 grows by the fade (J14.1); D gains two
  lines of contract. No extra build.

### J14. Plan-shape changes found by the reads

1. **Fade corpses are on both witness maps — a new game bug in landed work; implement now, V4c
   lane C2.** *(verified in the manifest, the docs and the port)* 25 of the 62 makers
   (`Flag_InfChild 1` on 22, `Flag_Fade 1` on 3) set `m_bFade`, and `MakeNPC 0x1034b7b0` then
   gives the child spawnflags `0x204`; the tutorial's stealth victim and guard are two of them.
   `Event_Killed` calls `SUB_StartFadeOut 0x102695d0` after slot 301 for such a body; the port's
   `StartFadeOut` is a counted seam reached only from `TASK_DIE`. **Against:** it is a lifecycle
   body in a lane already carrying five items. **Ruling:** now — `verbs_stealth_kill` kills
   exactly such a child, and J13's fourth clock cannot be tested without it. C2 ports the call in
   the port's `Event_Killed` (after `CreateCorpse`), `SUB_StartFadeOut` and `SUB_FadeOut
   0x10269960` (−7 alpha per 0.1 s, then `SUB_Remove` after 0.2 s), and checks that the maker
   passes `0x204`; if it does not, that one line is pulled from R6 into C2. A Kindred child both
   burns (look and sound at death) and fades (removal at about +13.8 s, not +10 s): the later
   think wins. Cost S. Proof: `corpse_fades`.
2. **`NO_PRIMARY_AMMO` cannot rise from firing** (J12). `SCHED_TROIKA_TAKE_COVER_NO_AMMO` drops
   out of V4o's list of live `0x400` writers; V5b loses any "empties the gun, then reloads"
   expectation for ordinary Shot. The owner's flamethrower exception remains: Attack
   `0x103e2f30` spends on NPC owners too; V5b found no standing shot event in the
   baked `flamet_attack` timeline, so its authorized no-bake fallback is arm-only.
3. **Who can run-and-gun** (S4 item d): nine placed rows on the tutorial, none on the hub with a
   ranged primary. V8's hub clause cannot witness the overlay; `cover_move_shoot` and the tutorial
   are the proofs. No order change.
4. **The melee band is settled and uncited in the port.** Constants 0.7 / 0.5 / 1.2 / 256.0 /
   0.25; the far word is `9` beyond `max(1.2 × reach, 256)` and `0x60` between. No line under
   `Private/Substrate` cites `0x103ea7e0`. **V5a-1** (it rewrites the attack gather) states which
   port body stands for the melee weapon's band and ports it whole if none does; V11's
   `chase_melee` expectations (`0xcb` / `0xd2` on `9` or `0x60`) depend on it.
5. **V11 item 11** is answerable from data: the three tables are the inventory sections (Melee,
   Ranged, Thrown, Hidden; 32 slots each). The seam stays only if the port's inventory has no
   section order; V11-1 reports which.
6. **V4b B1** takes the turn script's two helpers from S4 f.1 (`UTIL_ApproachAngle(target, value,
   speed)`; the 0.01 s / 0.8 insert rule of `0x10262c20`).
7. **Still open, each with its owner** (S4 f.2): slot 331's walk (V11-1 reads the doc section
   first); the status that doubles the attack rate (O3: a seam answering false); the section
   order file (V11-1).

### Briefs to amend (the coordinator, before the lanes start)

- **C1**: item 5 is closed, not pending — the body on the `SequenceBounds` seam, no pipeline, no
  import (J2b). **C integrator**: the body-data import is struck; four corpse records (J13).
- **C2**: the fade and the maker's `0x204` (J14.1); item 9 with the `Think` gate, `ElysiumNpc.cpp`
  added for it (J13).
- **D**: the contract row becomes four removal clocks; survive a +10 s removal; the burn sound;
  `BurnModel`'s look filed to 0014 (J13).
- **O3**: no spend and no refusal on the NPC's clip, equip gives `max(Default_Size, 1)`, the
  doubled-rate seam; record `ranged_sustained_fire` (J12). **`v4o/README.md`**: two live `0x400`
  writers, not three; the nine tutorial rows (J14.2, J14.3).
- **V11-1**: `0x102a11d0` is yours, with its files and the record `melee_ally_in_the_way` (J10);
  item 11 from the inventory sections; slot 331's doc read before `0x51` (J14.5, J14.7).
- **V5a-1**: the melee band's body and constants (J14.4). **V5b**: the reload finish as read; no
  ordinary-gun empty-clip reload record (J12); the owner's flamethrower exception
  requires the standing shot event, absent in the installed bank.
- **B1**: the turn script's helpers (J14.6).
- **A0 / harness**: the `removed` event kind if absent (J13).
- **`spec.md`**: the four species rows on the "on demand" line (J11); the bbox import and the
  feed-box read filed with the acquire cone's owner (J2b).

## V5a + A3 landed (integrator, 2026-10-04)

- **N1 closed** — `cover_armed` green: the cover tail `0x102b78a2..0x102b78ee` reads the enemy's active weapon (`0xa3` against a pistol).
- **N2 closed in the task, open in its input** — `TASK_WAIT_ATTACK_TIME1 0x102a337d` reads the stamp `0x10252450` + `0x102c5730`; `ranged_open_fire` stays red because the weapon's `m_flNextPrimaryAttack +0x730` has no NPC writer (no NPC shot runs through the weapon: the dispatcher is V4a lane A1, the event shot V4o lane O3). Game red, placed on A1 then O3 in the record's `known_red`.
- **Known red 2 closed** — `range_bands` green: `GatherAttackConditions 0x1026dd10` whole (the top clear `0x1026de02`, both arms, the friend timers, the tail `0x1026e062..0x1026e107`).
- **Known red 3, slot 363's part, closed** — `sense_enemy_facing_me` green; `ranged_open_fire` and `ranged_sustained_fire` raise no `BEHIND_ENEMY`. The trace named `0x56` `COND_?` (no name row); it is `ENEMY_FACING_ME` now (harness).
- **Packet R2's "`m_flStealthVisionCone +0x63c8` has no writer" is wrong**: `0x1028fca5` (in `FUN_1028fc90`, the per-pass reset) stores 1.0 (`docs/vtmb/npc-ai/senses.md` § Cone, corrected).

### V4c second-pass close measurement (2026-10-05; not green, no commit)

The three-build allowance is exhausted: build1 passed118.6s; build2 failed27.5s on two
fixture ResolveClipPair calls missing the required side argument; build3 passed25.2s.
No source changed after build3; temporary clock/sweep diagnostics were removed before it.
The lanes and all integration remain uncommitted and unstaged on spec-0002/step-2.
An external documentation commit advanced HEAD to947e57b6 (AGENTS/HANDOVER only).
Neither V4c nor V4s was ticked; no commit or push. Root200KB is absent.

Final default:169 executed,168 passed,1 failed (Stance.Driver),15.8s wall.
Final arm:1624 executed,1611 passed,13 failed,53.8s wall; no abort or unrun test.
Baseline default170 loses only the retired world-sweep MeleeBatch test, replaced by arm
character-clock coverage. Baseline arm1594 gains30 cases.
Full Arena ONCE after build3:132 records,110 pass/4 fail/16 expected-fail/2 unexpected-pass,
527.5s wall, report20261005T053609.881342Z. The eight new records all pass.
The earlier33-record named run was28 pass/4 fail/1 expected-fail; record-only cower/footstep
corrections passed by name and in the full run. After the full run, input_setrelationship's
unkillable staging passed by name (20261005T054700.512138Z); the full report still records
its original failure. No second full run. Kernel check7/7; no re-bake or corpus write.

| record | 64895278 | full run | remaining owner / measured cause |
|---|---|---|---|
| chase_melee | expected-fail | pass | Kernel swing, root interval movement and contact; distance below200cm. |
| corpse_fades | expected-fail | pass | Later fade installation wins: +10, alpha decrements, deferred removal. |
| corpse_pedestrian_stays | expected-fail | pass | 0x103a38c0 explicit NULL think remains NULL; no automatic removal. |
| damage_cower_one_hit | new | pass | Named attacker reaches0x109; task data2 is bit pattern0x40000000; positive18 kills with19 wounds. Zero staged before cower to preserve flag. |
| damage_high_health_control | new | pass | Effective cap99999, flag clear, five18 packets leave90 wounds and alive. |
| damage_knockout_one_hit | new | pass | Faint wired; TASK_KNOCKOUT sets0x440a0000; zero survives, positive18 kills at0x102beea4 with18 wounds. |
| melee_enemy_blocked | new | pass | 0x10347180 centre-ray block and failure ladder measured. |
| melee_same_team | new | pass | Same valid symbol rejects before hit-list/impact; negative-Y identical control contacts and damages. |
| ranged_enemy_dead | new | pass | BestEnemy drops dead entry; quiet ALERT observed; death after network-init retains corpse. |
| ranged_enemy_dead_retarget | new | pass | Live remembered replacement selected; dead corpse remains unselectable and resolvable. |
| team_damage_gate | new | pass | Real event shot admitted on different-team control, rejected on teammate before life dispatch. |
| idle_lookaround | pass | fail | Measured0x6b instead of required0x4f. Shared-stream draw-order audit remains V4c; record unchanged, no known_red. |
| unknown_crouched_band | pass | fail | MeasuredIGNORE_UNKNOWN0x3/0x60 instead of SEE_UNKNOWN0x1. Shared-stream draw-order audit remains V4c; record unchanged, no known_red. |
| input_setrelationship | pass | fail | Full-run player dies2.80 after31/31/31/33; enemy drops2.90. Unkillable staging subsequently passes named; full run was not repeated. |

Remaining test work is V4c, not V12. No V4d/V5b/V6 known_red was added to mask it.

| failing test | measured cause / next correction |
|---|---|
| Elysium.Substrate.Stance.Driver | No opening stance play or later destination play. Named stance rows alone leave the initial floor/activity fixture incomplete after the valid NPCInit disposition seed0x1029a6a9; inspect its initial sequence/clock, not a new RNG seed. |
| Elysium.Arm.Npc.ActivityResolve | Current sequence is retained on the miss, but the stale final assertion demands another play. ResolveActivityToSequence0x10272130 / SetActivity0x10272440 need fixture review. |
| Elysium.Arm.NpcKernelMotor.SpeciesProbes | After actual UTIL_SetSize propagation, the fixture must restore m_Collision through a forced hull setter before CheckStuck0x103ab580; measured push-distance assertion fails. |
| Elysium.Arm.NpcKernelMoveAndShoot.Run | Fixture still seeds only aim rows; CAP_MOVE_SHOOT early translation0x103854f0 needs ACT_WALK/RUN rows (added only in its Arm sibling). Overlay remains disabled. |
| Elysium.Arm.NpcKernelPlayerController.CreationPath | Effects assertion fails after disposition/sequence reset. Fixture authors no disposition table; floor ResetSequenceInfo0x10090950 adds0x300. Missing-table/activation ordering needs review. |
| Elysium.Arm.NpcKernelPlayerController.EffectsSurviveSnapshot | Same effects/setup failure, not proven to require V6. No later-owner placement. |
| Elysium.Arm.NpcKernelRunTask19.Base.ReloadAndSetActivity | AutoMovement advances floor sequence during the call; fixture only clears finished beforehand. IsActivityFinished0x10272900 completes early; isolate real clip/clock, not V5b reload. |
| Elysium.Arm.NpcKernelRunTask19.Base.ScriptArms | Same floor-sequence AutoMovement advance; slot251 gate0x10289448 sees finished. |
| Elysium.Arm.NpcKernelRunTask19.Species.Animal | AutoMovement finishes the unmodelled floor before the two-second arm0x1035f940; one-second fixture prematurely completes. |
| Elysium.Arm.NpcKernelRunTask19.Species.Zombie | Test asks IsCorpse for physics. That is now corpse identity; static0x1032c0e0 is a corpse too. Assert actual ragdoll creation, not identity. |
| Elysium.Arm.NpcKernelRunTask19.Troika.AttackArms | Floor-sequence AutoMovement advance; four unfinished arms complete early. Same clip/clock fixture migration. |
| Elysium.Arm.NpcKernelRunTask19.Troika.FinishingMoveAndOnFire | Floor finishes before bone-track sampling0x102ac4c7; sample time remains-5 and task completes. |
| Elysium.Arm.PlayerAttack.Reload | Old fixture expects PlayNpcClip on a claim channel; actual NPC AddGesture0x10099020 is the AnimOverlay table. Assert actual overlay before changing producer. |
| Elysium.Arm.Weapons.MeleeBand | NPC kernel commit plays at ambient; old expectation asks player scripted parity. Review presentation projection at0x10272400/0x10090950 against the claim contract before correcting it. |

Baseline reds are retained, with no new owner reassignment:

| record | baseline | current | measured cause / existing owner |
|---|---|---|---|
| damage_lethal_death | expected-fail | expected-fail | read false (pelvis 100.8 cm over the floor, bound 24.0; 0.0 cm/s, at rest under 5.0) at the end; V4d (no character physics asset) |
| lifecycle_unhide_fights | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 1.90]: arena_hidden SEE_ENEMY (0x46); 5: the StartHidden NPC thinks while hidden (SEE_ENEMY and FALL_TO_GROUND at t=0; retail ScriptHide 0x100a8710 installs a NULL think at FLT_MAX until ScriptUnhide 0x100a8990) |
| verbs_stealth_kill_scripted | expected-fail | expected-fail | unmet at its deadline (expect[1] was met at t=0.53); N11 (V7): the port's IsValidStealthKillTarget state term admits only Idle/Alert (ElysiumNpc.cpp, its comment calls the relaxed arm a developer arm); retail's shipping arm (debug_allow_non_idle_auto_sk '1') admits a victim in NPC_STATE_SCRIPT |
| idle_lookaround | pass | fail | unmet at its deadline; V4c |
| unknown_crouched_band | pass | fail | unmet at its deadline; V4c |
| input_changeschedule_reselect | expected-fail | expected-fail | unmet at its deadline (expect[1] was met at t=3.02); new: ChangeSchedule '-' forces no reselection (retail sets CHOOSE_NEW 0x82000000 in flags2 on every call, 0x102c33f0) |
| input_disablethink | expected-fail | expected-fail | appeared at t=5.08: match 1 where at most 0 may appear in [0.00, 9.00]: Hunter1 task_special_idle_activity; new: DisableThink with the wire's string param '1' disables nothing (ElysiumNpc.cpp:1387 takes only a Bool variant; retail converts the wire string to FIELD_BOOLEAN): task_special_idle_activity completes at 5.3 s |
| input_setrelationship | pass | fail | read "none" at the end; V4c |
| maker_respawn | expected-fail | expected-fail | unmet at its deadline (expect[4] was met at t=4.05); new: after the child's death (OnNPCDied at 4.05 s) the maker makes no second child within SpawnFrequency (MakerThink 0x1034bbf0 re-arms at freq) |
| rollcall_vanimal | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: plus_cat FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND and schedule FALL_TO_GROUND (0x3e) at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX). Was N10 ('installs no schedule'): that was red 6 -- the row is use_interesting 1 and the port's ambient executor held it; V3b deleted the executor and the row now selects |
| rollcall_vdog | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: plus_crowd FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND and schedule FALL_TO_GROUND (0x3e) at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX). Was N10 ('installs no schedule'): that was red 6 -- the row is use_interesting 1 and the port's ambient executor held it; V3b deleted the executor and the row now selects |
| rollcall_vmanbat | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: ManBat FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vmercurio | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: Mercurio FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vpedestrian | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: havenbum FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vsabbatleader | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: plus_Andrei FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vscurrying | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: CabbieSewer FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND and schedule FALL_TO_GROUND (0x3e) at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX). Was N10 ('installs no schedule'): that was red 6 -- the row is use_interesting 1 and the port's ambient executor held it; V3b deleted the executor and the row now selects |
| rollcall_vvampireboss | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: MingXiao2 FALL_TO_GROUND (0x3e); 5: the StartHidden row thinks while hidden (cond+ FLOATING_OFF_GROUND at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vwerewolf | expected-fail | expected-fail | appeared at t=0.00: match 1 where at most 0 may appear in [0.00, 0.45]: werewolf SCHED_TROIKA_START_COMBAT (0xea); 5: the StartHidden row thinks while hidden (cond+ TOO_FAR_FOR_MELEE at t=0.00, before the 0.5 s ScriptUnhide; retail PostSpawn 0x100aaf30 -> ScriptHide 0x100a8710 parks the think at FLT_MAX) |
| rollcall_vzombie | fail | fail | unmet at its deadline; H11 baseline harness |
| map_tutorial_sneak_past | expected-fail | expected-fail | unmet at its deadline (expect[2] was met at t=5.63); V12 (the hearing half; the first half is green since the V3b follow-up: 0.633 0xff -> 0x100 goal -1288 213 -102 -> 3.733 arrived -> 3.933 SCHED_TROIKA_DO_INTEREST_ACTIVITY (0x103)). After the walk starts (6.933) thug_1 hears the player at 7.633 (OnHearPlayer, cond+ HEAR_PLAYER, break INVESTIGATE_SOUND, Idle -> Alert) and runs 0x4c SCHED_TROIKA_ALERT_TURN_TO_SOUND, not an INVESTIGATE program, within 1.0 s: retail's alert ladder FUN_102b8980 answers 0x4c at m_eAlertLevel 0 unless m_bFullInvestigate +0x6340 is set (docs/vtmb/npc-ai/programs.md:396-399). Whether investigate_mode 4 sets +0x6340, i.e. whether the INVESTIGATE match is a record error, is open for V12's reader (Q-V3bf1) |

Every one of the nine hidden species rollcalls above was expected-fail at64895278, as was lifecycle_unhide_fights. They are not newly parked on V5b; their existing red5 is hidden-lifecycle work, not reload. Placeholder was green at baseline and was corrected to retail NPCInit0x103a4350 ThinkSet(NULL), remaining green. N4 hear_world_investigate and interest_mode_never remain the two baseline unexpected-passes.

Recovery and staging corrected in this pass:

- NPC event3031 stages at curtime; CommitTime>0 is not an estimate discriminator. The invalid
  guard was removed; BeginRangedShot still stages no NPC estimate. Retail0x10238160→0x10238320→
  0x102383b0→0x102387b0. cover_move_shoot and team_damage_gate pass again.
- Constructor0x1028d230 writes m_flStanceTime(+0x64e4)=curtime; this host writes it before
  Troika spawn's disposition lookup. NPCInit0x1029a6a9 seeds a valid default disposition index;
  bHasDispositionIndex now reflects that. These fixes remove spurious initial stance draws,
  but do not yet recover idle_lookaround/unknown_crouched_band's required verdicts. No reseed
  or expectation relaxation was made for either; the remaining draw-order audit is V4c.
- InputFaint0x1029f250 resets slot614, records line0x26c2 and SetSchedule(0xfa,false).
- Dead-enemy records originally killed at0.417, before network-init0.80. Retail0x1028d8d0
  walks every Troika (derived bit0x40) and slot5840x1028d910 resets all think timers, including
  that corpse's +10 deadline. Death is now after network-init; both controls pass. Quiet
  ALERT is probed before0x4b's five-second timeout, rather than promised through end8s.
- seed_health now recomputes before the initial probe: effective cap99999 is measured.
  The five-hit control ends alive at90 wounds. Knockout flags0x440a0000 kill positive18 at
  18 wounds; cower flags0x40000400 kill positive18 at19 wounds (including trigger1), both
  at0x102beea4, far below cap. Zero during cower raises LIGHT_DAMAGE at0x10265ed0 and interrupts
  the schedule/clears its flag; zero is staged before cower, and the positive packet atflag+0.45
  lands inside the unchanged0.5 deadline. No damage arithmetic or historical fifth-hit claim.
- The same-team contact control passes on identical negative-Y seats. No contact/impact/hit-list
  write reaches the matching teammate. The scalar cower trigger was replaced by a real player
  handle through damage_packet; the actual OnTakeDamage chain runs for zero too.
- ranged_open_fire and input_setrelationship use retail MakePlayerUnkillable: dead-enemy
  selection correctly drops a player who dies under their real bullets. Their expectations
  remain intact. Footstep staging now seats the player150cm north of the first patrol goal;
  the old ~700cm seat allowed one-second thinks against a0.939Hz walk, and0x10091880 lost loop
  windows. The near cadence0x10290b60 restores repeated2050/2051; event windows are unchanged.
- SharedPick/RagdollSeed/RunAnimation/interval-movement tests now supply models; the shared
  picker already returns-1 on empty0x10427fc0, and the test guards its array index. Session
  registry construction covers uninitialized headless fixtures. Other migrated tests author
  raw candidates, exact disposition levels, real feed cells/side, and the actual event shot.

Seams still named: D9 CalcPose-at-cycle / bone cache, m_flPoseParameter blending, slot247
seqdesc bounds, held weapon model clock/events/weights (0015), ordinary ragdoll rig/hitbone/
Spine2/force (V4d), render-alpha presentation, restored-corpse presentation (V6), real reload
slots322/323 (V5b), and TzimisceMelee+0x910. No pipeline/body-data/bbox import or re-bake.
Bounded silent inventory475 sidecars/9401 clips in0.404s found only male/female move_and_ranged
flamet_attack and flamet_attack_layer without3030..3044; both have no events. No timer fallback.


### V4c closing worker final measurement (2026-10-05; six builds, not committed)

All six builds passed:112.703s/18.500s/18.859s/21.765s/15.125s/60.031s.
No seventh build, no staging/commit/push; V4c/V4s remain unticked and AGENTS.md
was not edited or staged. The original lanes remain in the working tree.
Temporary stack/RNG diagnostics were removed before build4. No asset re-bake.

Final default169/0 failures (15.5s); final arm1624 executed,1623 pass/1 fail
(53.5s); kernel7/7. The sole full arena run, on build6, is
E:/elysium-work/reports/arena/20261005T064949.706263Z/index.json:
132 records,105 pass/9 fail/17 expected-fail/1 unexpected-pass (8m49s).
Use that aggregated index: the arena-host subindex contains only125 records.
All eight new wave records pass. The three EF->pass wave records remain green.
The nine hidden-species rollcalls and damage_lethal_death retain baseline red.

Every moved full-run verdict since previous-report.md:

| record | previous full run | final full run | measured cause / remaining audit |
|---|---|---|---|
| input_setrelationship | fail | pass | Previous worker's MakePlayerUnkillable staging now included in full run; bullets no longer kill the required enemy player. |
| range_bands | pass | fail | At6s the player is teleported during STEP_BACK, rather than the previous range-only phase: TOO_CLOSE_TO_ATTACK0x5f at6.1, CAN_RANGE_ATTACK1 again6.5, but no0x8 by7. First WAIT_ATTACK_TIME1 completes3.0 instead of4.0. Actor position/program phase changed with the corrected draw stream; whole band/task timing attribution remains open. No band predicate or expectation changed. |
| ranged_open_fire | pass | fail | Same program-phase movement: STEP_BACK and WAIT_ATTACK_TIME1 complete3.0; the record forbids completion in[3,3.5]. Previous completion4.0. The first real3031 still occurs0.5; the weapon deadline has expired by3.0. Full retail phase audit remains open. |
| hear_world_out_of_range | pass | fail | Blanket never(state) rejects the now-visible retail initialization None->Idle at0.0; no sound-investigation assertion failed first. |
| sense_beyond_vision | pass | fail | Same initial None->Idle rejection, not a newly measured vision-range predicate failure. |
| sense_cone_outside | pass | fail | Same initial None->Idle rejection, not a newly measured cone predicate failure. |
| interest_mode_never | unexpected-pass | expected-fail | Its existing known_red now catches the same blanket initial state ban. The hearing-expiry attribution is not what failed first in this run. |
| rollcall_vfrenzyshadow | pass | fail | Admission retains NPCInit's0xb, verified0x10375c80; HUNT_FINISH0x85 -> GET_PATH_TO_LASTPOSITION -> no route0xc at0.0. The former Idle overwrite hid this program; full route prerequisite remains to audit. |

Unmoved red records:

- idle_lookaround: named build6 selects0x4f at0.350, then fails its unchanged
  never(state) on None->Idle. Full run selects0x6b at0.0 and also fails that state
  ban. ConstructorNeutral0x1028d42c, equal-bound no-draw vstdlib0x10002e60,
  draw-free visual preview, and retained NPCInit state0x1029a0f5 are recovered;
  full-run start-clock/draw interleaving remains open. No seed/expectation change.
- unknown_crouched_band: named passes, full still IGNORE_UNKNOWN0x3/0x60 at0.700.
  Named initialization and later full-stage curtime branches differ; complete
  full-run draw order remains unresolved. The record is unchanged and has no
  known_red. This is still V4c work.
- rollcall_vzombie: baseline H11; no interesting places -> TaskFail0x22 at1.967.

The one arm failure is Elysium.Arm.NpcKernelBosses.CanPlaySequence: AndreiBlood
Activate0x1035dc93/99 writes COMBAT2, now retained through admission. Two idle
assertions call CanPlaySequence(false,0) without staging idle. Explicit SetState(1)
is the next fixture correction; the compile cap prevents another tested change.
All original13 arm failures and Stance.Driver pass on the final binary. Their
fixture migrations, new equal-range no-draw assertion and admission-state pins
are documented in npc-ai/lifecycle.md's closing-worker recovery. Twenty-five
additional stale-state fixtures exposed by admission were migrated through the
common world's explicit established-IDLE setup; the boss's COMBAT2 was correctly
not overwritten by that setup. No test was deleted by this worker.

Damage measurements remain: effective cap99999, five18 commits ->90 wounds alive;
knockout flags0x440a0000 ->18 wounds/death; cower flags0x40000400 ->19 wounds/death
(including trigger1), both0x102beea4. Historical fifth-hit cause remains unverified.
Pose/bone-cache, include-group shadow metadata, template fake-reload bounds,
ragdoll rig/bone/force/pelvis (V4d), real reload and restore seams remain named.
No modernization of event/state order was adopted to hide a failure.


### V4c final integration accepted (2026-10-05)

One build used of six allowed: --arm incremental 31.3 s, passed. Final binary
unchanged after that build. Default 169 pass / 0 fail (15.5 s wall); arm 1624
pass / 0 fail (52.9 s wall); regenerated kernel 7/7. Both RNG records pass alone
and after another record in one boot, with seeds/expectations unchanged.

The first full run (20261005T072253.912124Z) was 110 pass / 4 fail / 16 EF / 2 UP;
it exposed face_enemy_turn, lifecycle_relationship_flip and memory_occluded_kept.
Their retail/staging/observation corrections are in npc-ai/lifecycle.md's
"V4c final integrator" section. The turn's 0.5 bound and memory's 1.3/2.1
condition deadlines remain. No additional code or build was needed.

Final second full run: E:/elysium-work/reports/arena/20261005T074109.557188Z/index.json,
132 records, 113 pass / 1 fail / 16 expected-fail / 2 unexpected-pass, 8m49s wall.
Compared against 64895278's archived full run 20261004T210246.613350Z:
all 102 baseline passes and all eight new records pass; no baseline pass regresses.
V4c and V4s ticked. No asset import/re-bake, no generated slots edited by hand,
no new event/state-order modernization. AGENTS.md is the owner's edit and excluded.

A: NPCInit 0x1029a0f5 directly writes NONE, ideal IDLE at 0x10273473. The first
MaintainSchedule calls real SetState/hook chain at 0x10281b63 -> 0x1026e340 before
selecting a program. Keep that trace event and all hooks; state bans judge after
its establishing edge. One arena rule, with no edits to the four records.
B: the animation picker already shares NpcSchedule and SeedAll resets it; the leak
was boot curtime bypassing NPCInit/StartNPC's first-second delay/draw branches.
Each rebuilt arena now starts clock 0 before Load; map hosts retain their clock.
C: retail Shot 0x1023891b writes the next-attack stamp; WAIT_ATTACK_TIME 0x102a337d
adds RandomFloat(Min,Max) 0x102c5730 scaled by 0x102c5570. No fixed 3/4 deadline:
the fresh-clock first wait ends 3.700 and both unchanged records pass.
D: FrenzyShadow 0x10375c80 keeps HUNT 0xb. Select 0x10375d90 -> 0x102af660 reaches
HUNT_FINISH without enemy/path; zero last position is inside cover. 0x10285c87
reports NO_ROUTE. Corrected roll call asserts that chain, FAIL/backoff, and survival.
E: Andrei Activate 0x1035dc93 keeps COMBAT; idle CanPlaySequence assertions stage
SetState(1) explicitly. Arm now has zero failures.

Every verdict moved during V4c since 64895278 (arrows include temporary reds
from the two handoff reports and the first full run; EF/UP abbreviate expected-fail/
unexpected-pass):

| record | 64895278 -> interim -> final | cause |
|---|---|---|
| chase_melee | EF -> pass | Kernel swing commit, root interval movement and character-owned contact; distance below 200cm. |
| corpse_fades | EF -> pass | Later fade installation wins; +10 deadline, alpha decrement and deferred removal. |
| corpse_pedestrian_stays | EF -> pass | Retail 0x103a38c0 NULL think retained; no automatic removal. |
| melee_enemy_blocked | new -> pass | Centre-ray block 0x10347180 and failure ladder. |
| melee_same_team | new -> pass | Equal valid team symbol rejects before hit-list/impact; identical different-team control contacts. |
| team_damage_gate | new -> pass | Real event shot rejected on teammate before life dispatch; different-team control admitted. |
| ranged_enemy_dead | new -> pass | BestEnemy drops the dead entry; ALERT before timeout; death staged after network init. |
| ranged_enemy_dead_retarget | new -> pass | Live remembered replacement selected; corpse resolvable but unselectable. |
| damage_high_health_control | new -> pass | Effective cap 99999, five 18-point packets -> 90 wounds, alive, flag clear. |
| damage_knockout_one_hit | new -> pass | TASK_KNOCKOUT sets 0x440a0000; zero survives; positive 18 kills at 18 wounds through 0x102beea4. |
| damage_cower_one_hit | new -> pass | Cower sets 0x40000400; positive 18 kills at 19 wounds (trigger 1 included) through 0x102beea4; zero staged before cower to preserve flag. |
| idle_lookaround | pass -> fail -> pass | First-second clock/draw leak selected 0x6b instead of 0x4f; initial state edge also tripped ban. |
| unknown_crouched_band | pass -> fail -> pass | Same clock/draw leak read IGNORE_UNKNOWN; fresh clock restores SEE_UNKNOWN. |
| input_setrelationship | pass -> fail -> pass | Required enemy player died under real bullets; retail MakePlayerUnkillable staging. |
| range_bands | pass -> fail -> pass | Inherited boot clock changed draw/program phase; teleport landed during STEP_BACK. Fresh stage clock restores 0x8 at 6.100. |
| ranged_open_fire | pass -> fail -> pass | Same startup draw leak moved the wait to 3.0 inside its ban; fresh-clock first wait ends 3.700 with windows unchanged. |
| hear_world_out_of_range | pass -> fail -> pass | Blanket state ban counted the real establishing NONE -> IDLE edge. |
| sense_beyond_vision | pass -> fail -> pass | Same initialization-state observation rule; vision predicate intact. |
| sense_cone_outside | pass -> fail -> pass | Same initialization-state observation rule; cone predicate intact. |
| rollcall_vfrenzyshadow | pass -> fail -> pass | Retained retail HUNT exposes the real no-route chain; corrected record instead of inventing IDLE. |
| face_enemy_turn | pass -> fail -> pass | Fresh-clock rescan lost new LKP; teleport after scan restores knowledge. Sight rises mid-turn, so probe SEE_ENEMY at 7.0; original 0.5 turn bound retained. |
| lifecycle_relationship_flip | pass -> fail -> pass | Seven real shots killed player at 18.5; retail unkillable staging preserves required enemy/state. |
| memory_occluded_kept | pass -> fail -> pass | Schedule condition clear erased gather's falling trace delta; probe actual SEE bits at original 1.3/2.1 deadlines. |
| interest_mode_never | UP -> EF -> UP | Initialization-state ban repaired; baseline N4 intermittent hearing verdict remains allowed. |

The three damage controls measured cap 99999: flag-clear five 18-point packets
leave 90 wounds alive; knockout 0x440a0000 kills positive 18 at 18 wounds; cower
0x40000400 kills positive 18 at 19 wounds (trigger 1 included), both 0x102beea4.
Zero survives knockout; before-cower zero preserves the cower flag. These controls
do not prove the historical fifth-hit cause, which remains unverified.

Remaining reds are baseline: rollcall_vzombie H11 (no interesting places, 0x22);
nine hidden-species rollcalls and lifecycle_unhide_fights (NULL-think hiding);
damage_lethal_death V4d (pelvis 100.8cm high, no physics asset); scripted stealth
kill, ChangeSchedule/DisableThink inputs, maker respawn, tutorial hearing.
The two N4 hearing intermittents remain unexpected-pass. Existing pose/bone-cache,
include-group shadow metadata, bounds/template fake-reload, discipline, rig/force,
real reload and restore seams remain named. No new known_red field masks V4c.

### V4d witness save smoke — V6 placement (2026-10-05)

On sm_hub_1, two placed bum_male pedestrians (#1925/#1927) died from numeric
TakeDamage10000; both actual .phy assets admitted15 bodies/sim1. Save v4d-smoke at
118.439, load: anchors and Dead minds remain, one guarded presentation handoff per
rebuilt visual, coherent collapsed garments and no crash/standing resurrection.
However m_lifeState(+0x200, slot158 IsAlive at0x100b4dc0) reads0 instead of1, and
Health10 returns after load. `research where m_lifeState` identifies its generated
SAVE row (ElysiumClassRegistry.cpp); this observation does not recover why the live
restore loses those words. **V6**, already the owner of corpse save semantics, must
recover/port the complete save/restore word/think chain. No V4d record is marked
known_red for this smoke, no physics frame/timing was altered to conceal it.
Evidence: codex/V4d-int/bum-map-dead.png and bum-map-restored-close.png under the
work root; run-play 20261005T091313.291073Z.

### V4d final verdict comparison (2026-10-05)

Compared complete 132-record reports: d0f79574's 20261005T074109.557188Z against
20261005T091932.640246Z. Baseline113/1/16/2 ->114/1/15/2 (pass/fail/expected-fail/
unexpected-pass); all 113 baseline passes retained. Only damage_lethal_death moves
expected-fail -> pass: real authored ragdoll plus measured rest and the floor probe
at8s before correct unseen removal at13.017. All corpse controls and stealth stay pass.
Default 169/0, arm 1625/0, kernel 7/7. No new known_red; existing H11/N4/other-story
placements retain their actual verdicts. Physics frames/garments/release are proved
for this scope; save state/impulse/BurnModel remain the named V6/0014 work.


## V5b integration recovery (2026-10-05)

Integration tree: `spec-0002/step-2` at `e6e21d58`, a docs-only descendant of
V4d `a5b58f37`, plus the three lanes and the
compile gate's owed lines, excluding the owner's dirty AGENTS.md / ElysiumRng.cpp
from staging. The compile gate used two builds (127 s failed protected access;
83 s passed after two test friends), with kernel regeneration/check 7/7.

Red 4 is a record question, not a mask defect: the authored 0xef text and slot 453
omit NOT_FACING_ATTACK 0x61 / WEAPON_THROUGH_WALL 0x3c / TOO_CLOSE_FOR_RANGED 0x08.
The old 99-second sink was unfinished TASK_RANGE_ATTACK1. Seed 1 stages 0xef at
6.100, teleport [1200,683,0] at 6.400 behind the west-facing shooter [683,683,0]
(517 cm = 203.5 Source units; timed distance >500 cm). The turn reacquires the
enemy, NOT_FACING_ATTACK rises 6.900, and the program's LAST task_wait_attack_time1
finishes 9.100 before 0xed is selected. This witness is `ranged_step_back_holds`.
The damage witness needs a player activator: retail null-attacker exit
`0x10265f64` precedes LIGHT_DAMAGE. V5b ports InputTakeDamage `0x102c29a0`'s
integer/zero type arms and caller/activator packet identities, and the arena
fire action can name the live activator. No magnitude escalation is used.

The separately confirmed cache defect IS repaired: `0x1026a0f0` copies both masks
then calls slot453 once, empty411, positive freeze insertion; it preserves
current conditions, and Guard1 replaces Troika453. Arm tests pin order, inverse
mask, condition preservation and positive-only HasInterruptCondition.

J12's ordinary NPC Shot retains its clip, but the six-set TutorialThug counter
now decrements per FireBullets set `0x10268919`, rerolls through the char-template
loader `0x101d4394..0x101d43c8` / `0x102c54c0`, and selects fake reload first
(`0x102b8620`). `ranged_fake_reload` observes shot six 16.500, 0xc4 19.500,
no-cover 0xc5 19.600, Pistol_Reload_Fast finish 22.100 and resume 22.200.
`ranged_sustained_fire` keeps its four never clauses; shot_7's 6.0-second
window is the measured 5.7-second interval plus 0.3 seconds.

Real TASK_RELOAD slots322/323 are ported with arm proof of live deadline,
bulk writes and NPC reserve retention. The owner's flamethrower exception was
checked: entity registration and baked wield/standing attack/reload assets exist,
but `flamet_attack` and `flamet_attack_layer` have empty event timelines in the
male AND female banks, and specialized Attack `0x103e2f30` is absent in the port.
The missing shot event / actual NPC Attack entry is filed for the asset/attack
judge under the explicit no-bake fallback. Full evidence: combat-and-damage.md.
Presence cast/status producer remains spec0006; retained NPC single-round
bInReload persistence remains V6 (`0x1025506f..0x10255077`). No modernization.

Named final binary proof: `20261005T100145.635221Z`, all eleven records pass
with no first_unmet: cover_armed, cover_move_shoot, range_bands, ranged_fake_reload,
ranged_friend_in_line_of_fire, ranged_open_fire, ranged_step_back_holds,
ranged_sustained_fire, control_sequence, cover, input_takedamage. The host
actually runs combat records before world records; cover_armed precedes both
new records (control_sequence is later), so this boot proves after-another
independence. Alone: step `20261005T095940.898815Z`, fake
`20261005T100012.187497Z`, sustained `20261005T100047.958407Z`: all pass.
The step witness breaks LIGHT_DAMAGE at 9.700 after the 9.617 input delivery.
Its distance probe runs at 6.450, after the 6.400 teleport; a probe at the
same script timestamp samples before the action and cannot measure the new seat.
Third build (the input/activator repair) passed in 116 s; default final
`20261005T095834.803076Z-elysium-session-elysium-substrate`: 169/0.
Oracle citations: 3/3, fixing the four values, brief copies of those old
headings, and the two GetSchedule citations invalidated by PreSelectSchedule's
corrected identity.

First arm run exposed two wave test defects, not runtime divergence: CacheTail
claimed a non-interruptible director but authored no `spawnflags 32`; Spawn
`0x101a6f10` therefore made it interruptible and RemoveIgnoredConditions
`0x101a89a0` correctly returned. The fixture now authors retail `0x20`.
WiredYukieMelee still expected `0x18000` after the compile gate restored
CWeaponMelee's complete `0x40018000` (`0x103eaea0`); its expectation is corrected
without changing Yukie's masked gate `0x103dd900`. All other arm tests passed.

Fourth build passed in 15.312 s (two arm fixture/expectation edits only).
The final source citation identifies director Spawn `0x101a6f10`; the comment
correction after compilation changes no compiled behavior. Final default
again executes all 169 tests with zero failures.

Final build-four validation: default `20261005T100533.860877Z-elysium-session-elysium-substrate`, named boot
V5b-int/named-final4.log (11/11), arm `20261005T100858.960158Z-elysium-arm`
(1636/0). Both new records and changed sustained fire also pass alone on build
four, with outputs in V5b-int/{fake,step,sustained}-alone-final.log.
Kernel final check `20261005T101242.470381Z`: 7/7. Evidence-only completion
corrected the stale schedule-kernel empty-inverse paragraph and slot453
verdict targets/evidence (Troika, Guard1 and HumanCombatant), then regenerated;
all three generated runtime outputs remained unchanged.

Full arena ONCE, final binary: `20261005T101029.230065Z`, 9m00s, 134 records:
116 pass / 1 existing fail rollcall_vzombie / 15 expected-fail / 2 unexpected-pass.
Machine comparison with V4d `20261005T091932.640246Z` retains all 114 baseline
passes, finds no missing records and no moved existing verdict. Only additions:

| record | before | after | retail cause |
|---|---|---|---|
| ranged_fake_reload | new | pass | Six-set TutorialThug count, reroll, 0xc4 -> no-cover 0xc5, authored Pistol_Reload_Fast -> resume 22.200. |
| ranged_step_back_holds | new | pass | 0xef texts/overlay omit 0x61/0x3c/0x08; NOT_FACING_ATTACK survives the hold through final task 9.100; player-activated one-point hit breaks next 0xed on LIGHT_DAMAGE 9.700. |

The baseline zombie still fails on `No interesting places were available to go
to. (0x22)` at 4.3667, H11; it is not a V5b regression. The launch command
returns exit7 for that existing fail / the two retained unexpected-pass records;
the owner's green criterion is satisfied by the explicit verdict comparison.
V5 is ticked in spec.md and TRACKER.md. No new known_red or modernization.
