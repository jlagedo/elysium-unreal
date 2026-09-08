# 0005 park-stealth — NPC AI

## Witness
`sp_tutorial_1`'s stealth lessons played against retail. The sneak-past lesson on `thug_1`: he
idles at his interesting place, hears the player's footsteps and walks the alert ladder to look,
sees the player only inside `540 ×` the light scalar and the 157° cone, and only then commits an
enemy and fires `OnFoundPlayer` into the "spotted" sign. The stealth-kill lesson on
`stealth_victim`. The post-feed trance on any civilian.

## Scope
The whole of NPC AI: everything an NPC senses, remembers, decides, schedules and navigates, in
retail's order. Four themes: stealth, senses and memory, the schedule host, incapacitation.

Owned elsewhere and consumed here: weapons, damage, death — **0006**; firearms — **0009**;
disciplines' own effects and costs — **0007**; conversation UI and the `.dlg` runtime — **0004**
(this spec provides the `NO_DIALOG` refusal and the partner's sense freeze).

## Sources
- Oracle: `docs/vtmb/npc-ai-reverse-engineering.md` (the NPC AI oracle), `docs/vtmb/stealth.md`,
  `docs/vtmb/navigation-jump-links.md`, `docs/vtmb/feeding.md`, `docs/vtmb/footsteps.md`,
  `docs/vtmb/entity_io.md`, `docs/vtmb/activity_enum.md`, `docs/vtmb/animation_and_movers.md`,
  `docs/vtmb/disciplines.md`, `docs/vtmb/lighting.md`, `docs/vtmb/vdata-catalog.md`,
  `docs/vtmb/sp_tutorial_1-event-surface.md`. Each story names its section.
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `maps/sp_tutorial_1.entities.glb`,
  `maps/sp_tutorial_1.lighting.glb`, `nav-graphs/sp_tutorial_1.glb`, `vdata/` (`stealth.txt`,
  `sound_volume_table.txt`, `interestingplacetypelist.txt`, `Rules.txt`, `npctemplate*.txt`,
  `StealthKillRules.txt`).

## Witness data
**`thug_1`**, child of `npc_maker` `thug_maker` (`NPCType npc_VVampire`, `NPCTargetname thug_1`,
`Flag_StartDisabled 1`, `Flag_InfChild 1`, `MaxLiveChildren 1`, `SpawnFrequency 5`), model
`Shovelhead/shovelhead.mdl`, `stattemplate TutorialShovelhead`, `soundgroup Young_Thug`.
- Relation: `player_reaction D_HT 5`; `default_disposition Neutral`; `additionalequipment
  item_w_tire_iron`; `combat_start_activity ACT_INVALID`.
- Senses: `npc_perception 3`, `vision 540`, `hearing 1.00`, `investigate_mode 4`,
  `investigate_mode_combat 4`, `full_investigate 0`, `allow_alert_lookaround 1`,
  `pl_investigate 1`, `pl_criminal_attack 2`, `pl_criminal_flee 6`, `pl_supernatural_attack 4`,
  `pl_supernatural_flee 6`.
- Reaction keys: `hint_groups 1..32`, `allow_kick_hint_use 1`, `use_interesting 1`,
  `interesting_place_groups 2`, `percent_occluded_wait 10 / cover 30 / walk 10 / flank 20 /
  chase 30`, `bright_route_penalty 0`, `follower_type Default`, `stay_entrenched 0`, no squad.
- Outputs (authored on the maker, forwarded to the child): `OnFoundPlayer →
  logic_failed_thug.Trigger` (0.5 s) → `popup_29.OpenWindow`, whose `OnUseBegin` kills him;
  `OnDeath → kill_check.Test`, `trig_popup_tireiron.Enable`.
- Spawn and reset: `trig_dialog_feed_on_rats.OnStartTouch → thug_maker.Spawn` (the player is in
  Jack's dialogue ~2380 units away); `logic_failed_thug_2 → thug_maker.Spawn` at 1.2 s and 2.5 s
  beside `teleport_thug.Teleport`; `logic_reset_sneak_2` / `logic_reset_unarmed_2 → thug_1.Kill,
  thug_maker.Spawn`.
- Places: `pt1` (`group_id 2`, `enabled 1`, `min_time 30`, `max_time 60`) at his spawn; `pt2`,
  `pt3` (`group_id 2`, `enabled 0`) down the alley. His pool is `pt1` alone until a script enables
  the others.
- The `trig_diversion → logic_gunfire` sounds are `ambient_generic`s with `sound_event 0`: audio
  only, no NPC hears them. His investigate inputs are the player's footsteps (180/240 units) and
  doors.

**Retail, spawn to detection.** Look prefilters at 3072 units every 0.15 s; admission needs
`d ≤ 540 × player light scalar` (75.6…540), the 157° cone, and an eye-to-eye trace; the outer 30 %
of that radius raises `SEE_UNKNOWN` instead. A pass writes `SEE_PLAYER` + `SEE_HATE` and an
enemy-memory record; `BestEnemy` walks that memory only, so at 2380 units nothing happens and he
takes `pt1`. Hearing yields conditions 0.2–0.9 s late, never a memory record: a noise sends him
through turn → step → walk to the sound (once per life), and he turns hostile only by seeing.
On commit, `NEW_ENEMY` breaks the interest program; the committed-enemy LOS edge fires
`OnFoundPlayer`. The player's dialogue is invisible to him.

**`stealth_victim`**, child of `stealth_victim_maker`, same model and template, `player_reaction
D_NU 0`, `item_w_fists`, `vision -1`, `hearing -1`, `allow_alert_lookaround 0`,
`interesting_place_groups 8`, all `pl_*` 6; `popup_39.OnUseEnd → SetRelationship player D_HT 5`;
`OnDeath → thug_maker_4.Spawn`.

The seven Society hunters and `sentry2` at the far end of the BSP are an Unofficial Patch
level-select hub, not tutorial content.

## Stories
In build order within each theme. A story is done when every behaviour it lists is in the
substrate and its recovery is written in the oracle section it names. Numbers are stable ids
cited by other documents; a split keeps the number and adds a letter. Each open story carries
the retail contract the code must match, the job, and what it consumes or provides.

### Stealth
- [x] **1. Target surface and the light query.** The light row and `Sneaking` publish the vision,
  cone and hearing scalars; `trigger_stealth_mod` is a balanced overlap modifier. The light query
  is a live per-worldlight evaluation: Source falloff by light type, the cone/angle term, the live
  lightstyle value, occlusion by a trace only world geometry and unflagged static props block,
  the first sun via a sky trace, luminance `0.30/0.59/0.11`, sum clamped to `[0, 1]`; eligible
  only under `FL_DUCKING` and unseen by any `D_HT` NPC for 1.0 s, `-1.0` when not, sampling runs
  even when ineligible, three sample points off the world AABB. Oracle: `stealth.md` § "Player
  target-surface update", § "`trigger_stealth_mod`", § "The light query, recovered". Settled
  as not in the image: `IVEngineServer` slot 118 (`GetLightForPoint`), the `0x100` shadow-brush
  contents name.
- [x] **2. The light gauge producer.** Gameplay publishes the 0..10 light row into
  `FElysiumStealthView`, valid once a real sample exists, shown only while crouched; the HUD is
  this project's own five-step asset and maps the eleven rows itself. Oracle: `stealth.md`
  § "HUD observability is not authority". Unrecovered: the recv-prop names of the client's
  `+0x16b4` / `+0x16f4` stealth icon fields.
- [x] **3a. Stealth kill: rules and arc.** `StealthKillRules.txt` loader, the deaf arc
  (`InDeafArc` `0x101be500`) and minimum approach depth (`ComputeMinDepth` `0x101bef50`),
  active-weapon Brawl/Melee arc selection, the rear deaf zone shared with hearing. Oracle:
  `stealth.md` § "Stealth-kill transaction (RE50)".
- [x] **3b. Stealth kill: victim selection.**
  Retail: `FindVictim` (`0x101be1f0`) runs per frame with a cache; admission is the trace to
  the victim, the weapon's stealth-kill capability, the arc and depth from 3a; an oblivious
  victim (7) admits a backstab from any angle.
  Job: the victim query on the player, the weapon capability read, the obliviousness override.
  Oracle: `stealth.md` § "Victim selection and per-frame cache".
- [x] **3c. Stealth kill: commitment and the paired action.**
  Retail: the input commits the kill; grapple type 3 pairs attacker and victim through the
  `m_GrappleType` role pair; the victim's death is synchronized to the action; the tutorial's
  lesson completes on the victim's `OnDeath`.
  Job: input commitment, the grapple type 3 pair, the synchronized death, the output.
  Consumes: the grapple state machine and death from 0006.
  Oracle: `stealth.md` § "HUD publication and input commitment", § "The grapple role pair and
  the `m_GrappleType` enum", § "Tutorial lesson completion".
- [x] **4. The committed observer snapshot.** Gameplay publishes searching/detected to the HUD
  after the NPC state commits; the HUD runs no trace and no detection. Oracle: `stealth.md`
  § "HUD observability is not authority".

### Senses and memory
- [x] **5. The enemy memory** (`CAI_Memory`). Records at `mem+0xc` (handle, last position,
  anchor, velocity, last-seen time, nav nodes, position-only byte, eluded byte) written by
  `UpdateEnemyMemory` (slot 544, `0x102709c0`) from `OnLooked`'s `D_HT`/`D_FR` arms;
  `RefreshMemories` (`0x102df320`) drops only dead or invalid handles; `BestEnemy`
  (`0x102743c0`) walks this list only; `ChooseEnemy` (`0x10279dd0`) treats a null active
  schedule as interested in `NEW_ENEMY` / `LOST_ENEMY` / `ENEMY_DEAD`, so no substitute spawn
  schedule. Oracle: § "The enemy memory — `CAI_Memory`".
- [x] **6. Sight and hearing.** 3072-unit prefilter; cadences 0.15 s players / 0.25 s NPCs /
  0.45 s objects; `m_flFieldOfView 0.2`; `SEE_PLAYER 0x5a`; `IRelationPriority` `<0` DISLIKE /
  `0..10` HATE / `≥11` NEMESIS; `FinViewCone3dNew`'s strict front test then the apex cosine
  times the target's cone scalar; the combat-state range bypass; concealment `0x10146b20`; the
  outer-band path `0x102b3e00` (`SEE_UNKNOWN`, `OnUnknownVisionPlayer`); hearing radius ×
  `hearing`, conditions delayed `RandomFloat(0.2, 0.9)`, deaf-arc suppression, the
  cowering/sleeping quarter radius, the seventh (`Flinch`) record; `ambient_generic`
  `sound_event` / `sound_event_level` 1..3 → 180/240/1200, non-occludable, once per activation;
  `m_flStealthVisionOverrideTime` (+0x6604) written by damage with a 5 s deadline, consumed by
  `FVisible` (`0x102b4760`) as a range bypass. Oracle: § "The sense pass for a hated player,
  walked", § "Hearing, walked", § "`ambient_generic` as an AI sound source", § "Sense and
  investigate leftovers, closed". Named seams: the cone-apex ConVar (`0x10937a8c`, no writer in
  `.text`) and the 2-D cone mode (`0x10936f74`); 0007's cloak/detection-record producers.
  Settled as not in the image: `+0x6081`.
- [x] **7. Obliviousness.** `TASK_MAKE_OBLIVIOUS` 0x131: `flags2 |= 0x80001000`,
  `SetEnemy(NULL)`, squad disconnect (`0x1026d050`, a seam until 17), `++m_iIsOblivious`
  (+0x5bb4, saved), `OnIncapacitatedStart`. Consumers: `PerformSensing` skips the pass,
  `UpdatePoseParameters` (slot 314) drops aim, slot 587 rejects; `FindVictim`'s any-angle
  backstab is 3b. Oracle: § "`TASK_MAKE_OBLIVIOUS` and `m_iIsOblivious`". Settled as not in
  the image: slot 587's name (`CanWitnessSupernatural()` is the project's).
- [x] **8. The NPC flag word.** `m_bfAINPCFlags` (+0x14b8) / `m_bfAINPCFlags2` (+0x14bc), 62
  names (`0x1030cbd0`); `TASK_SET/CLEAR_NPC_FLAG` 0x100/0x101; `D_IS_BUSY` →
  `IsBusyWithDiscipline` only, `NO_DIALOG` → the dialogue gate, `DONT_INVESTIGATE` → the
  interest predicate only; `OnScheduleChange` (slot 435, `0x102a0940`, gated on
  `PRESERVE_PATH`) `flags1 &= 0xbbf4b97e`, `flags2 &= 0x77fff14f`, `MADE_OBLIVIOUS` cleared
  with decrement; every species override chains to it, so no classname branch. The second
  writer is a discipline HitGroup's `AI_NPCFlag`, set on apply (`0x101de660`), cleared on
  expiry (`0x101def10`, with `RemoveFromComfortList` and a schedule teardown); `MiscFlag`
  (`+0xa4`, resolver `0x1033cb00`) is the sibling. Oracle: § "The incapacitation tasks and the
  NPC flag word", § "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload",
  § "Species slot-435 overrides all chain".
- [x] **9. The interest predicate and the base condition table.** `ShouldInvestigate`
  (`0x102b3270`) with `investigate_mode(_combat)` 0–6 (default 4), rejecting
  `DONT_INVESTIGATE|IN_FLEE_SCHED`, `stay_entrenched`, null and my follower boss; the base
  table (`0x102c8ce0`, 119 entries). Oracle: § "The three `GatherConditions` sweeps and the
  interest predicate", § "The base condition table".
- [ ] **10a. The sound sweep.**
  Retail: `0x102b1cd0` over the seven sound records (incl. `Flinch` +0x6210); gate
  `m_flNextInvestigateSoundTime` +0x623c re-armed 2.0 s, 20.0 s for a stranger's sound;
  `HasCondition(HEAR_X) && (mask has HEAR_X || ShouldInvestigate)` → `INVESTIGATE_SOUND` 0x25;
  `HEAR_DANGER` skips the predicate; `HEAR_FLANK_SOUND` 0x33; `SEE_SOUND_SOURCE` 0x2d
  rate-limited by +0x6418; `CommitBestSound` (`0x102b4090`) copies the winner by priority into
  the sticky `m_InvestigateSound` +0x60dc.
  Job: the sweep in `GatherConditions`, the gate, the three conditions, `CommitBestSound`.
  Oracle: § "The three `GatherConditions` sweeps and the interest predicate", § "The
  `INVESTIGATE` family, decoded" (selection).
- [ ] **10b. The see-unknown sweep.**
  Retail: `0x102b15c0`: `m_hBestSeeUnknown` +0x6088 with 1.5 s grace +0x6084,
  `m_vecLastSeeUnknownPos` +0x6090; the one-shot `MADE_INITIAL_RESPONSE` roll over
  `m_iSeeUnknownRepeatSightings` +0x60a4 and `full_investigate` +0x6340 setting
  `ATTACK_UNKNOWN` / `IGNORE_UNKNOWN`; 2-D closing speed vs `20.0f` → `UNKNOWN_ADVANCING /
  HOLDING / RETREATING` 0x05–0x07 (`HOLDING` dead, reproduced); `INVESTIGATE_SIGHT` 0x26.
  Job: the sweep, the five conditions, the roll.
  Oracle: § "The three `GatherConditions` sweeps and the interest predicate".
- [ ] **10c. The comfort sweep.**
  Retail: `0x102b1a20`, idle only, every 0.2–0.4 s; `AddToComfortList` `0x10323630` /
  `RemoveFromComfortList` `0x10323770`, ≤1024 units, ≤3 per target; `COMFORT` 0x27;
  `BuildScheduleTestBits` withholds it while `COWERING`.
  Job: the sweep and the condition over the comfort list 8 already keeps.
  Oracle: § "The three `GatherConditions` sweeps and the interest predicate".
- [ ] **10d. The alert selectors and the ladder.**
  Retail: `SelectSchedule` (`0x102af660`) case 3 in order: see-unknown selector
  `FUN_102b8a60`, damage `FUN_102b8c40` → 0x8a, `DETECTED_ATTACK` 0x0b → 0x56, door
  obstruction `FUN_102b7370`, sound selector `FUN_102b9060`, else 0x4b `ALERT_WAIT`. The ladder
  `FUN_102b8980` on `m_eAlertLevel` +0x63f4 (saved, zeroed only at Spawn: once per life;
  `full_investigate` jumps to 3): 0 → 0x4c `ALERT_TURN_TO_SOUND`, 1 → 0x4d
  `ALERT_STEP_TOWARDS_SOUND`, 2/3 → 0x51 + (`MEMORY:INVESTIGATING` 0x8000000 ? 1 : 0). The
  third-party tail `FUN_102b8d20` (D_HT → 0x89, D_FR → 0x73). `HasInterruptCondition` needs
  the bit in the running mask; `HasCondition` does not, and every selector mixes the two. The
  hunt-state case 0xb exists only under `debug_allow_npc_hunting` (default `"0"`).
  Job: the two selectors, the ladder, the tail, the case-3 order, 0x4c/0x4d/0x4b as the
  ladder's targets; `HasInterruptCondition` on the kernel.
  Oracle: § "The `INVESTIGATE` family, decoded" (Selection).
- [ ] **10e. The sound-investigation programs.**
  Retail: 0x50, 0x51, 0x52, 0x53, 0x54, 0x58 with their task lists and interrupt sets; 0x53
  and 0x54 are dead in code, reachable by name only. No `INVESTIGATE` program declares
  `DELAY_INTERRUPTS`.
  Job: the six programs and their tasks: `STORE_LASTPOSITION` 0x17, `GET_PATH_TO_BESTSOUND`
  0x20, `WALK_RUN_PATH_COMBAT_SOUND` 0xd7, `ALERT_LOOK_AT_BEST_SOUND` 0xf9, `PLAY_SOUND` 0x11e,
  `ADD_EVENT_EXPRESSION` 0xbe, `PLAY_COWER` 0xe6, `ALERT_LOOK_AT_UNKNOWN_ATTACKER` 0x148, and
  `ALERT_LOOK_AROUND` as their fail and exit schedule.
  Oracle: § "The `INVESTIGATE` family, decoded" (The programs, verbatim; Tasks the port lacks).
- [ ] **10f. The unknown-investigation programs.**
  Retail: 0x59–0x63 with their task lists and interrupt sets; 0x61 and 0x63 dead in code;
  0x5b from `GetSchedule` (`0x102ae920`) in combat only under `ATTACK_UNKNOWN`; 0x62 and 0x58
  reached by `SET_SCHEDULE`/`SET_FAIL_SCHEDULE` only; 0x60/0x61 clear `NO_UNKNOWN_ATTACK` and
  0x62/0x63 clear `LOOKED_AT_UNKNOWN`.
  Job: the eleven programs and their tasks: `LOOK_AT_BEST_UNKNOWN` 0xfc, `UNLOOK_AT` 0xfe,
  `GET_PATH_TO_BESTUNKNOWN` 0x79, `SET_PRESERVE_PATH` 0xc4, `WALK_PATH_HUNT` 0x104,
  `CLEAR_NPC_FLAG` 0x101, `GET_PATH_TO_LASTPOSITION` 0x1c, `WALK_PATH` 0x23, `FACE_LASTANGLE`
  0x11d, `CLEAR_LASTPOSITION` 0x18, `FORGET` 0x6d, `PLAY_SEQUENCE` 0x52.
  Oracle: § "The `INVESTIGATE` family, decoded".
- [ ] **10g. The patrol programs.**
  Retail: 0x64 / 0x66 / 0x68 `INVESTIGATE_NODE` / `_WALK` / `_HUNT`; `SelectSchedule` case 1
  returns the patrol path object's own id (`m_sppPatrolPath` +0x6590 → `+4`), one of these
  three or `FOLLOW_PATROL_PATH` 0x65/0x67/0x69.
  Job: the three programs and `GET_PATH_TO_PATROL_POINT` 0x7a, `NEXT_PATROL_POINT` 0x7d,
  `FACE_PATROL_INTEREST` 0xb3, `DO_PATROL_INTEREST_ACTIVITY` 0xb5, `FACE_IDEAL` 0x2b; the
  path object's id read in the idle selector.
  Oracle: § "The `INVESTIGATE` family, decoded", § "Followers, patrols, and loitering".
- [ ] **10h. The hunt-investigation programs.**
  Retail: 0x7f, 0x80, 0x81, 0x82 and the hunt-state case 0xb order (raw `HEAR_*` accepted
  there, unlike alert). Reached in retail only by script, by name, or by `DoFrenzy` (16c).
  Job: the four programs; case 0xb behind the `"0"` default.
  Oracle: § "The `INVESTIGATE` family, decoded" (Case 0xb).
- [ ] **11. Interesting places: the selector arms.**
  Retail: arms `0xff SETUP` / `0x100 WALK` / `0x102 CROSSWALK` / `0x105 LOITER` / `0x106
  INTERACT`; the last two unreachable (no setter for `SHOULD_LOITER`, no caller pairs NPCs for
  `SHOULD_INTERACT`; do not invent one); `group_id` and `interesting_place_groups` are 1-based
  index lists → masks, empty `interesting_place_groups` matches nothing; eligibility
  `0x102dad60`; `RandomFloat(min_time, max_time)` into `m_flWaitFinished`; `Enable`/`Disable`
  on the place. The port has the step and the place entity.
  Job: each of the three reachable arms and its program compared against retail and ported;
  the masks; the wait.
  Oracle: § "Interesting places: the selector, the programs, the wait", § "Interesting-place
  eligibility".
- [ ] **12a. The reaction keyfields.**
  Retail: `percent_occluded_*` normalized at Spawn to a cumulative ladder, `_chase` forced to
  100 and never compared, rolled only in the ranged occluded selector `0x102b8320`;
  `hint_groups` index list → mask, empty = all, `FValidateHintType` slot 566;
  `stay_entrenched` (+0x6435, input `StayEntrenched`, seven "keep my cover" readers);
  `combat_start_activity` (`TASK_PLAY_COMBAT_START_SEQUENCE` in 0xeb, squad-only);
  `bright_route_penalty` parsed and never read: no NavMesh light cost.
  Job: the parse, the normalization, the readers named.
  Oracle: § "The navigation and reaction keyfields", § "The cover and kick chooser, and the
  combat leftovers". Unrecovered: what authors hint type 800.
- [ ] **12b. The cover and kick chooser.**
  Retail: `0x102b7690` gated on `CanSeekCover` slot 592 and `allow_kick_hint_use`; the
  physics-prop kick 0xa9 with its 10° predicate and the one-shot `npc_kickable` byte; the kick
  hints 0xa7/0xa8; `COND_KICK_PROP_INVALID` has no producer.
  Job: the chooser, the kick program and its hints, the kickable byte.
  Oracle: § "The cover and kick chooser, and the combat leftovers".

### The schedule host
- [x] **13. `TaskFail`** (slot 448). Base `0x10273fc0`: reason table `0x106152b0` (0x00–0x29)
  to +0x5c50, clear `m_bShouldMove`, `COND_TASK_FAILED` 0x5c. Troika `0x1029adb0` first:
  interesting-place teardown, clear `PRESERVE_PATH` unless nav type CLIMB/JUMP, motor reset,
  four think stamps := curtime, goal tolerance and interrupt distances 0, move target and kick
  prop released, `m_afMemory &= 0x0fffffff`, `flags2 &= 0x7fffe24f`, `flags1 &= 0xa3f40178`
  (`MADE_OBLIVIOUS` cleared, +0x5bb4 not decremented: retail's bounded leak, reproduced with a
  trace row), `SLEEP_BOUNDING_BOX` restore through the attack extents, `ClearHintNode(5.0)`.
  `COND_SCHEDULE_DONE` 0x5d. `TASK_STOP_MOVING`'s `FAIL_STUCK_ONTOP` 0x1c: goal active at
  StartTask, then `NAV_JUMP`, not on ground, `|v| ≤ 0.01` at RunTask. Oracle: § "`TaskFail`
  and stopped special navigation, walked".
- [ ] **13b. The leak in the defect catalogue.**
  Job: the `TaskFail` obliviousness leak as an entry in `docs/vtmb/retail-defects.md`, from
  the oracle section above.
- [x] **14. `TASK_SET_ACTIVITY` completes on a miss** (arm `0x102a1c0f`, no fail path); the
  miss is a trace row. Oracle: § "Nothing in schedule data clears these bits — the schedule
  *change* does".
- [ ] **15. The think cadence.**
  Retail: four stamps with four interval laws (`CalcNextUpdateThink` `0x10290720`, `Normal`
  `0x10290b60`, `Move` `0x10290fc0`, `AI` `0x10291230`), distance/PVS/LOS-driven, none reading
  NPC state; due when `(stamp − curtime) ≤ frametime`; `NPCThink` (`0x10292de0`) runs its
  body only when the normal think is due and passes `bReduced = !IsThinkDue(NextAI)` to
  `RunAI` (`0x1026f110`: no `GatherConditions`, `MaintainSchedule` bound 1, no clear of
  `LIGHT/HEAVY_DAMAGE` / `WAS_BUMPED`); `m_flNextThink = min(NextUpdate, NextNormal)`;
  `SCHEDULE_CHANGED`, LOS and dialogue pin normal and AI to 0.1 s; `SetPlayerLOS` at most every
  2 s with the 512-unit bypass and 8 s hysteresis. The port runs one `NextThink`, bound 10
  always, no `WasBumped`; 13 already resets the four stamps.
  Job: the four stamps and laws on the NPC, the reduced mode gating the condition pass and the
  completion bound, the pins, `WAS_BUMPED`.
  Provides: the clock 16's and 17's programs run on.
  Oracle: § "The think cadence, decoded". Unrecovered: `m_bfNPCStateFlags` bit 3 (forces
  PVS/LOS true), the subclass writers of `m_flNextAIThink` (`CNPC_VCamera`, `CNPC_VNewscaster`),
  `CAI_BaseNPC+0x98`'s entity, slot 578 (the survivor callback), slot 168 (the `GetEnemy`
  variant).
- [ ] **16a. Followers.**
  Retail: keyfields `follower_boss` (+0x6478 → `m_hFollowerBoss` +0x647c) and `follower_type`
  (+0x6480); `SetFollowerBoss` (`0x102c44e0`, refuses self and squad members) through the
  `!player`/`!self`/`!enemy` resolver and the entity input (`0x102c3350`);
  `CNPC_VPedestrian::Activate` clears it; `Npc_Follower_Info` from `Rules.txt` (`0x102c4680`:
  back-away / walk-to / run-to per type, `walkTo ≥ backAway + overlap`, `runTo ≥ walkTo +
  overlap`); slot 607 (`0x102b93c0`) as the idle selector's step between busy/choreo and patrol
  (distance² vs +0x6484 → 0x10c, +0x648c → 0x113, +0x6488 → 0x112, else 0x115); the four
  programs (blobs from `0x105e3100`) interrupting on `COND_INSIDE/OUTSIDE_INTERRUPT_DIST_F`
  0x19/0x18, produced via `GetFollowerBoss` slot 293; tasks 0x86–0x88, fail 0x29 when the boss
  is dead. The port has a comment stub in the idle selector and a body-owner enum value the
  mind refuses.
  Job: the keyfields and setter, the rules table, the idle step, the two conditions, the four
  programs and three tasks.
  Oracle: § "`m_hFollowerBoss` — the follower controller", § "Followers, patrols, and
  loitering".
- [ ] **16b. The composed relationship and the human ideal state.**
  Retail: `IRelationType` (`0x10299da0`): self → D_ER; a `D_INSANE` target with my closest
  player not hated and not my enemy → D_HT; target's boss hated or my enemy → D_HT; my boss ==
  target → D_LI; else inherit `boss->IRelationType(target)`, upgraded to D_HT if either hates
  or targets the other; else the base table. `CNPC_VHuman::SelectIdealState` (`0x103851e0`):
  enemy gone → follower to alert (idle under `no_alert_state`), non-follower to hunt only under
  `debug_allow_npc_hunting` `"0"`. The player's action state (`0x101755d0`) reads a follower
  as ally. The port has a flat relationship table.
  Job: the composition routed through the feed guard and `GatherSight`; the ideal-state arm;
  the ally read.
  Provides: the composed relation to 0006, 0007 and the target HUD.
  Oracle: § "`m_hFollowerBoss` — the follower controller", § "Relationship table, exactly
  decoded".
- [ ] **16c. Possession and frenzy.**
  Retail: `Dominate_Possession`'s `DoPossession` byte runs `0x102c51a0`: squad disconnect,
  `SetEnemy(NULL)`, `flags2 |= D_POSSESSED | D_DISCONNECT_SQUAD`, `"player D_LI 99"`,
  `SetFollowerBoss(caster)` + `SetFollowerType("Combat")`, ideal state 1, target/friend =
  caster, `frenziedFlags = 0x3b1c`, acquire the nearest hated entity (`0x102b4cc0`). `DoFrenzy`
  (`Dementation_Berserk` / `_Bedlam`) runs `0x102c5310`: same teardown, `D_INSANE`, hunt state,
  investigate modes 6, `frenziedFlags = 0x9fbd`, no follower. The HitGroup's `AI_Schedule`
  installs before either. The port parses and carries both bytes.
  Job: both arms executed on apply, over 16a and 17.
  Consumes: the HitGroup apply path from 0007.
  Oracle: § "Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload".
  Settled as not in the image: the `m_bfNPCFrenziedFlags` bit names.
- [ ] **17. Squads.**
  Retail: one shared `AI_Enemies` memory. Joining (`squadname` + `bits_CAP_SQUAD`, `InitSquad`
  `0x10273d30` / `SetSquad` `0x1029a930`) points `m_pEnemies` at `squad+8`; `GetEnemies()`
  (`0x10273e10`) diverts to the global `g_DisconnectedEnemies` while `m_iSquadDisconnected` (a
  refcount) is nonzero; `DisconnectFromSquad` (`0x1026d050`) wipes that global and increments;
  `ReconnectToSquad` (`0x1026d0c0`) decrements, re-adds at 0, clears `D_DISCONNECT_SQUAD`;
  `LeaveSquad` is empty. `COND_SQUAD_SEE_ENEMY` 0x31 producer `0x102b2730` (someone sharing
  my memory saw him in the last 0.2 s); `TASK_SQUAD_NEW_ENEMY` 0x138 → `SquadNewEnemy`
  `0x103161a0`; `TASK_DISCONNECT_FROM_SQUAD` 0xf5. 16 members, the 17th overwrites the 16th;
  `GetMember` returns NULL for all when member 0 is disconnected; `Event_Killed` compacts;
  membership rebuilt on restore from `squadname`. `SQUAD_NEW_ENEMY` and
  `IGNORE_SQUAD_SEE_ENEMY` have no readers (drop the port's clear under the latter); the
  strategy-slot namespace ships dead (do not build).
  Job: the squad object sharing 5's record store, the disconnect refcount replacing the seams
  in 7 and 8, the condition, the two tasks, the `SquadSeesPlayer` stub replaced.
  Oracle: § "Squads, decoded". Unrecovered: `m_iMySquadSlot`'s offset.
- [x] **19. Jump links.** `NavLinkProxy`s from the decoded `.ain` links with retail's
  disabled-bit and capability filter; the motor reports `Jump` while traversing one and
  `Ground` before arrival or failure. Oracle: `navigation-jump-links.md`; the Unreal flight
  service is a named modernization in `docs/decisions.md` (2026-09-08).

### Incapacitation
- [x] **20. The trance.** `FeedInterrupt` (`0x1033a9e0`) on a victim with `BloodPool ≥ 1` and
  `IRelationType(attacker) != D_HT`: think timers := curtime, `SetSchedule(0xfb
  SCHED_TROIKA_MESMERIZED)` (`MAKE_OBLIVIOUS TRUE; SET_NPC_FLAG D_IS_BUSY, DONT_INVESTIGATE,
  NO_DIALOG; SET_ACTIVITY ACT_DISPOSITION_MESMERIZED; WAIT 30; WAIT_RANDOM 120`, damage
  interrupts, `DELAY_INTERRUPTS`); after `LeaveGrappleState`; replaced by 0x6b when
  `IsBusyWithDiscipline()`. `DELAY_INTERRUPTS` is one think of immunity re-armed by every
  install; `SetSchedule` zeroes the condition set; `MaintainSchedule` bound 10; effective mask
  = authored ∪ `BuildScheduleTestBits` ∪ `NPC_FREEZE`. Oracle: § "Incapacitation, feeding,
  grapple, and death", § "`DELAY_INTERRUPTS`, decoded", § "`SetSchedule` clears the condition
  set".
- [ ] **21a. The flee state.**
  Retail: `m_NPCState == 8`, entered only in `CAI_BaseNPCTroika::SelectIdealState`
  (`0x102ad660`) from idle and alert on `COND_SUPERNATURAL_FLEE_LEVEL` 0x21 or
  `COND_CRIMINAL_FLEE_LEVEL` 0x1f, setting `INITIAL_FLEE` 0x100; terminal (case 8 returns 8),
  `m_bfNPCStateFlags = 0x85`. `SelectSchedule` case 8 in order: `COVER_FAILURE` 0x39 → 0x73;
  no stimulus → (`DETECTED_ATTACK` → 0x56; `INVESTIGATE_SOUND` → re-arm, clear `INITIAL_FLEE`,
  `CommitBestSound`, 0x48; else 0x77); `INITIAL_FLEE` clear → 0x73; else the one-shot pass:
  clear it, `m_flNextFleeSoundTime = curtime + RandomFloat(10, 20)`, the flee vocalisation
  (slot +0x7c8), damage → 0x72, `SEE_FEAR` without a law level → 0x72, the supernatural then
  criminal branch (offender, `m_vSavePosition`, 0x76 / 0x73, `MakeAISound(type 8, player eye,
  radius and type from `sound_volume_table.txt`, 10.0)`, `ReportSupernaturalAct` `0x1017f4a0`
  / `ReportCriminalAct` `0x1017f2a0`, `m_flPlayerDist < 512.0 && RandomInt(0,99) < 80` → 0x71
  else 0x70). `TranslateSchedule` (`0x102b12f0`): 0x77 → 0x78 when the hint node is type
  0x2774; 1/0x6b → 0x132 under `D_MILDLY_CRAZY`.
  Job: the state, the ideal-state entry, the case-8 chain, the translation.
  Oracle: § "The flee state and the cower, disoriented and lost programs". Unrecovered: the
  criminal level `+0x6364`, the `sound_volume_table.txt` rows for the flee sound, hint type
  names 0x2774/0x27d8, the base `SCHED_COWER` id.
- [ ] **21b. The cower, disoriented and lost programs.**
  Retail: 0x70/0x71 `FLEE_AND_COWER_TURN_TO_PLAYER(_NEAR)`, 0x72 `_SCREAM`, 0x73
  `FLEE_AND_COWER`, 0x74/0x75 `_STALL(_FAILED)`, 0x76 `_NO_ENEMY`, 0x77/0x78 `COWER(_HINT)`,
  0x109/0x10a/0x10b `COWER_SIMPLE(_HINT/_NOSEE)` (`SEE_FEAR` listed twice, verbatim),
  0x12d `DISORIENTED` / 0x12e `LOST` (`DELAY_INTERRUPTS`, mask `{NPC_FREEZE}`, `WAIT_PVS`).
  `DISORIENTED` is the terminal schedule of 16 discipline programs; `LOST` has no producer.
  `TASK_PLAY_COWER` rolls `m_iCowerAnimOffset` +0x6414 = `RandomInt(0,2) × 3`, `SET_COWER`
  reuses it. `IN_FLEE_SCHED` set by every flee leg and cleared first by `COWER_SIMPLE*`.
  Job: the fourteen programs and their tasks: `SUGGEST_STATE` 0x06, `GET_PATH_TO_RANDOM_NODE`
  0x1f, `FACE_HINTNODE` 0x2f, `GET_PATH_TO_COWER_NODE` 0x84, `_SAVE_POS` 0x85, `PAUSE_MOVING`
  0xa6, `PLAY_COWER` 0xe6, `SET_COWER` 0xe7, `LOOK_AT_PLAYER` 0xfb, `RUN_PATH_FLEE` 0x103,
  `FLIP_NEXT_IDEAL_YAW` 0x106, `WAIT_PVS`; the 0xe1/0xe3 completion branch 8 left waiting.
  Oracle: § "The flee state and the cower, disoriented and lost programs".
- [ ] **21c. The incapacitated victim's consumers.**
  Retail: `AttemptFeed` (`0x10168910`) reads the victim's ideal activity (+0xff0), auto-accepts
  on `ACT_DISPOSITION_MESMERIZED 0x104e / ACT_DISORIENTED 0x1068 / ACT_LOST 0x1069 / ACT_COWER
  0x1098` only, so a cowering NPC auto-accepts one time in three. `ONE_HIT_KILL` (bit 30) has
  one reader, `OnTakeDamage` `0x102beda0`: any non-light hit kills outright. `COWERING` (bit
  10) withholds `COMFORT` in `BuildScheduleTestBits` and quarters hearing.
  Job: `IsFeedAutoAcceptState` answering from the ideal activity instead of the disposition
  name; the `ONE_HIT_KILL` read in the damage path; the `COWERING` overlay read.
  Provides: the `ONE_HIT_KILL` seam to 0006's damage.
  Oracle: § "The flee state and the cower, disoriented and lost programs" (Activities and the
  feed; Flags the family writes); `feeding.md` § "Step 4, decoded".

### Content and tooling
- [ ] **22. `sp_tutorial_1` on the V2 lane.**
  Job: the map baked on the V2 lane so the light query reads its 396 worldlights (1) and the
  nine hull-0 Jump links `22, 24, 30, 88, 110, 115, 147, 163, 218` exist as link actors (19).
  Until then 13's stuck-on-top failure is unreachable in the tutorial.
- [ ] **23. The NPC debugger** (visual-only modernization, out of Shipping, reads only).
  Job: an `FGameplayDebuggerCategory` "ElysiumNPC" whose `CollectData` packs mind state,
  schedule/task, conditions, enemy memory, sense radii and the trace, and whose `DrawData`
  draws the vision circle and cone, hearing radius, enemy LOS line and held place; an
  `IVisualLoggerDebugSnapshotInterface` on the body actor with `UE_VLOG` shapes at sense
  admission, enemy choice and schedule install.

## Seams
- Provides: the awareness seam (`Cognition.Conditions`, the enemy memory, `Senses.Memory`,
  `IsOblivious()`, `ShouldInvestigate`) to 0006 and 0008; the stealth scalars; the failure path
  (13) and the cadence (15) every later program family runs on (0006, 0007); the composed
  relation (16b) to 0006, 0007 and the target HUD; the trance and the flag word to the feed
  and dialogue gates (0004).
- Consumes: the HitGroup apply path and the cloak/detection-record producers from 0007 (6,
  16c); the grapple state machine and damage from 0006 (3c, 21c).
