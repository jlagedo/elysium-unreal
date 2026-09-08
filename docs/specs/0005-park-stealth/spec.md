# 0005 park-stealth — NPC AI: sensing, the enemy memory, the schedule host, and the tutorial's stealth lessons

## Witness
`sp_tutorial_1`'s stealth lessons played against retail. The sneak-past lesson on `thug_1`: he
idles at his interesting place, hears the player's footsteps and walks the alert ladder to look,
sees the player only inside `540 ×` the light scalar and the 157° cone, and only then commits an
enemy and fires `OnFoundPlayer` into the "spotted" sign. The stealth-kill lesson on
`stealth_victim`. The post-feed trance on any civilian.

## Scope
The whole of NPC AI is one system and lives here: everything an NPC senses, remembers, decides,
schedules and navigates, in retail's order. Other specs witness fights or lessons against it.

- Owned here:
  - **Stealth**: the player's light and sneaking target surface, `trigger_stealth_mod`, the
    observer transaction, the stealth-kill transaction, the player's reserved movement sound and
    the door sound (the hearing producers), the HUD light gauge's producer.
  - **Senses and memory**: sight, hearing, the enemy memory, the conditions layer, the interest
    predicate, the three condition sweeps and the `INVESTIGATE` programs, the ideal-state pass.
  - **Decision and programs**: the schedule host (failure virtual, think cadence, forced-program
    pre-emption), enemy choice, the alert and combat families, `aiscripted_schedule`, flinch and
    reactions, door obstruction and `Prone`, interesting places, patrols, followers, squads.
  - **Navigation contract**: the nav type the AI reads (ground / jump / climb), goal tolerance,
    arrival and failure reasons, and jump links emitted from the `.ain` node graph as
    `NavLinkProxy`s.
  - **Incapacitation**: the post-feed trance, the NPC flag word, obliviousness, the disposition
    auto-accept states, the NPC-side response to a discipline (possession, frenzy, `AI_NPCFlag`).
- Out of scope, each owned by a named spec and consumed here:
  - Weapons, damage, the health commit, death, ragdoll — **0006** (witnesses the first fight
    against this AI).
  - Firearms and ranged spread — **0009**. Disciplines' own effects and costs — **0007**.
  - Conversation UI and the `.dlg` runtime — **0004**; this spec provides the `NO_DIALOG` refusal
    and the dialogue partner's own sense freeze.
  - The Play-tier beat driver — **0000**.
  - Unreal's queries (traces, light sampling, NavMesh pathfinding, animation playback): used, not
    reproduced (see Design).

## Reading list
Oracle documents, with the sections to open first:
- `docs/vtmb/npc-ai-reverse-engineering.md` — the NPC AI oracle. Sections: "The idle branch,
  decided"; "Enemy acquisition and replacement"; "The incapacitation tasks and the NPC flag
  word"; "`MaintainSchedule`, walked"; "`m_hFollowerBoss`"; "The three `GatherConditions`
  sweeps"; "The base condition table"; "`BuildScheduleTestBits`"; "The sense pass for a hated
  player, walked"; "The enemy memory — `CAI_Memory`"; "Dialogue does not gate bystanders";
  "Hearing, walked"; "`ambient_generic` as an AI sound source"; "Interesting places: the
  selector, the programs, the wait"; "The `INVESTIGATE` family, decoded"; "Disciplines that
  possess or frenzy an NPC"; "Sense and investigate leftovers, closed"; "The navigation and
  reaction keyfields".
- `docs/vtmb/stealth.md` — the target surface, the observer transaction, the stealth kill, "The
  light query, recovered".
- `docs/vtmb/feeding.md` ("Step 4, decoded"), `docs/vtmb/footsteps.md` (§2.5/2.6, the player's
  reserved sound), `docs/vtmb/entity_io.md`, `docs/vtmb/activity_enum.md`,
  `docs/vtmb/animation_and_movers.md` (the disposition stance machine), `docs/vtmb/disciplines.md`,
  `docs/vtmb/lighting.md` (lump 15, lightstyles), `docs/vtmb/vdata-catalog.md` (`stealth.txt`,
  `sound_volume_table.txt`, `interestingplacetypelist.txt`, `Rules.txt`, `npctemplate*.txt`),
  `docs/vtmb/sp_tutorial_1-event-surface.md` (§2, §6–7, §11).
- Authored data: the **V2 seams** under `$ELYSIUM_WORK_ROOT/exports_v2/` —
  `maps/sp_tutorial_1.entities.glb` (GLB; 12-byte header, chunk length at byte 12, JSON at 20;
  extension `ELYSIUM_vtmb_map_entities`: `entities[].keyValues[{key,value,sourceOffset}]`,
  `outputs[{target,input,parameter,delay}]`, anomalies, lump sha256), `maps/
  sp_tutorial_1.lighting.glb` (worldlights), `nav-graphs/sp_tutorial_1.glb` (`.ain` links),
  `vdata/`. Cite these, not the legacy `exports/<map>/<map>.ents`.

Port entry points: `Source/ElysiumUE/Private/Substrate/` — `ElysiumNpc.{h,cpp}` (`Think()`),
`ElysiumSchedule.{h,cpp}` (the kernel), `ElysiumNpcSenses`, `ElysiumNpcEnemy`,
`ElysiumNpcConditions`, `ElysiumNpcFlags`, `ElysiumStealth`, `ElysiumFeedSchedules`,
`ElysiumNpcMaker`, `ElysiumInterestingPlace.cpp`, `ElysiumDisciplines.cpp` /
`ElysiumDisciplineTargetTables.cpp` (the carried `AI_NPCFlag`/`DoPossession`/`DoFrenzy`);
`Map/ElysiumMapActor.cpp` (`QueryLightAtPoint`); `UI/ElysiumHUDModel.cpp` (the stealth view).
Tests under `Source/ElysiumUE/Private/Tests/`.

Recovery tooling, for the arms still to decode: the `vtmb-corpus` MCP server (load with
`ToolSearch("select:mcp__vtmb-corpus__vtmb_grep,...vtmb_func,...vtmb_code,...vtmb_asm,
...vtmb_callers,...vtmb_callees,...vtmb_string,...vtmb_fields,...vtmb_readers,...vtmb_slot,
...vtmb_vtable,...vtmb_globals")`; `vtmb_readers` is the "nothing else touches this" proof but
misses index-form accesses — cross-check with `vtmb_grep` on `[0x<offset/4>]`; vtable dumps cap
at 600 slots, read higher slots from the DLL (`$ELYSIUM_VTMB_ROOT/Vampire/dlls/vampire.dll`,
base `0x10000000`) with a PE section walk. Schedule blobs are NUL-terminated ASCII
`"\n\tSchedule\n\t\tSCHED_…\n\n\t\tTasks\n\t\t…\t\tInterrupts\n\t\t…"` — byte-scan them; ids come
from the registrar `FUN_102b9810` (`mov [esp+0x10], name; mov [esp+0x14], id; call 0x1001528a`
right before the blob's `call 0x1000ce8c`). Task arms live in jump tables the decompiler drops:
`CAI_BaseNPCTroika::StartTask` `0x102a1910` (idx = id−5, byte table `0x102a7ab8`, dword table
`0x102a77f8`), `CAI_BaseNPC::StartTask` `0x102827f0` (idx = id−1, `0x10287138`/`0x10286f8c`),
`CAI_BaseNPC::RunTask` `0x10288780` (idx = id−2, `0x10289794`/`0x10289724`),
`CAI_BaseNPCTroika::RunTask` `0x102aacf0` (idx = id−2, `0x102ac844`/`0x102ac760`). Registrars:
tasks `FUN_10316ff0` (ids 0–276) and `0x1031918a` (277–329); conditions `0x102c8ce0`; activities
`FUN_104126e0`; NPC flags `0x1030cbd0`; schedule flags `0x1030d7e0`.

## The witness NPC: `thug_1`
Child of `npc_maker` `thug_maker` (`NPCType npc_VVampire`, `NPCTargetname thug_1`,
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

**`stealth_victim`** — child of `stealth_victim_maker`, same model and template, `player_reaction
D_NU 0`, `item_w_fists`, `vision -1`, `hearing -1`, `allow_alert_lookaround 0`,
`interesting_place_groups 8`, all `pl_*` 6; `popup_39.OnUseEnd → SetRelationship player D_HT 5`;
`OnDeath → thug_maker_4.Spawn`. Its lesson is requirement 3.

Not tutorial content: the seven Society hunters and `sentry2` at the far end of the BSP are an
Unofficial Patch level-select hub; never use them as a witness.

## Requirements
Ordered by build. Each carries its retail source, the port state, and what to build.

**Stealth**
1. **Target surface.** Light row and `Sneaking` publish the vision, cone and hearing scalars;
   `trigger_stealth_mod` is a balanced overlap modifier. *Landed* (`ElysiumStealth`). *Open:* the
   light query. Retail is a live per-worldlight evaluation (`stealth.md`, "The light query,
   recovered"): Source falloff by light type, the cone/angle term, the live lightstyle value,
   occlusion by a trace only world geometry and unflagged static props block, the first sun via a
   sky trace, luminance `0.30/0.59/0.11`, unclamped sum clamped to `[0, 1]`. Also to match: the
   eligibility predicate (`FL_DUCKING` and unseen by any `D_HT` NPC for 1.0 s), the `-1.0`
   sentinel, sampling that runs even when ineligible, the three sample points off the world AABB.
   The port's `QueryLightAtPoint` sums with a flat falloff, skips the sun and traces nothing.
2. **The light gauge producer.** Publish the light row into `FElysiumStealthView` (validity true
   once a sample exists; today only the HUD preview fixture sets it). Retail (`stealth.md`, "The
   HUD light gauge, decoded") networks the row and the client draws a continuous bottom-up fill,
   29.7 px per row on a 768-high layout, full = lit, empty = dark, eased toward `row × 10` by ±3
   per painted frame, drawn only while crouched; the five `lightgauge_%d` frames are dead in
   retail. The HUD is this project's own asset (five-step gauge): the producer publishes the
   row, the presentation maps 11 rows to its steps.
3. **Stealth-kill transaction.** `StealthKillRules.txt`, the deaf arc / approach depth, grapple
   mode 3 (`stealth.md`, RE50 section). Recovered, unimplemented. Consumes obliviousness (7).
4. **The committed observer snapshot** for the HUD (`stealth.md`, "HUD observability is not
   authority"): gameplay publishes after the state commits; the HUD never runs detection.

**Senses and memory**
5. **The enemy memory** (`CAI_Memory`) — **first build.** Record store `mem+0xc` (handle, last
   position, anchor, velocity, last-seen time, nav nodes, position-only byte, eluded byte),
   written by `UpdateEnemyMemory` (slot 544, `0x102709c0`, no override) from `OnLooked`'s
   `D_HT`/`D_FR` arms; `RefreshMemories` (`0x102df320`) drops only dead or invalid handles, no
   time expiry; `BestEnemy` (`0x102743c0`) walks this list only. Port: a record store on the NPC
   written by `GatherSight`'s hate/fear arms, read by `ElysiumNpcEnemy::BestEnemy` in place of the
   world-list walk; review `IsScheduleInterested`'s "no schedule → interested" arm against
   retail's always-installed schedule.
6. **Sight and hearing details.** The 3072-unit prefilter and cadences (players 0.15 s, NPCs
   0.25 s, objects 0.45 s); `m_flFieldOfView 0.2` (replaces the port's chosen `DefaultViewConeDot
   0.5`); `SEE_PLAYER = 0x5a`; the `IRelationPriority` split `<0` DISLIKE / `0..10` HATE / `≥11`
   NEMESIS; `FinViewCone3dNew`'s two tests (strict front test, then the cosine from an apex
   pulled back along forward by an unnamed ConVar, times the target's cone scalar, against
   `m_flFieldOfView`); the combat-state range bypass; the concealment test `0x10146b20`; the outer-band
   attention path `0x102b3e00` (`SEE_UNKNOWN`, `OnUnknownVisionPlayer`, the run/start timers);
   hearing as radius × `hearing`, the delayed conditions `RandomFloat(0.2, 0.9)`, the deaf-arc
   suppression, the cowering/sleeping quarter radius, the seventh (`Flinch`) record and
   `CommitBestSound`'s priority; `ambient_generic` `sound_event` (raw type bitmask) /
   `sound_event_level` (1..3 → 180/240/1200, non-occludable, once per activation). *Landed:*
   the effective radius and outer band, the 2 s LOS cache with the 512-unit bypass and 8 s grace,
   the ten-failure debounce and the `OnFoundPlayer` edge, the sound-radius reduction, the
   observer's `vision`/`hearing` sentinel resolution.
7. **Obliviousness.** `TASK_MAKE_OBLIVIOUS` 0x131: `flags2 |= 0x80001000`, `SetEnemy(NULL)`,
   squad disconnect (`0x1026d050`, a named seam until 17), `++m_iIsOblivious` (+0x5bb4, saved),
   `OnIncapacitatedStart`. Consumers: `PerformSensing` skips the sense pass; `UpdatePoseParameters`
   (slot 314) drops aim; slot 587 rejects; `FindVictim` allows backstab from any angle. *Landed*
   except the backstab consumer (3).
8. **The NPC flag word.** `m_bfAINPCFlags` (+0x14b8) / `m_bfAINPCFlags2` (+0x14bc), 62 names
   (`0x1030cbd0`); `TASK_SET/CLEAR_NPC_FLAG` 0x100/0x101; `D_IS_BUSY` → `IsBusyWithDiscipline`
   only, `NO_DIALOG` → the dialogue gate, `DONT_INVESTIGATE` → the interest predicate only;
   `OnScheduleChange` (slot 435, `0x102a0940`, gated on `PRESERVE_PATH`) `flags1 &= 0xbbf4b97e`,
   `flags2 &= 0x77fff14f`, `MADE_OBLIVIOUS` cleared with decrement. *Landed*
   (`ElysiumNpcFlags`). *Open:* the second, non-task writer — a discipline HitGroup's `HitInfo`
   `AI_NPCFlag` key, set on apply (`0x101de660`) and cleared on expiry (`0x101def10`, with
   `RemoveFromComfortList` and a schedule teardown); the port carries it warned; execute through
   `ParseName` + `Set`/`Clear`. `MiscFlag` (`+0xa4`, resolver `0x1033cb00`) is the sibling.
9. **Interest and conditions.** `ShouldInvestigate` (`0x102b3270`) with `investigate_mode(_combat)`
   0–6 (default 4), rejecting `DONT_INVESTIGATE|IN_FLEE_SCHED`, `stay_entrenched`, null and my
   follower boss; the base condition table (`0x102c8ce0`, 119 entries). *Landed.* Consumers of
   the predicate are 10.
10. **The three condition sweeps and the `INVESTIGATE` programs.** See-unknown `0x102b15c0`
    (`m_hBestSeeUnknown` +0x6088, 1.5 s grace +0x6084, `m_vecLastSeeUnknownPos` +0x6090, the
    one-shot `MADE_INITIAL_RESPONSE` roll over `m_iSeeUnknownRepeatSightings` +0x60a4 and
    `full_investigate` +0x6340 setting `ATTACK_UNKNOWN`/`IGNORE_UNKNOWN`, 2-D closing speed vs
    `20.0f` → `UNKNOWN_ADVANCING/HOLDING/RETREATING` 0x05–0x07 with the dead `HOLDING`
    reproduced, `INVESTIGATE_SIGHT` 0x26); sound `0x102b1cd0` (seven records incl. `Flinch
    +0x6210`, gate `m_flNextInvestigateSoundTime` +0x623c re-armed 2.0 s / 20.0 s for a
    stranger's sound, `HasCondition(HEAR_X) && (mask has HEAR_X || ShouldInvestigate)` →
    `INVESTIGATE_SOUND` 0x25, `HEAR_DANGER` skips the predicate, `HEAR_FLANK_SOUND` 0x33,
    `SEE_SOUND_SOURCE` 0x2d rate-limited by +0x6418); comfort `0x102b1a20` (idle only, 0.2–0.4 s,
    `AddToComfortList` `0x10323630` / `Remove` `0x10323770`, ≤1024 units, ≤3 per target,
    `COMFORT` 0x27). The 31 programs with ids, the alert selectors `FUN_102b9060`/`FUN_102b8a60`,
    the alert ladder `FUN_102b8980` on `m_eAlertLevel` +0x63f4 (saved, zeroed only at Spawn:
    once per life), `CommitBestSound` `0x102b4090` and the sticky `m_InvestigateSound` +0x60dc,
    `MEMORY:INVESTIGATING` 0x8000000, and the 26 tasks the port lacks — all in the oracle's
    "`INVESTIGATE` family, decoded". `HasInterruptCondition` (needs the bit in the running mask)
    vs `HasCondition` is load-bearing in every selector. 0x53, 0x54, 0x61, 0x63 are dead in
    code. The hunt-state case 0xb is script-only in retail (`debug_allow_npc_hunting` defaults
    `"0"`). *Open:* all of it; the predicate exists. Five of the 26 task arms are summarized in
    the oracle; the rest are read from the jump tables above.
11. **Interesting places.** Selector arms `0xff SETUP` / `0x100 WALK` / `0x102 CROSSWALK` /
    `0x105 LOITER` / `0x106 INTERACT` (the last two unreachable in retail: no setter for
    `SHOULD_LOITER`, no caller pairs NPCs for `SHOULD_INTERACT`; do not invent one); both
    `group_id` and `interesting_place_groups` are 1-based index lists → masks (empty
    `interesting_place_groups` matches nothing); eligibility `0x102dad60`; `RandomFloat(min_time,
    max_time)` into `m_flWaitFinished`; `Enable`/`Disable` on the place; the programs' task
    lists in the oracle. *Landed:* the step and the place entity. *Open:* verify each arm.
12. **The reaction keys**, ported verbatim from the oracle's "navigation and reaction keyfields":
    `percent_occluded_*` (Spawn normalizes to a cumulative ladder, `_chase` forced to 100 and
    never compared; rolled only in the ranged occluded selector `0x102b8320`), `hint_groups`
    (index list → mask, empty = all; `FValidateHintType` slot 566; the hint-type table),
    `allow_kick_hint_use` (the cover/kick chooser `0x102b7690` gated on `CanSeekCover` slot 592;
    the physics-prop kick `0xa9` with its 10° predicate and the one-shot `npc_kickable` byte; the
    kick hints `0xa7`/`0xa8`; `COND_KICK_PROP_INVALID` has no producer),
    `stay_entrenched` (keyfield +0x6435, input `StayEntrenched`, seven "keep my cover" readers,
    all decoded in the oracle's "cover and kick chooser" section),
    `combat_start_activity` (`TASK_PLAY_COMBAT_START_SEQUENCE` in `0xeb`, squad-only).
    `bright_route_penalty` is unconsumed in retail: parse, never read, no NavMesh light cost.

**The schedule host**
13. **`TaskFail`** (slot 448). Base `0x10273fc0`: reason to +0x5c50 (table `0x106152b0`: `0x05
    "Schedule not found"`, `0x17 "No player"`, `0x1c "Stuck on top of something"`, `0x29 "NPC had
    no follower boss"`), clear `m_bShouldMove`, DevMsg, `SetCondition(COND_TASK_FAILED 0x5c)`.
    Troika `0x1029adb0` first: interesting-place teardown (`0x102b53d0`), clear `PRESERVE_PATH`
    unless nav type CLIMB/JUMP, motor speed/yaw reset, all four think stamps := curtime,
    `m_flGoalTolerance = 0`, interrupt distances 0, `m_hMoveTargetEnt` cleared, kick prop
    released, `m_afMemory &= 0x0fffffff`, `flags2 &= 0x7fffe24f`, `flags1 &= 0xa3f40178`,
    `SLEEP_BOUNDING_BOX` restore, `ClearHintNode(5.0)`, `m_bPatrolPathUseHint = 0`. Both masks
    verbatim: `MADE_OBLIVIOUS` cleared, +0x5bb4 **not** decremented (retail's bounded leak,
    reproduced; write a trace row when the bit was set). Add `COND_SCHEDULE_DONE` 0x5d, and
    `TASK_STOP_MOVING`'s `FAIL_STUCK_ONTOP` (`0x10288963`: active goal, `NAV_JUMP`, not on
    ground, `|v| ≤ 0.01`) against the body's flight state (`AElysiumNpcBody::Launch`). *Today:*
    the kernel's `Failed` writes a trace row and routes to the fail schedule; `StopMoving`
    returns void and cannot fail; no nav type exists anywhere.
14. **`TASK_SET_ACTIVITY` completes on a miss** (arm `0x102a1c0f`, no fail path). Complete,
    record the miss in the trace, let the body's ladder answer; then drop `bNpcActivitiesResolve
    = true` from the trance fixture. *Today:* the task fails on an unresolvable activity, and 24
    test sites force resolution.
15. **The think cadence** (the VM clock; oracle "The think cadence, decoded"). Four stamps
    with four interval laws (`CalcNextUpdateThink 0x10290720`, `Normal 0x10290b60`, `Move
    0x10290fc0`, `AI 0x10291230`), all distance/PVS/LOS-driven, none reading the NPC state; the
    due test is `(stamp − curtime) ≤ frametime`; `NPCThink` (`0x10292de0`) runs its body only when
    the normal think is due, passes `bReduced = !IsThinkDue(NextAI)` to `RunAI` (`0x1026f110`:
    no `GatherConditions`, `MaintainSchedule` bound 1, no clear of `LIGHT/HEAVY_DAMAGE`/
    `WAS_BUMPED`), and sets `m_flNextThink = min(NextUpdate, NextNormal)`; `SCHEDULE_CHANGED`,
    LOS and dialogue pin normal and AI to 0.1 s; `TaskFail` resets all four `Next` stamps;
    `SetPlayerLOS` runs at most every 2 s with the 512-unit bypass and 8 s hysteresis. *Today:*
    one `NextThink`, bound 10 always, no `WasBumped`. Build before followers so their programs
    run on the right clock.
16. **Followers.** Keyfields `follower_boss` (+0x6478 → `m_hFollowerBoss` +0x647c) and
    `follower_type` (+0x6480); `SetFollowerBoss` (`0x102c44e0`; refuses self and squad members)
    through the `!player`/`!self`/`!enemy` resolver, the entity input (`0x102c3350`),
    `CNPC_VPedestrian::Activate` clears it; `Npc_Follower_Info` from `Rules.txt` (`0x102c4680`:
    back-away / walk-to / run-to per type, `walkTo ≥ backAway + overlap`, `runTo ≥ walkTo +
    overlap`); slot 607 (`0x102b93c0`) as `SelectIdleSchedule`'s step between busy/choreo and
    patrol (distance² vs +0x6484 → `0x10c`, +0x648c → `0x113`, +0x6488 → `0x112`, else `0x115`);
    the four programs (blobs from `0x105e3100`, interrupting on `COND_INSIDE/OUTSIDE_INTERRUPT_
    DIST_F` 0x19/0x18 — build that producer via `GetFollowerBoss` slot 293); tasks `0x86–0x88`
    (fail 0x29 when the boss is dead). `IRelationType` (`0x10299da0`): self → D_ER; a `D_INSANE`
    target with my closest player not hated and not my enemy → D_HT; target's boss hated or my
    enemy → D_HT; my boss == target → D_LI; else inherit `boss->IRelationType(target)`, upgraded
    to D_HT if either hates or targets the other; else the base table — routed through the feed
    guard and `GatherSight`. `CNPC_VHuman::SelectIdealState` (`0x103851e0`): enemy gone →
    follower to alert (idle under `no_alert_state`), non-follower to hunt only under
    `debug_allow_npc_hunting` (default `"0"`, port with that default). The player's action state
    (`0x101755d0`) reads a follower as ally. Discipline producers: `Dominate_Possession`'s
    `DoPossession` byte runs `0x102c51a0` (squad disconnect, `SetEnemy(NULL)`, `flags2 |=
    D_POSSESSED | D_DISCONNECT_SQUAD`, `"player D_LI 99"`, `SetFollowerBoss(caster)` +
    `SetFollowerType("Combat")`, ideal state 1, target/friend = caster, `frenziedFlags = 0x3b1c`,
    acquire the nearest hated entity `0x102b4cc0`); `DoFrenzy` (`Dementation_Berserk`/`_Bedlam`)
    runs `0x102c5310` (same teardown, `D_INSANE`, hunt state, investigate modes 6,
    `frenziedFlags = 0x9fbd`, no follower); the HitGroup's `AI_Schedule` installs before either.
    *Today:* a comment stub in the idle selector, a body-owner enum value refused by the mind, a
    flat relationship table, both discipline bytes parsed and carried.
17. **Squads** (oracle "Squads, decoded"). The coupling is one shared `AI_Enemies` memory: joining
    (`squadname` + `bits_CAP_SQUAD`, `InitSquad 0x10273d30` / `SetSquad 0x1029a930`) points the
    NPC's `m_pEnemies` at `squad+8`; `GetEnemies()` (`0x10273e10`) diverts to the global
    `g_DisconnectedEnemies` while `m_iSquadDisconnected` (a refcount) is nonzero;
    `DisconnectFromSquad` (`0x1026d050`) wipes that global and increments;
    `ReconnectToSquad` (`0x1026d0c0`) decrements, re-adds to the squad memory at 0, clears
    `D_DISCONNECT_SQUAD`; `LeaveSquad` is an empty stub. `COND_SQUAD_SEE_ENEMY 0x31` producer
    `0x102b2730` ("someone sharing my memory saw him in the last 0.2 s"); `TASK_SQUAD_NEW_ENEMY
    0x138` → `SquadNewEnemy 0x103161a0`; `TASK_DISCONNECT_FROM_SQUAD 0xf5`. 16 members with the
    17th-overwrites-16th bug; `GetMember` returns NULL for all when member 0 is disconnected;
    `Event_Killed` compacts; membership rebuilt on restore from `squadname`. **`SQUAD_NEW_ENEMY`
    and `IGNORE_SQUAD_SEE_ENEMY` have no readers** (drop the port's clear under the latter); the
    strategy-slot namespace ships dead (do not build). Replaces the seams in `MakeOblivious` /
    `OnScheduleChange` and the `SquadSeesPlayer` stub. Depends on the enemy memory (5): the
    squad memory is the same record store, shared.
18. **Species schedule-change overrides** all chain to the Troika release (oracle "Species
    slot-435 overrides all chain"): the port's single `OnScheduleChange` needs no classname
    branch. If Gargoyle, Hengeyokai, Tzimisce or Werewolf are ever built, add their shun
    counters and the werewolf's schedule history behind the `PRESERVE_PATH` gate. *Closed.*
19. **Jump links.** Emit `NavLinkProxy`s from the decoded `.ain` links in the map bake (the
    pipeline's `nav_graph_glb` decoder already reads them); the motor reports nav type `Jump`
    while traversing one. Until then the stuck-on-top failure (13) is unreachable.

**Incapacitation**
20. **The trance.** `FeedInterrupt` (`0x1033a9e0`) on a victim with `BloodPool ≥ 1` and
    `IRelationType(attacker) != D_HT`: think timers := curtime, `SetSchedule(0xfb
    SCHED_TROIKA_MESMERIZED)` (`MAKE_OBLIVIOUS TRUE; SET_NPC_FLAG D_IS_BUSY, DONT_INVESTIGATE,
    NO_DIALOG; SET_ACTIVITY ACT_DISPOSITION_MESMERIZED; WAIT 30; WAIT_RANDOM 120`, damage
    interrupts, `DELAY_INTERRUPTS`); starts after `LeaveGrappleState`; ends by replacement with
    `0x6b` when `IsBusyWithDiscipline()`. `DELAY_INTERRUPTS` = one think of immunity re-armed by
    every install; `SetSchedule` zeroes the condition set; `MaintainSchedule` bound 10; effective
    mask = authored ∪ `BuildScheduleTestBits` ∪ `NPC_FREEZE`. *Landed* (`ElysiumFeedSchedules`,
    `ElysiumSchedule`), including executor pre-emption by a running program.
21. **The flee state, the cower family, disoriented and lost** (oracle "The flee state and the
    cower, disoriented and lost programs"). Fourteen programs with ids, the terminal flee
    state 8 entered on the two law flee levels, `SelectSchedule` case 8's chain with the one-shot
    `INITIAL_FLEE` report/scream/turn pass, the `0x77 → 0x78` hint translation, the `D_*` idle
    chain, and 14 missing tasks with arms. `DISORIENTED` is reached only as the terminal
    schedule of 16 discipline programs; `LOST` has no producer in the shipped game. **The feed's
    auto-accept reads the victim's ideal activity** (`AttemptFeed 0x10168910`, `+0xff0`) against
    `ACT_DISPOSITION_MESMERIZED / ACT_DISORIENTED / ACT_LOST / ACT_COWER` only, so a cowering NPC
    auto-accepts one time in three (`m_iCowerAnimOffset`). Port: the programs, the flee state,
    `ONE_HIT_KILL`'s reader in damage, and `IsFeedAutoAcceptState` answering from the ideal
    activity instead of the disposition name.
## Design
`FElysiumNpc::Think()` runs, in order: `ThinkDead`, admission, loadout, deferred order,
`RunConditionPass` (senses → `ElysiumNpcEnemy::GatherConditions` → `UpdateIdealState`), state
pump, `TickFeed`, watchdog, dialog, script-owned, `ThinkSchedulePolicy` (combat, scripted order
or any running program pre-empts the executors), `ThinkAutonomous` (patrol / ambient / stance).
Senses live in `Cognition.Conditions` (gathered per pass) and `Senses.Memory`; the enemy memory
(5) joins them as the store `BestEnemy` walks. The flag word is `NpcFlags`, read through
`IsOblivious()` / `IsBusyWithDiscipline()` / `HasDialogSuppressFlag()`. The stealth surface is a
separate producer the senses read as scalars.

The schedule kernel (`ElysiumSchedule`) is the VM: `FElysiumSchedule{Id, Tasks[], FailSchedule,
Interrupts, bDelayInterrupts}`, a registry keyed by `EElysiumScheduleId` with retail numbers in
`MetaFor`, `Start` as the single install choke point running retail's `SetSchedule` rule
(`OnScheduleChange` → `ClearConditions` → re-arm the delay), `Tick` checking the effective mask
once then running ≤10 completions. Runner verbs on `IElysiumScheduleRunner` are the host's
privileged instructions; `FElysiumNpc` overrides them. Adding a task: enum member with its
retail id and arm address, `ElysiumTaskName`, a `BeginTask` case (and `ContinueTask` if it spans
thinks), a runner verb with an honest default, the NPC override, and a recording override in the
tests' runner doubles. The failure path adds a `TaskFail(reason)` verb called on `Failed` before
routing; `FElysiumNpcFlags::OnTaskFail()` holds the masks. The reduced-think mode gates
`RunConditionPass` and the completion bound on the AI-think stamp. Followers are an idle-selector
step and a relationship composition; squads a new substrate object the flags already name.
Pathfinding is Unreal NavMesh; the motor reports the nav type the AI reads.

Unreal answers queries only: line and sky traces, light sampling from the rig's authored
worldlights, NavMesh paths, animation playback. Retail keeps cadence, thresholds, memory,
conditions and their order. No AIPerception, no Behavior or State Trees.

Retail bugs are reproduced verbatim (the `TaskFail` refcount leak, the dead `HOLDING` condition,
the alert ladder's once-per-life latch, the unreachable loiter and interact programs): the
programs above the host were authored against them.

## Seams
- Consumes: the Play-tier beat driver (0000); the pipeline's `nav_graph_glb` decode (19); the peer session's pending commit to
  `docs/vtmb/retail-defects.md` (the `TaskFail` leak note goes there afterwards); the game's
  content root still points at the legacy `exports/` tree (a pipeline lane switch).
- Provides: the awareness seam (`Cognition.Conditions`, the enemy memory, `Senses.Memory`,
  `IsOblivious()`, `ShouldInvestigate`) to 0006 and 0008; the stealth scalars; the failure path
  and cadence every later program family runs on (0006, 0007); `m_hFollowerBoss` and the composed
  `IRelationType` to 0006, 0007 and the target HUD; the trance and the flag word to the feed and
  dialogue gates.

## Tasks
In build order.
- [ ] 5 The enemy memory: record store, `OnLooked` writes, `BestEnemy` over memory, the
  spawn-time interest arm.
- [ ] 6 Sight and hearing details; `ambient_generic` sound events.
- [ ] 13 `TaskFail` with the reason table, the Troika teardown, both masks verbatim and the trace
  row; `COND_TASK_FAILED`/`COND_SCHEDULE_DONE`; stuck-on-top on `StopMoving`.
- [ ] 14 `TASK_SET_ACTIVITY` completes on a miss; the trance fixture stops forcing resolution.
- [ ] 15 The think cadence.
- [ ] 10 The three sweeps, the alert ladder and the `INVESTIGATE` programs, starting with the
  sound arm; the 26 tasks.
- [ ] 11 Interesting places: each selector arm verified; the group masks; the wait.
- [ ] 12 The reaction keys.
- [ ] 8 `AI_NPCFlag`/`MiscFlag` executed on apply and expiry.
- [ ] 16 Followers, including the possession and frenzy arms.
- [ ] 17 Squads.
- [ ] 1 The light query rebuilt to the recovered contract; the predicate, sentinel and sample
  points.
- [ ] 2 The light gauge producer.
- [ ] 3 The stealth-kill transaction.
- [ ] 4 The committed observer snapshot.
- [ ] 21 The flee state and the cower/disoriented/lost programs; auto-accept from the ideal activity.
- [ ] 19 Jump links emitted; nav type `Jump`.
- [ ] Visual-only: an `FGameplayDebuggerCategory` ("ElysiumNPC"; `CollectData` packs mind state,
  schedule/task, conditions, enemy memory, sense radii, the trace; `DrawData` draws the vision
  circle and cone, hearing radius, enemy LOS line, held place) and
  `IVisualLoggerDebugSnapshotInterface` on the body actor with `UE_VLOG` shapes at sense
  admission, enemy choice and schedule install. Reads only; out of Shipping; after 5.
- [ ] The `TaskFail` leak note in `docs/vtmb/retail-defects.md` once the peer's commit lands.

## Open questions
Recoveries, none owner-facing:
- Settled as not in the image (keep the project names, stop searching): slot 587
  (`CanWitnessSupernatural()`), the `m_bfNPCFrenziedFlags` bit names, `+0x6081`, `IVEngineServer`
  slot 118 (`GetLightForPoint`), the `0x100` shadow-brush contents name. Still unrecovered: the
  recv-prop names of the client's `+0x16b4` / `+0x16f4` stealth icon fields.
- Cadence and squads: `m_bfNPCStateFlags` bit 3 (forces PVS/LOS true), the subclass writers of
  `m_flNextAIThink` (`CNPC_VCamera`, `CNPC_VNewscaster`), `CAI_BaseNPC+0x98`'s entity, slot 578
  (the survivor callback), slot 168 (the `GetEnemy` variant), `m_iMySquadSlot`'s offset.
- The flee family: which `sound_volume_table.txt` rows fill the flee sound's radius and type
  (`DAT_1072bc88`/`DAT_1072bcc2`, runtime-filled), the obfuscated criminal level `+0x6364`, the
  base `SCHED_COWER` id, hint type names `0x2774`/`0x27d8`.
- Unnamed ConVars: the view-cone body-offset scalar at `0x10937a8c` and the 2-D cone mode at
  `0x10936f74` (both `.bss` pointers with no writer in `.text`); what authors hint type 800.
