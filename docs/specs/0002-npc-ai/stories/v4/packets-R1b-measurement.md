# R1b measurement — `face_enemy_turn`, the seam's build (A0, 2026-10-04)

Measured, no cause named. Record `Arena/scenarios/combat/face_enemy_turn.json` (a hostile
`TutorialThug` gunman on `far_ne`, facing yaw 180; at 6 s the player is set down 135° off its
facing). Lab session `uv run elysium gr --arena --headless`, `elysium.gr_scenario face_enemy_turn`,
real time (~138 fps); `elysium_entity_get` (`motion`, H19) on `arena_turner` every ~30 ms, 436
samples over 16 s. The headless arena run (fixed step 60 Hz) gives the same trace shape.

**The 13.4 s turn does not reproduce: 135° in 0.28–0.34 s, and `task_face_enemy` starts and
completes in one think.**

## The trace, the teleport to the first `task_face_enemy`'s `taskdone`

| lab t (s) | arena t (s) | who | kind | text |
|---|---|---|---|---|
| 6.004 | 6.000 | player | script | `player_teleport V(X=966.00, Y=400.00)` |
| — | 6.100 | arena_turner | cond+ | `NOT_FACING_ATTACK (0x61)` |
| 6.056 | 6.200 | arena_turner | cond- | `SEE_HATE (0x43)`, `SEE_ENEMY (0x46)`, `CAN_RANGE_ATTACK1 (0x4f)`, `BEHIND_ENEMY (0x57)`, `SEE_PLAYER (0x5a)` |
| 6.254 | 6.300 | arena_turner | cond+ | `SEE_ENEMY (0x46)`, `CAN_RANGE_ATTACK1 (0x4f)`, `BEHIND_ENEMY (0x57)` (lab: also `SEE_HATE`, `SEE_PLAYER`; arena: those two at 6.400) |
| 6.769 | 6.600 | arena_turner | schedule | `SCHED_TROIKA_RANGE_ATTACK1 (0xed)` (the running `0xef` / `0xed` ends first) |
| 6.769 | 6.600 | arena_turner | task | `task_face_enemy (1000000046) 0` |
| 6.769 | 6.600 | arena_turner | taskdone | `task_face_enemy` — **0.000 s after its start** |
| 6.776 | 6.617 | arena_turner | animevent | `3031` (the next shot) |

No `task_face_enemy` in the arena traces kept under `$ELYSIUM_WORK_ROOT/reports/arena` for 2026-10-03 and 2026-10-04 runs 0.3 s or longer.

## The samples across the turn (lab; scenario t ≈ wall − 0.05, aligned on the teleport)

| # | wall (s) | yaw | ideal yaw | `max_yaw_speed` | `m_afMemory` | `m_Activity` | body yaw | facing queue | °/s since the last sample |
|---|---|---|---|---|---|---|---|---|---|
| 179 | 6.047 | −180.00 | 180.00 | 45 | 0x30000 | 16 | 180.00 | 0 | 0 (player not yet moved) |
| 180 | 6.074 | −180.00 | 180.00 | 45 | 0x30000 | 16 | 180.00 | 0 | 0 (player moved) |
| 181 | 6.113 | −180.00 | 180.00 | 45 | 0x30000 | 16 | 180.00 | 0 | 0 |
| 182 | 6.137 | 171.35 | 44.95 | 45 | 0x30000 | 16 | −171.35 | 0 | 360 |
| 183 | 6.184 | 151.21 | 44.95 | 45 | 0x30000 | 16 | −151.21 | 0 | 428 |
| 184 | 6.206 | 135.80 | 44.95 | 45 | 0x30000 | 16 | −135.80 | 0 | 700 |
| 185 | 6.241 | 125.53 | 44.95 | 45 | 0x30000 | 16 | −125.53 | 0 | 294 |
| 186 | 6.263 | 115.29 | 44.95 | 45 | 0x30000 | 16 | −115.29 | 0 | 465 |
| 187 | 6.288 | 104.69 | 44.95 | 45 | 0x30000 | 16 | −104.69 | 0 | 424 |
| 188 | 6.310 | 85.96 | 44.95 | 45 | 0x30000 | 16 | −85.96 | 0 | 851 |
| 189 | 6.355 | 66.17 | 44.95 | 45 | 0x30000 | 16 | −66.17 | 0 | 440 |
| 190 | 6.395 | 55.92 | 44.95 | 45 | 0x30000 | 16 | −55.92 | 0 | 256 |
| 191 | 6.420 | 45.11 | 44.95 | 45 | 0x30000 | 16 | −45.11 | 0 | 432 |
| 192 | 6.457 | 44.95 | 44.95 | 45 | 0x30000 | 16 | −44.95 | 0 | 4 (arrived) |

| quantity | value |
|---|---|
| degrees turned | 135.05 (yaw −180.00 → 44.95; the ideal yaw 180.00 → 44.95 in one step, between samples 181 and 182) |
| time | first motion between wall 6.113 and 6.137, arrived between 6.420 and 6.457: **0.28–0.34 s** |
| degrees per second | **446** over samples 182–191 (126.24° in 0.283 s); per-sample 256–851 |
| `max_yaw_speed` during the turn | 45 throughout (`m_Activity` 16); 90 is read only at `m_Activity` 1, 45 at 16 and 25, before and after |
| `m_afMemory` | `0x30000` in every one of the 436 samples: bit `0x2000` never set |
| `m_Activity` | 16 during the turn (the samples before: 1 → 25 → 16 per attack cycle) |
| `m_flYawSpeed` (`+0x560`), `m_flGroundSpeed` (`+0x654`) | 0 in every sample (unwritten: the seam) |
| facing queue | count 0 in every sample; `orient_to_movement` true; `selection_move_yaw` 0 |
| `yaw` against `body_yaw` | equal and opposite in sign at every sample (the kernel's Source yaw, the body's Unreal yaw) |
| when the turn ran | **before** `task_face_enemy`: the yaw reached the ideal at ~6.41 scenario s; the task started at 6.769 (lab) / 6.600 (arena) |
