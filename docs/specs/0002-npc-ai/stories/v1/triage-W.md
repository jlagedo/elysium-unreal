# V1 triage — lane W

Runs (2026-10-04 UTC, `$ELYSIUM_WORK_ROOT/reports/arena/`): `20261004T015410` and `…015554` (the
roll call), `…015814` (the security camera alone), `…020415` (every other record, arena and map
hosts), `…021216` and `…021759` (the corrected records). One entry per red. Classes: README's
1 record error, 2 harness gap / fault, 3 known red, 4 new red.

## Totals (67 records)

| result | n | records |
|---|---|---|
| pass | 44 | 40 of the 48 roll-call records; `input_teleporttoentity`, `input_tweakparam_vision`, `input_takedamage`, `map_tutorial_idle` |
| known red | 11 | 6: `rollcall_vhuman`, `rollcall_vhumancombatpatrol`, `places_pedestrian_visit`, `places_thug_pt1`, `input_useinteresting`, `map_hub_idle`, `map_tutorial_sneak_past`; 5: `input_setrelationship`; 1: `patrol_sentry2_pingpong`, `patrol_monk_loop`, `input_clearpatrolpath` |
| new red | 9 | `rollcall_vanimal`, `rollcall_vdog`, `rollcall_vscurrying`, `script_walk_to_mark`, `script_dialog_hold`, `input_startplayerdialogremote`, `input_changeschedule_reselect`, `input_disablethink`, `maker_respawn` |
| harness | 3 | `rollcall_vcamera`, `rollcall_vcamerasecurity` (error), `rollcall_vzombie` (fail) |

## Known reds met

**Red 6 owns every `use_interesting 1` NPC, not only its places.** A row with `use_interesting 1`
emits no kernel event at all: no gather, no schedule, no task, while its body animates (the
port's ambient executor holds it). Retail selects `0xff SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP`
on the first idle selection (`CAI_BaseNPCTroika::SelectSchedule` case 1 step 4, `0x102af6eb`), and
when no place is eligible `TASK_FIND_INTERESTING_PLACE` fails into `Idle_Stand`. This silences
more than places: the tutorial's `thug_1`, `sentry2`'s row, the hub's pedestrians, and any input
fired at such an NPC. The input records were moved off `clinic_guard` (use_interesting 1) onto
`Hunter1` (use_interesting 0) for that reason.

| record | first unmet | trace | class |
|---|---|---|---|
| `rollcall_vhuman` (sm_asylum_1 Cal) | #0 schedule by 3.0 | 0 events in 3 s | 6 |
| `rollcall_vhumancombatpatrol` (la_museum_1 npc_guard_b1) | #0 schedule by 3.0 | 0 events | 6 |
| `places_pedestrian_visit` | #0 `…_SETUP (` by 3.0 | 0 events for pedestrian_female | 6 |
| `places_thug_pt1` | #0 `…_SETUP (` by 3.0 | `thug_maker` Spawn and OnSpawnNPC at 0.517, nothing from thug_1 | 6 |
| `input_useinteresting` | #2 `…_SETUP (` within 15 | `UseInteresting 1` at 2.02, then 113 `fidget02 rate=0` / 139 `seqfinished` (the executor), no schedule | 6 |
| `map_hub_idle` | probe pedestrian_female schedule ~ INTEREST | reads `SCHED_NONE` at 60 s; the patrol cops arrive and step (3 + 2 arrivals) | 6 |
| `map_tutorial_sneak_past` | #0 `…_SETUP (` by 3.0 | thug_maker spawns at 0.5, thug_1 silent; the hearing and sight halves never ran | 6 |
| `input_setrelationship` | #1 cond+ SEE_HATE within 1.0 | `SetRelationship player D_HT 5` at 2.02 is the last event; Hunter1 keeps idling | 5 |
| `patrol_sentry2_pingpong` | #10 arrived at sentry2_3 within 20 | `walk rate=0`; 6.6 m in 10.0 s (2.65 → 12.68), 3.6 m in 3.1 s; the 15.9 m leg is not walked in 20 s. Retail `walk_0` ground speed 136.7 cm/s (`animation_and_movers.md` § move_yaw fan) | 1 |
| `patrol_monk_loop` | #10 arrived at pod_1 within 10 | `walk rate=0`, ~0.6 m/s; pod_5..pod_2 reached in order | 1 |
| `input_clearpatrolpath` | #4 arrived within 12 | `ClearPatrolPath` at 11.12 mid-leg (10 m), no arrival by 23.1; the first 6.7 m took 9.8 s | 1 |

The patrol records show the program right: Setup installs `0x67` at once, Follow re-installs it,
one goal per point, `task_next_patrol_point` steps, and the monk loops backward from pod_5. Retail's
own Setup-then-Follow window is visible too: the port fails `TASK_GET_PATH_TO_PATROL_POINT` with
`0x1d` "No patrol path" when a think lands between the two wires (input_clearpatrolpath 0.600,
patrol_monk_loop 0.567), as `0x102aa640` does.

## New reds

| record | first unmet | trace | retail | port | owner |
|---|---|---|---|---|---|
| `rollcall_vanimal`, `rollcall_vdog`, `rollcall_vscurrying` | #0 schedule by 3.0 | cond+ at 0 s (`FLOATING_OFF_GROUND`; the dog also SEE_PLAYER, DOG_IDLE_FROM_ALERT), `ScriptUnhide` at 0.517, then nothing | a StartHidden NPC unhidden by the Troika `ScriptUnhide` tail `0x102c1ec0` (slot 614 reset) thinks and selects; the hidden ManBat, Mercurio, SabbatLeader and VampireBoss rows do select (FALL_TO_GROUND, red 5's shape) | the `CNPC_VAnimal` line's select, `ElysiumNpcSelectSpecies.cpp` (the Animal arm ~280–302); retail body not read | V6 (red 5's lifecycle) |
| `script_walk_to_mark` | #1 state `-> Script` within 1.0 | `BeginSequence` 2.017, `COND_? (0x4b)` 2.033, then only anim events | `PossessEntity 0x101a7880` → `NPC_STATE_SCRIPT`, `SelectSchedule 0x1028a380` case 4 → `SCHED_TROIKA_SCRIPTED_WALK 0xf2` | the director's 0.05 s beat (`ElysiumScriptedSequence.h:24-31`; divergences.md row 18) | V3 |
| `script_dialog_hold` | #1 `SCHED_TROIKA_START_PLAYER_DIALOG (` within 0.5 | `StartPlayerDialog` 2.017, `OnDialogBegin` at once, no schedule | `0x1029ef80`: forced install of `0x6d`; `OnDialogBegin` from its tasks | `ElysiumNpcDialogue.cpp:66` (via `ElysiumNpc.h:700`) opens synchronously | V3 (the dialogue hold as `0x6a RUN_DIALOG`) |
| `input_startplayerdialogremote` | #1 `…_REMOTE (` within 0.5 | `OnDialogBegin` at the input, no schedule | `0x1029f060`: forced `0x6e` | `ElysiumNpcDialogue.cpp:79` | V3 |
| `input_changeschedule_reselect` | #2 schedule within 0.5 | `ChangeSchedule -` at 3.017, the running program continues | `0x102c33f0`: flags2 `|= 0x82000000` (CHOOSE_NEW) on every call, `-` only skips the forced-schedule word | `ElysiumNpc.cpp:1672` → `StartNamedSchedule`: `-` is not a schedule name | V7 |
| `input_disablethink` | never: taskdone before 9.0 | `DisableThink 1` at 2.0; `task_special_idle_activity` completes 5.3 s and the schedule reinstalls | `0x1029f2a0` → `SetDisableAI 0x1029f300`; `NPCThink` returns at `m_bDisableAI +0x6080`; the wire's string is converted to FIELD_BOOLEAN | `ElysiumNpc.cpp:1387` accepts only a Bool variant: the corpus's string `'1'` / `'0'` disables nothing | V7 |
| `maker_respawn` (hw_hub_1 ratmaker_2) | #5 second OnSpawnNPC within 5.5 | Enable 1.017, OnSpawnNPC 1.033, rat_2 dies 4.05, OnNPCDied 4.05, no second child by 9.55 | `MakerThink 0x1034bbf0` re-arms at SpawnFrequency (5 s) after a child: due 6.03 with live 0 → `MakeNPC 0x1034b7b0` | `ElysiumNpcMaker.cpp` (think re-arm / DeathNotice live count) | R6 |

## Harness gaps and faults

1. **A row with no `model` key never activates the stage** (`npc_VCamera`, `npc_VCameraSecurity`:
   "the stage never activated within 60 s (runtime Failed, waiting on: map animation
   residency)"), and **a Failed stage poisons every later record of the boot** ("the stage runtime
   is Failed, not Active"; 44 records errored behind `rollcall_vcamera` in the first run). Fault.
2. **`rollcall_vzombie`**: `task_vzombie_crawl_out_of_ground` runs at 0.000 with no `schedule` event
   before it (`SpeciesSelectSchedule` answers `0x161`, `ElysiumNpcSelectSpecies.cpp:313`). Either the
   schedule tap misses the spawn-time install or that install bypasses `SetSchedule 0x10280e50`;
   undetermined. Record left as is.
3. **`never` takes no count and no start**: a taskfail storm cannot be told from one retail-legal
   fail (the roll call uses `never taskfail`, which a single legal fail would break), and silence
   inside a window that opens after 0 (DisableThink's off window, a second OnSpawnNPC while the
   first child lives, the podium's 10 s minimum) cannot be stated directly.
4. **`from_map` selects by targetname only**: unnamed rows (every patrol point on both maps, most
   hub places) cannot travel, so the hub's patrol and the tutorial's patrols are map records, and
   `input_clearpatrolpath` hand-places three points with the map's keys.
5. **No action answers or closes a dialogue** as the player does: the release half of the dialogue
   hold is not expressed.
6. **ensure / assert counts are not in the trace** (the map smokes' 0 ensure / assert claim).
7. Not a fault, recorded for the map host: time zero falls after a map's t0 load wires and before
   its delayed ones (the monk's logic_auto has fired, sentry2's t+2.0 has not).
8. The tap emits `taskdone task_get_path_to_patrol_point` twice per goal.

## Record errors fixed

`rollcall_payphone` (CPayphone::NPCThink `0x101aabf0` runs no schedule: now `never schedule`);
the three controller-line rows (the entity renames itself `playercontroller`); `input_disablethink`
(the 0 edge resets timers, it forces no reselection); the tutorial patrol records (the Society hub
is out of the start's PVS, where retail's normal think stretches to 16 s, `lifecycle.md` § 'The
interval laws': the player is now set down beside the patroller; `map_tutorial_idle`'s deadlines
carry the 16 s); `input_clearpatrolpath` (a row's `angles` key is refused); the input records moved
off a `use_interesting 1` NPC.

## Observations for V2 (passing records, not classified)

- `rollcall_vtaxidriver`: `SCHED_TROIKA_IDLE_DISPOSITION` reinstalled 455 times in 10 s,
  `task_special_idle_activity` completing on the frame it starts; the VRat line reinstalls
  `SCHED_VSCURRYING_LOITER (0x161)` 2–3 times per think (`maker_respawn`, `rollcall_vrat`). Schedule
  churn the roll call's `never taskfail` does not catch.
- Hidden rows (ManBat, Mercurio, SabbatLeader, VampireBoss; on the tutorial thug_2, thug_3,
  sheriff, sabbat_redshirt_*) sit in `FALL_TO_GROUND (0x3e)`: red 5's shape; the roll call passes
  them because any schedule counts.
- `OnFoundPlayer` fires on every think while the player is seen (lane K's `cover` trace); retail
  fires it on the new sighting (`GatherEnemyConditions 0x10270b20`).
