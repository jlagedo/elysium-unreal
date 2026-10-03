# Arena run — Green Room cover scenario over MCP (brief H2, run 4)

Headless `uv run elysium gr --arena`, one MCP call at a time. The stage was already up: `gr_scenario cover`
ran at t≈3.6 and `elysium.npc_trace arena_gunman` was set. Status at the first read: `Playing`,
`spawn_done true`, `game_time 49.26`. Times are game seconds. The trace ring holds 212 lines, so the
chain before t 10.87 (including the first claim of #11 at ~7.7) had already rolled off.

History: in run 1 no gunman spawned (nav built under `InitialLock`). In run 2, `0x102b7110` was still a
seam. In run 3 the real search ran but nothing was claimed from the seat, and task 5 stuck at gen 1.

## 1. Chain so far (`npc_trace_tail 212`, read at t≈49)

| t | event (trace line) |
|---|---|
| 10.87 | at #11: `task_snap_to_hint`, `task_face_enemy`, `task_play_cover_idle`, `SCHEDULE_DONE`, then `SelectCoverOrKickSchedule AI_BaseNPCTroika.cpp:23747 -> 0xa4` (TAKE_COVER_HINT_VS_MELEE) |
| 10.97 | `task_resolve_botch_in_cover`, then `task_play_cover_outof`, which never completes |
| 15.95 | `Break condition -> COND_? (0x29)`; "0xa4 interrupted by COND_41"; `:23805 -> 0x9b`. **This is the release of #11**: a schedule change caused by the 0x29 interrupt. No TaskFail line. next use 20.95 = 15.95 + 5.0 |
| 15.95–18.35 | 0x9b: `get_path_to_hintnode`, `attempt_dive`, `run_path`, `wait_for_movement` (to #10 ne) |
| 18.35 | 0x29 break, still moving. #10 released (next use 23.35), then 0x9b again, which claims #8 low_north |
| 20.81 | reached #8: `clear_npc_flag`, `snap_to_hint`, `face_enemy` |
| 21.11 | 0x29 break. #8 released (26.11), then 0x9b claims #11 nw again (its 20.95 stamp had expired) |
| 21.41 | 0x29 break. #11 released (26.41), then `SCHED_TROIKA_RANGE_ATTACK1 (0xed)` (no selector line printed) |
| 22.84 | `task_announce_attack`, then `task_range_attack1` |
| 25.26 | `Break -> TOO_CLOSE_TO_ATTACK`. 0x9b claims #9 low_south |
| 28.30 | 0x29 break. #9 released (33.30). `SelectScheduleRangedCombat NPC_VHuman.cpp:1807 -> 0xef` STEP_BACK_RANGE_ATTACK1 |
| 28.30 → 41.68 | `task_face_enemy` runs for 13.4 s |
| 41.68 | `task_announce_attack`, then `task_range_attack1`. Still the current task at death (t 141.00) |

The claim order is derived from the `next use` stamps (the `gr_hints` read at 61.80): nw, ne, low_north,
nw, low_south. Each release is the 0x29 interrupt followed by a schedule change, and every stamp is release + 5.0.

## 2. Polls, no occlusion

| t | schedule / task | hint | conditions / notes |
|---|---|---|---|
| 61.58 | 0xef task 5, fail=0xed | node none, at_cover 0 | TOO_CLOSE_FOR_RANGED, TOO_CLOSE_TO_ATTACK, SEE_*, CAN_RANGE_ATTACK1; player 747 cm; origin (-188,-311) |
| 61.80 hints | — | all owner none | next use: nw 26.41, ne 23.35, low_south 33.30, low_north 26.11 |
| 82.49 | 0xef task 5 | none | unchanged; no trace line after 44.72 |
| 91.84 hints / 92.00 | 0xef task 5 | none | unchanged. It never reached AT_COVER again, never fired, and made no new claim |

## 3. `gr_los on` (t≈95) → `off` (t≈120)

| t | observed |
|---|---|
| ~95 | player moved to (-278,-278), 96 cm from the gunman, who stepped back to (-188,-311) next to the block. Still SEEN with LOS, so no occlusion. HEAR_PLAYER pulse at 96.44 |
| 98.89 / 114.36 | 0xef task 5 unchanged. No new search, no claim of low_north |
| ~120 `off` | player at seat (-300,683). 120.56 `SetCondition NOT_FACING_ATTACK`; 122.27 `WEAPON_THROUGH_WALL` |
| 122.58 / ~135 | 0xef task 5 unchanged. Player 1000 cm, TOO_CLOSE_* still set, NPC does not turn |

## 4. `gr_hints --validate arena_gunman`

| t | held | nw #11 | ne #10 | low_south #9 | low_north #8 | walk returns |
|---|---|---|---|---|---|---|
| 118.29 (run 1, no hint held, gunman at (-188,-311), player behind the block) | none | bad range 0.88 ≥ 0.73 | bad range 0.94 ≥ 0.73 | Distance 110 < 256 | Distance 254 < 256 | none |
| 167.97 (restage, holds #11) | #11 | refused: owned by #12 | **admitted** 1920.00 (facing 0.62) | refused: good range 0.51 ≤ 0.87 | **admitted** 1164.99 (facing 0.95) | #10 |

Matches expectation: low_north and corner_ne pass, low_south is refused, and nw is refused only as `unusable` because it is owned.

## 5. Kill (`TakeDamage 1000`, t 141.00)

| read | observed |
|---|---|
| trace | 141.00 `SelectIdealState AI_BaseNPC.cpp:535 -> 7`, `:577 -> 7`, `LIGHT_DAMAGE` set, `ClearSchedule: 0xef cleared` |
| brief | Dead, schedule none, gen 3, `SCHEDULE_CHANGED`, weapon none, `on_ground=0`, body "skeletal (standing)" |
| hints 143.53 | unchanged (all owner none, same stamps). **The death + 5.0 release is untestable**: 0xef holds no hint |

## 6. Restage (`gr_scenario cover`, t 153.12) + `npc_trace arena_gunman`

| t | observed |
|---|---|
| 153.12 | rebuilt: 13 defs, 4 nodes, 18 entities, epoch 3, player #13 |
| 160.50–160.61 | at #11: `snap_to_hint`, `face_enemy`, `play_cover_idle`, `SCHEDULE_DONE`, then `:23747 -> 0xa4`, then `play_cover_outof` |
| 163.28 | 0xa4 task 3, node 11 owner #12, `at_cover=1`, flags `AT_COVER_HINT\|TASKS_FACE_ENEMY`. Clean repeat |
| 176.34 | still 0xa4 task 3 (`play_cover_outof`) after 15.7 s. No 0x29 break this time |

## 7. Log (`Saved/Logs/ElysiumUE.log`, grep)

Ensure 0, Assert 0, `Error:` 33 (31 `LogPython` engine-toolset imports, 2 `LogHttpConnection`
socket_send_failure). Elysium errors 0.

## Defects observed

1. **`TASK_RANGE_ATTACK1` never completes.** It started at 41.68 and was still `0xef task 5` at 61.58, 82.49, 92.00, 114.36 and 122.58, until `ClearSchedule ... 0xef cleared` at 141.00. The earlier instance (22.84) ended only by the `TOO_CLOSE_TO_ATTACK` interrupt. No shot was observed in the whole run.
2. **`task_play_cover_outof` never completes** (a cover animation task, consistent with the anim-event placeholder). It ran from 10.97 until the 0x29 break at 15.95, and on restage from 160.61 to at least 176.34 (0xa4 task 3). This is why the hint loop turns over only on the 0x29 interrupts.
3. **0xef is a sink.** Nothing interrupts it: the player moved to 96 cm, then to 1000 cm, and `NOT_FACING_ATTACK` (120.56) and `WEAPON_THROUGH_WALL` (122.27) were both set with no break for 99 s. Retail's 0xef interrupt mask is not checked against this.
4. **The range band is inverted** (carried from run 3): `TOO_CLOSE_FOR_RANGED` and `TOO_CLOSE_TO_ATTACK` hold at 747 cm and 1000 cm. That drove the 25.26 break and the step back.
5. **`task_face_enemy` took 13.4 s** (28.30 → 41.68), and the NPC did not turn after `NOT_FACING_ATTACK`.
6. **Condition 0x29 is unnamed.** The trace prints `COND_?` and `COND_41`; 0x57 prints as `COND_?`/`COND_87`. It is the interrupt behind every release, and its source is unrecovered.
7. **`npc_brief` player line:** `2092cm, unseen ... vision=1118 in_range=0` while SEE_PLAYER and HAVE_ENEMY_LOS are set (163.28). This looks like a cm/inch unit mix in the printer.
8. **The dead body stays "skeletal (standing)"** with `on_ground=0` after 141.00. No death pose is visible in the brief.
9. **Unexplained one-think `INVESTIGATE_SOUND` + `HEAR_PLAYER` pulses** between 25.67 and 44.72 while the player stood still (source unknown). One more fired at 96.44, which is the teleport.
10. **Scenario authoring:** after the step back, `gr_los on` does not occlude, because the gunman stands 96 cm from the hide seat.
