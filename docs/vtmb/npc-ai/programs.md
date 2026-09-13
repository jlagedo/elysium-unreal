# NPC AI — The programs

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Door-obstruction schedule selection

`FUN_102b7370` at `0x102b7370` is the Troika NPC's door-obstruction selector; the working semantic
name is `CAI_BaseNPCTroika::SelectDoorObstructionSchedule`. It is not an idle or disposition
helper. Idle state calls it only when `m_hBlockedDoor(+0x5d28)` or
`m_hCondHitByDoor(+0x5d2c)` is valid, and combat state calls it after the two higher-priority
combat helpers. A non-zero result is a selected schedule returned through
`CAI_BaseNPCTroika::SelectSchedule`; zero means this policy declined to handle the obstruction.

The selector first rejects an NPC that already owns `m_pHintNode(+0x5ddc)`. It then chooses the
obstruction source in this order:

1. A valid `m_hBlockedDoor`, provided the door's expiry value at `+0x640` is later than game
   `curtime`; an expired handle is cleared.
2. Under `COND_ENEMY_UNREACHABLE` (`0x59`), no active movement-state entry at `+0x5bb0`, and a
   valid timed record rooted at `+0x5da4`, the entity held by that record.
3. A valid `m_hCondHitByDoor`, but only while `COND_HIT_BY_DOOR` (`0x34`) is set.

With a source selected, the eligible path records its handle at `+0x6448` and asks the ordinary
hint-node machinery to find and claim cover. A claimed medium-cover, low-cover or corner-cover
context (type `100`, `101` or `0x27d8`) returns
`SCHED_TROIKA_TAKE_COVER_HINT_DOOR` (`0x9c`). Any other claimed hint is released with a five-second
delay and the selector returns zero.

When no hint is claimed, the source origin becomes `m_vSavePosition(+0x5dd0)`. The squared
NPC-to-source distance is compared with `65536.0` (256 units), and virtual `+0x29c` is
`GetEnemy`; the resulting schedules are:

| Distance from source | Enemy | Schedule |
|---:|---|---|
| at most 256 units | present | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR` (`0x90`) |
| at most 256 units | absent | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE` (`0x91`) |
| over 256 units | present | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT` (`0x94`) |
| over 256 units | absent | `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE` (`0x96`) |

The nearby schedules perform repeated step-backs and fail over to their run variants. The distant
schedules wait while facing the enemy, or the saved obstruction position in the `_NE` variants.
This makes the helper a complete obstruction reaction policy: cover at a compatible door hint,
back away while near the obstruction, and wait once already clear of it.

## Interesting-place eligibility

`TASK_FIND_INTERESTING_PLACE` (task 164) is Troika `StartTask` case 48, body `0x102a1f23`: it calls
`PickRandomInterestingPlace` (`0x102db590`), stores the result at `+0x62ec`, then requires
`PickSpotFor` (`0x102da0d0`) to sample a free spot inside the node's bounds; either failure is
`TaskFail(0x22)`.

`BuildCandidates` (`0x102db470`) walks the global place list (head `DAT_10927194`, next at `+0x540`)
at **rating 5 down to 0**, takes the first rating level that yields any eligible node, and picks
uniformly within it. Eligibility (`0x102dad60`) is:

```
place->+0x57c != 0                                   // authored `enabled`
place->+0x57d == 0                                   // runtime-disabled
(place->+0x584 - place->+0x58c - place->+0x588) > 0  // max_npcs - reserved - users
(place->+0x574 & npc->+0x62dc) != 0                  // group mask & interesting_place_groups
|place - npc|² <= 1.0e8                              // 10000 units, straight line
```

There is **no pathfinding and no line-of-sight test in the find stage** — a node 9,000 units away
behind a wall is eligible, and the walk schedule is where such a goal fails. The entity classname
is `intersting_place`, misspelled in VtMB itself.

`vdata/system/interestingplacetypelist.txt` binds a node's `type` to activities. `wall_lean` is
`ACT_WALL_LEAN_INTO` → `ACT_WALL_LEAN_IDLE` → `ACT_WALL_LEAN_OUTOF`, and unlike `sitting` and the
`conversation_*` types it declares no `AcceptedClasses` block, so any NPC may claim one.

## Ordinary humanoid combat selection

State 2 is the concrete combat branch. `CNPC_VHuman::SelectSchedule` (`0x10384ee0`) and the
`CNPC_VHumanCombatant` override (`0x103872d0`) query the active weapon's capability bits. A weapon
with `0x18000` enters virtual `+0x970`, the melee selector at `0x10385e40`; other weapons enter
virtual `+0x974`, the ranged selector at `0x10386560`. A selector returning zero falls through to
`CAI_BaseNPCTroika::SelectSchedule`, so the weapon policy composes with damage, door, fear and base
state reactions rather than replacing them.

The combat-facing condition identities needed by those bodies are fixed by the retail registry:

| ID | Condition | ID | Condition |
|---:|---|---:|---|
| `0x0c` | `SHOULD_DODGE` | `0x0d` | `SHOULD_BLOCK` |
| `0x0e` | `SHOULD_STEPBACK` | `0x0f` | `SHOULD_KICK` |
| `0x28` | `KNOCKBACK` | `0x2f` | `WAITING_ATTACK_TIME` |
| `0x3c` | `WEAPON_THROUGH_WALL` | `0x40` | `NO_PRIMARY_AMMO` |
| `0x48` | `ENEMY_OCCLUDED` | `0x4a` | `HAVE_ENEMY_LOS` |
| `0x4c` | `LIGHT_DAMAGE` | `0x4d` | `HEAVY_DAMAGE` |
| `0x4e` | `REPEATED_DAMAGE` | `0x4f` | `CAN_RANGE_ATTACK1` |
| `0x50` | `CAN_RANGE_ATTACK2` | `0x51` | `CAN_MELEE_ATTACK1` |
| `0x52` | `CAN_MELEE_ATTACK2` | `0x59` | `ENEMY_UNREACHABLE` |
| `0x5f` | `TOO_CLOSE_TO_ATTACK` | `0x60` | `TOO_FAR_TO_ATTACK` |
| `0x63` | `WEAPON_BLOCKED_BY_FRIEND` | `0x66` | `WEAPON_SIGHT_OCCLUDED` |

`KNOCKBACK` is registered and read but never produced, so the two `SelectSchedule` branches that
test it and the three melee-idle schedules that list it as an interrupt are unreachable; the
knockback reaction forces its schedule directly instead (`docs/vtmb/combat-and-damage.md` →
"`COND_KNOCKBACK` is a dead condition").

The melee selector is ordered policy, not a random attack picker. Scripted combat-mode and weapon
switch gates run first, followed by door/class helpers. `SHOULD_DODGE` returns
`SCHED_TROIKA_MELEE_DODGE` (`0xd5`); `SHOULD_BLOCK` returns
`SCHED_TROIKA_MELEE_PREBLOCK` (`0xd6`). When kick and step-back are both requested, a binary random
choice selects `SCHED_TROIKA_MELEE_KICK` (`0xdb`) or `SCHED_TROIKA_MELEE_STEPBACK` (`0xd3`);
either condition outranks an ordinary attack. A usable
`CAN_MELEE_ATTACK1` then chooses `SCHED_TROIKA_MELEE_ATTACK1` (`0xdc`) or its no-turn variant
(`0xdd`). Remaining branches can switch to ranged (`0xe9`), take cover on unreachable enemy
(`0x17`), idle in melee (`0xc7`), circle (`0xe0`/`0xe1`), use class helpers, advance
(`0xca`/`0xcb`), or slow-advance (`0xd1`/`0xd2`) according to facing, distance, reachability and
per-NPC timers. The selector retains timer state, so repeated ticks do not independently reroll
every action.

The ranged selector first honors a pending switch to melee (`0xe3`) and weapon/timing setup. A
weapon protruding through a wall selects back-away (`0xb8`). Its class helpers then own dodge,
door, cover and chase decisions. Attack-ready branches choose the registered range-attack shoot,
range-attack, step-back and forced-range schedules (`0xec`–`0xf0`); no ammo, blocked line of fire,
occlusion, excessive distance or an obstructing friend instead route through reload/hide,
move-for-clear-shot (`0xbb`/`0xbd`), wait-for-clear-shot (`0xbc`), cover, chase (`0xb1`) or
run-away (`0xb9`) policy. Exact fire and ammunition commit remains in the weapon controller: a
schedule only establishes the task/activity that reaches its animation event.

The selected schedules preserve the multi-update action contract:

- `SCHED_TROIKA_CHASE_ENEMY` sets fail schedule `CHASE_ENEMY_FAILED`, tolerance 24, gets a path to
  the enemy, forces relaxed locomotion, runs it and waits for movement. It interrupts on a new,
  dead, unreachable, occluded or lost enemy; any newly available melee/ranged attack; too-close,
  task-failed or better-weapon conditions. Pathing cannot monopolize an attack-ready NPC.
- `SCHED_TROIKA_MELEE_ATTACK1`/`_NR` set melee-idle as failure, face, stop, then transfer to
  `SCHED_TROIKA_MELEE_ATTACK1_SWING`. The ordinary form can still abort on too-far-for-melee;
  both admit enemy death/loss, damage, being attacked, dodge and block before transfer. The swing
  schedule has exactly `TASK_ANNOUNCE_ATTACK 1 -> TASK_MELEE_ATTACK1 0` and no interrupts, so once
  that terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each tick.
- `SCHED_TROIKA_RANGE_ATTACK1` sets ignore-failure, stops, resolves a prior botch, faces, announces,
  runs `TASK_RANGE_ATTACK1`, applies a zero base plus up-to-one-second finish wait, resolves botch,
  then waits for attack time. It can be interrupted by new/dead enemy, light/heavy damage,
  occlusion, no primary ammo or too-close-to-attack.
- `SCHED_TROIKA_MELEE_PREBLOCK` stops then runs `TASK_MELEE_PREBLOCK`; melee dodge stops, runs
  `TASK_MELEE_DODGE`, stops again and runs `TASK_MELEE_DODGE_ATTACK`. Both use melee-idle as their
  failure schedule and interrupt only on lost enemy.

These selectors define the minimum ordinary fight loop: acquire one committed enemy, derive
attack/spacing/obstruction conditions, choose a schedule with current-schedule context, run its
movement and attack tasks, let the weapon commit the hit, then feed the committed damage back into
memory and conditions on the next decision pass.

## Interesting places: the selector, the programs, the wait (2026-09-08)

**Selector** (`CAI_BaseNPCTroika::SelectSchedule` case 1 step 4, asm `0x102af6eb`–`0x102af7f3`):
```
if (m_bUseInteresting +0x63d9) {
  if (navigator state (0x100037e2) == 8 || m_hInterestingPlace +0x62ec != 0) {
    if (HasCondition(0x13 CROSSWALK_DONTWALK)) return 0x102;
    if (HasCondition(0x10 SHOULD_INTERACT))    return 0x106;
    if (HasCondition(0x11 SHOULD_LOITER))      return 0x105;
    return 0x100;
  }
  return 0xff;
}
```
Producers: `0x12/0x13` from `UpdatePedestrianInfo` (`0x102a0d20`); `0x10` from `0x102a0cb0`
(sets it on both NPCs; **zero recovered callers**); **`0x11 SHOULD_LOITER` has no setter in the
image** — `SCHED_TROIKA_LOITER` is unreachable from this selector in retail.

**Blobs** (byte-scanned; registrar `FUN_102b9810`):
- `0xff SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_SETUP` (`0x105e6aa8`): `SET_FAIL_SCHEDULE
  Idle_Stand; FIND_INTERESTING_PLACE; SET_PRESERVE_PATH 1; SET_SCHEDULE 0x100`. Interrupts
  `NEW_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT
  IGNORE_UNKNOWN DETECTED_ATTACK PLAYER_ON_HEAD`.
- `0x100 SCHED_TROIKA_WALK_TO_INTERESTING_PLACE` (`0x105e67b8`): `SET_FAIL_SCHEDULE
  …_FAILED; GET_PATH_TO_INTERESTING_PLACE; WALK_PATH; WAIT_FOR_MOVEMENT; STOP_MOVING;
  FACE_INTEREST; SET_SCHEDULE SCHED_TROIKA_DO_INTEREST_ACTIVITY`. Interrupts add `GIVE_WAY
  SHOULD_INTERACT SHOULD_LOITER CROSSWALK_DONTWALK`.
- `SCHED_TROIKA_DO_INTEREST_ACTIVITY` (`0x105e6130`): `SET_FAIL_SCHEDULE Idle_Stand;
  SET_PRESERVE_PATH 0; DO_INTEREST_ACTIVITY; WAIT_PVS`. `…_FAILED` (`0x105e6520`):
  `SET_ACTIVITY ACT_IDLE; WAIT 5; WAIT_RANDOM 5; SET_SCHEDULE 0xff`.
- `0x105 SCHED_TROIKA_LOITER` (`0x105e5eb8`): `SET_FAIL_SCHEDULE Idle_Stand; STOP_MOVING;
  DO_LOITER_ACTIVITY; WAIT_PVS`. `0x106 SCHED_TROIKA_INTERACT` (`0x105e5d10`): `…;
  DO_INTERACT_ACTIVITY; WAIT_PVS`. `0x102 SCHED_TROIKA_WAIT_AT_CROSSWALK` (`0x105e6308`):
  `…; PAUSE_MOVING; FACE_NEXT_NODE; WAIT_INDEFINITE`, interrupt `CROSSWALK_WALK`.
None of these carries `SEE_ENEMY` or any `HEAR_*` — an interest program is broken by
`NEW_ENEMY` (the enemy actually replaced), not by merely seeing.

**Eligibility** (`0x102dad60`, restating with the field names): `m_bEnabled +0x57c` (key
`enabled`), `+0x57d == 0` (dead: only the constructor writes it, to 0), `max_npcs +0x584 −
+0x58c − +0x588 > 0`, `(m_iGroupID +0x574 & m_iInterestingPlaceGroups +0x62dc) != 0`, `|place −
npc|² ≤ 1.0e8`. **Both sides are bitmasks** (corrected 2026-09-08): `CAI_InterestingPlace::Spawn`
(`0x102d9c20`) converts the place's `group_id` `1..32` to `1 << (id−1)` and anything else to
`1` (group 1), and clamps `rating` to `0..5`; the NPC's `interesting_place_groups` is a
space-separated list of 1-based indices → mask (`FUN_10298910`; empty or `"0"` → 0, no place
ever matches). So a place matches only an NPC that lists its group. On `sp_tutorial_1` `thug_1`
(`"2"`) matches `pt1`, `pt2`, `pt3` (all `group_id 2`) and nothing else; with `pt2`/`pt3`
authored `enabled 0` his pool at spawn is **`pt1` alone**, 30–60 s per visit. `m_bEnabled`'s only
writers are `InputEnable` (`0x102db420`) and `InputDisable` (`0x102db440`).

**The wait.** `0x102a9f40(npc, place, activityIdx, bInto)` — shared by
`TASK_DO_INTEREST_ACTIVITY` / `_LOITER_` / `_INTERACT_` (both call sites in Troika `StartTask`):
`ClaimMarker` (`0x102da7c0`); fire `npc+0x5f74`; if the type has an INTO activity
(`0x102dae70`) → `flags1 |= 0x20000000 INTERESTING_INTO`, `+0x6304 = 1`, `SetActivity(into)`,
`+0x63d4 = -1`; else `+0x6304 = 2`, `SetActivity(idle)`, `+0x63d4 = curtime + RandomFloat(2,
10)`; **`m_flWaitFinished (+0x5db4) = curtime + RandomFloat(m_fMinStayTime +0x568, m_fMaxStayTime
+0x56c)`** — the same field every `TASK_WAIT*` uses, no clamp, no swap (`ip_20`'s authored
`10/5` is a shipped inversion); `match_orientation +0x570` sets the yaw from the marker.

**The failed walk, walked (2026-09-12).** `TASK_GET_PATH_TO_INTERESTING_PLACE` is Troika
`StartTask` (`0x102a1910`) case `0x31`: no held place (`+0x62ec == 0`) → `TaskFail(0x22)`;
otherwise `AI_NavGoal_t{type 8, dest = m_vecInterestingPlace, tolerance [0x1049a1ac], flags −1}`
into navigator `SetGoal` (`0x102ecd20`), and a refusal is `TaskFail(0x0c)` "Don't have a route".
`TaskFail`'s step 1 (`0x102b53d0`) releases the visit, so the place is free again at once. The
program's declared route is `SCHED_TROIKA_WALK_TO_INTERESTING_PLACE_FAILED` (`0x105e6520`):
`SET_ACTIVITY ACT_IDLE; WAIT 5; WAIT_RANDOM 5; SET_SCHEDULE SETUP` — with `TASK_WAIT_RANDOM`'s
0.1 floor, **one route attempt every 5.1–10 s**, standing in `ACT_IDLE` between them. The pick
(`0x102db590` / eligibility `0x102dad60`) keeps no memory of a failed place, so the same
top-rated node is chosen again every time; nothing gives up and nothing excludes it. **Port
divergence (owned by 0002/11, which retires the executor):** `FElysiumNpc::ThinkAmbient` has no
`_FAILED` arm — a refused route puts the place in a port-only `FailedSpotIndices`, releases the
claim, and the next think claims the next candidate; when every candidate has failed the list is
cleared and the cycle restarts, **one refused route per think**. Witness on `sp_tutorial_1`
(2026-09-12, with the body's `refused:` diagnostic below): `Jack` (`interesting_place_groups 32`
→ `ip_by_window`, `ip_lean_1`, both across the map) alternates the two at the 0.1 s cadence;
`mercenary_upstairs` (`8` → `sentry3_ip_arms_crossed` one floor down, `ip_melee_guy` ×2 in the
warehouse) cycles the three on its 16 s cadence. Every one of those routes is refused because the
runtime navmesh does not connect the NPC's area to the node (a partial path ends 160–240 m
short), not because a node is off the mesh. Retail refuses them too, and by its own rule: the
`.ain` graph has no node within 6000 units of Jack's spawn and none at the Society hub
(`navigation-jump-links.md` § "Tutorial connectivity"), so `SetGoal` fails and retail stands
those NPCs idle with the 5–10 s retry. The port's mesh agrees by geometry, not by contract —
spec 0002 story 24.

## The `INVESTIGATE` family, decoded (2026-09-08)

691 schedule blobs in the image; **31** carry `INVESTIGAT` (the doc's earlier count of 32 included
`SCHED_INVESTIGATE_SOUND`, a Source-SDK name at `0x105d3754` that lives only in the stale debug
name array `0x105d1488` — no blob, no id; never use that array for numbers). Registration in
`FUN_102b9810` is `mov [esp+0x10], name; mov [esp+0x14], id; call 0x1001528a` immediately before
the blob's `call 0x1000ce8c`. **No `INVESTIGATE` program declares `Flags`** (no
`DELAY_INTERRUPTS`).

| id | name | blob |
|---|---|---|
| 0x50 | SCHED_TROIKA_INVESTIGATE_SOUND_FLINCH | 0x105ff320 |
| 0x51 | SCHED_TROIKA_INVESTIGATE_SOUND | 0x105fef58 |
| 0x52 | SCHED_TROIKA_INVESTIGATE_OTHER_SOUND | 0x105feca8 |
| 0x53 | SCHED_TROIKA_INVESTIGATE_SOUND_FLINCH_NO | 0x105feb38 |
| 0x54 | SCHED_TROIKA_INVESTIGATE_SOUND_NO | 0x105fe860 |
| 0x58 | SCHED_TROIKA_ALERT_INVESTIGATE_UNKNOWN_ATTACKER | 0x105fdb78 |
| 0x59 | SCHED_TROIKA_INVESTIGATE_UNKNOWN | 0x105fd948 |
| 0x5a | SCHED_TROIKA_INVESTIGATE_UNKNOWN_QUICK | 0x105fd728 |
| 0x5b | SCHED_TROIKA_INVESTIGATE_UNKNOWN_ATTACK | 0x105fd5c0 |
| 0x5c | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER | 0x105fd298 |
| 0x5d | SCHED_TROIKA_INVESTIGATE_UNKNOWN_LOST | 0x105fcfb8 |
| 0x5e | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER_RUN | 0x105fcc80 |
| 0x5f | SCHED_TROIKA_INVESTIGATE_UNKNOWN_LOST_RUN | 0x105fc960 |
| 0x60 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_IGNORE | 0x105fc698 |
| 0x61 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_OTHER_IGNORE | 0x105fc3b0 |
| 0x62 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_RETURN | 0x105fbf50 |
| 0x63 | SCHED_TROIKA_INVESTIGATE_UNKNOWN_RETURN_GIVEUP | 0x105fbae8 |
| 0x64 / 0x66 / 0x68 | SCHED_TROIKA_INVESTIGATE_NODE / _WALK / _HUNT | 0x105fb858 / 0x105fb360 / 0x105faea0 |
| 0x7f | SCHED_TROIKA_HUNT_INVESTIGATE_FLINCH | 0x105f87b0 |
| 0x80 | SCHED_TROIKA_HUNT_INVESTIGATE | 0x105f8510 |
| 0x81 | SCHED_TROIKA_HUNT_INVESTIGATE_UNKNOWN | 0x105f8240 |
| 0x82 | SCHED_TROIKA_HUNT_INVESTIGATE_UNKNOWN_LOST | 0x105f7fa8 |

Species tables (own id spaces): `0x15a VHENGEYOKAI_INVESTIGATE_UNKNOWN_ATTACK` (`0x1063eeb0`),
`0x15b/0x15c VFRENZYSHADOW_HUNT_INVESTIGATE(_UNKNOWN)` (`0x106388f8`/`0x106386a0`),
`0x15c/0x15d VTZIMISCE_HUNT_INVESTIGATE(_UNKNOWN)` (`0x1065b7e0`/`0x1065b568`),
`0x15d VGARGOYLE_INVESTIGATE_UNKNOWN_ATTACK` (`0x10639db0`, `TASK_LOOK_AT_PLAYER`),
`0x168 VZOMBIE_IGNORE_INVESTIGATE_UNKNOWN_ATTACK` (`0x10665120`).

**The programs, verbatim.**

`0x51 INVESTIGATE_SOUND`: `STOP_MOVING; STORE_LASTPOSITION; REMEMBER MEMORY:INVESTIGATING;
ALERT_LOOK_AT_BEST_SOUND; WAIT_RANDOM 0.5; PLAY_SOUND SOUND:Target_Suspect; WAIT_RANDOM 1.5;
SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND; SET_NPC_FLAG IGNORE_DOOR_FAILURE;
SET_FAIL_SCHEDULE SCHED_TROIKA_ALERT_LOOK_AROUND; WALK_RUN_PATH_COMBAT_SOUND; WAIT_FOR_MOVEMENT;
SET_SCHEDULE SCHED_TROIKA_ALERT_LOOK_AROUND`. Interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY
SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND SEE_SOUND_SOURCE PLAYER_ON_HEAD`.
`0x52 INVESTIGATE_OTHER_SOUND`: `PLAY_SOUND Target_Suspect; SET_TOLERANCE_DISTANCE 5;
GET_PATH_TO_BESTSOUND; SET_NPC_FLAG IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE ALERT_LOOK_AROUND;
WALK_RUN_PATH_COMBAT_SOUND; WAIT_FOR_MOVEMENT; SET_SCHEDULE ALERT_LOOK_AROUND`; interrupts as
0x51 minus `SEE_SOUND_SOURCE`. `0x54 _SOUND_NO`: tasks as 0x52; interrupts with
`SEE_SOUND_SOURCE`, without `INVESTIGATE_SOUND`. `0x50 _SOUND_FLINCH`: `ADD_EVENT_EXPRESSION
EXPRESSION:FLINCH; PLAY_COWER ACT_COWER_INTO; SET_SCHEDULE 0x51`; interrupts `LIGHT_DAMAGE
HEAVY_DAMAGE`. `0x53 _FLINCH_NO`: identical, also transfers to **0x51** (retail asymmetry).
`0x58 ALERT_INVESTIGATE_UNKNOWN_ATTACKER`: as 0x51 with `ALERT_LOOK_AT_UNKNOWN_ATTACKER` in
place of the best-sound look; no `SEE_SOUND_SOURCE`.

`0x59 INVESTIGATE_UNKNOWN`: `STOP_MOVING; STORE_LASTPOSITION; REMEMBER INVESTIGATING;
LOOK_AT_BEST_UNKNOWN; WAIT_RANDOM 0.5; PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5; SET_NPC_FLAG
LOOKED_AT_UNKNOWN`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE PLAYER_ON_HEAD`. `0x5a _QUICK`: same minus the first two tasks.
`0x5b _ATTACK`: `SET_ACTIVITY ACT_IDLE; WAIT 0.25; PLAY_SOUND Target_Acquired; WAIT_RANDOM
0.25`; interrupts `NEW_ENEMY LIGHT_DAMAGE HEAVY_DAMAGE`.
`0x5c _OTHER`: `REMEMBER INVESTIGATING; SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTUNKNOWN;
SET_PRESERVE_PATH 1; SET_NPC_FLAG IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE 0x62; WALK_PATH_HUNT;
WAIT_FOR_MOVEMENT; SET_SCHEDULE 0x62`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR
LIGHT_DAMAGE HEAVY_DAMAGE LOST_UNKNOWN UNKNOWN_RUN_TIMER PLAYER_ON_HEAD`. `0x5e _OTHER_RUN`:
adds `SET_NPC_FLAG FORCE_RELAXED_ANIMS`, `RUN_PATH` for `WALK_PATH_HUNT`; interrupts end at
`LOST_UNKNOWN`. `0x5d _LOST`: `REMEMBER INVESTIGATING; SET_PRESERVE_PATH 0; SET_NPC_FLAG
IGNORE_DOOR_FAILURE; SET_FAIL_SCHEDULE 0x62; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; SET_SCHEDULE
0x62`; interrupts `… HEAVY_DAMAGE INVESTIGATE_SIGHT DETECTED_ATTACK PLAYER_ON_HEAD`.
`0x5f _LOST_RUN`: plus `FORCE_RELAXED_ANIMS`, `RUN_PATH`.
`0x60 _IGNORE`: `STOP_MOVING; SET_NPC_FLAG NO_UNKNOWN_ATTACK; LOOK_AT_BEST_UNKNOWN; WAIT_RANDOM
0.5; PLAY_SOUND Target_Suspect; WAIT_RANDOM 1.5; UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK;
SET_NPC_FLAG FINISHED_IGNORE_UNKNOWN`; interrupts `… HEAVY_DAMAGE UNKNOWN_ADVANCING
PLAYER_ON_HEAD`. `0x61 _OTHER_IGNORE`: `SET_PRESERVE_PATH 0; SET_NPC_FLAG NO_UNKNOWN_ATTACK;
LOOK_AT_BEST_UNKNOWN; WAIT 1.0; WAIT_RANDOM 1.0; UNLOOK_AT; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK;
SET_NPC_FLAG FINISHED_IGNORE_UNKNOWN; SET_SCHEDULE 0x52`.
`0x62 _RETURN`: `REMEMBER INVESTIGATING; PLAY_SEQUENCE ACT_IDLE; SET_ACTIVITY
ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 5; PLAY_SOUND SUSPECT_GIVEUP; WAIT_RANDOM 1; SET_ACTIVITY
ACT_IDLE; WAIT_RANDOM 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; WAIT_RANDOM 1; SET_TOLERANCE_DISTANCE
5; GET_PATH_TO_LASTPOSITION; WALK_PATH; WAIT_FOR_MOVEMENT; FACE_LASTANGLE; CLEAR_LASTPOSITION;
FORGET INVESTIGATING; CLEAR_NPC_FLAG LOOKED_AT_UNKNOWN`; interrupts `… HEAVY_DAMAGE
INVESTIGATE_SOUND INVESTIGATE_SIGHT DETECTED_ATTACK`. `0x63 _RETURN_GIVEUP`: `STOP_MOVING;
SET_PRESERVE_PATH 0; REMEMBER INVESTIGATING; PLAY_SOUND Target_Lost; SET_ACTIVITY
ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 4; WAIT_RANDOM 2; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 1;
SET_NPC_FLAG FORCE_RELAXED_ANIMS; SET_TOLERANCE_DISTANCE 5; GET_PATH_TO_LASTPOSITION; WALK_PATH;
WAIT_FOR_MOVEMENT; FORGET INVESTIGATING; FACE_LASTANGLE; CLEAR_LASTPOSITION; CLEAR_NPC_FLAG
LOOKED_AT_UNKNOWN` (forget before facing, unlike 0x62); interrupts add `IGNORE_UNKNOWN`.
`0x64 INVESTIGATE_NODE`: `SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_PATROL_POINT; SET_NPC_FLAG
FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; FACE_PATROL_INTEREST;
DO_PATROL_INTEREST_ACTIVITY; NEXT_PATROL_POINT`; interrupts `NEW_ENEMY SEE_ENEMY
SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT
IGNORE_UNKNOWN DETECTED_ATTACK PLAYER_ON_HEAD`; `0x66 _WALK` uses `WALK_PATH` without the flag;
`0x68 _HUNT` uses `WALK_PATH_HUNT`. These three are **patrol** programs: `SelectSchedule` case 1
returns `*(uint*)(m_sppPatrolPath(+0x6590) + 4)`, the path object's own id, one of the three
`INVESTIGATE_NODE` or three `FOLLOW_PATROL_PATH` (0x65/0x67/0x69) ids.
The `FOLLOW_PATROL_PATH` blobs (`0x105fb5c0` / `_WALK 0x105fb100` / `_HUNT 0x105fac40`, byte-read
2026-09-12) are the same task lists as the `INVESTIGATE_NODE` three, program for program, and
**none of the six sets a fail schedule**. `TASK_GET_PATH_TO_PATROL_POINT` is Troika `StartTask`
case `0x13`: no path object → `TaskFail(0x1d)`; a node id of −1 returns without completing; else
the node's position for the hull (`0x102fb0d0`) becomes `AI_NavGoal_t{type 4 LOCATION, tolerance
[0x1049a1ac], flags −1}` into navigator `SetGoal` (`0x102ecd20`), and a refusal is
`DevWarning("%s can't reach patrol point")` + `TaskFail(0x0c)`. A program without a fail schedule
routes through `GetFailSchedule` (`0x1028abe0`): `m_failSchedule` (`+0x5c54`, zeroed by every
`SetSchedule 0x10280e50`) or **base schedule `0x43`**, the last id before the Troika block starts
at `0x44`. UNRECOVERED: `0x43`'s task list — the base (`CAI_BaseNPC`) programs are not text blobs
in `vampire.dll` (629 `Schedule` blobs, all Troika/`V*`/crow/manbat/tzimisce/test); only its name
table `0x105d1488` (`SCHED_NONE` first) survives. Port divergence, owned by 0002/10g: `ThinkPatrol`
re-issues the same point's route every think on a refusal, and the kernel's `Fail == None →
State.Clear()` (`ElysiumSchedule.cpp`) re-selects at once where retail runs `0x43`.
`0x80 HUNT_INVESTIGATE`: `SET_FAIL_SCHEDULE SCHED_TROIKA_HUNT_LOOK_AROUND; STOP_MOVING;
SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND; FACE_IDEAL; SET_TOLERANCE_DISTANCE 5;
WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; SET_SCHEDULE HUNT_LOOK_AROUND`; interrupts `NEW_ENEMY
LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT SEE_SOUND_SOURCE IGNORE_UNKNOWN
DETECTED_ATTACK`. `0x81 _UNKNOWN`: `…; GET_PATH_TO_BESTUNKNOWN; SET_PRESERVE_PATH 1; FACE_IDEAL;
SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SET_PRESERVE_PATH 0; SET_SCHEDULE
HUNT_LOOK_AROUND`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE LOST_UNKNOWN`. `0x82 _UNKNOWN_LOST`: `SET_FAIL_SCHEDULE HUNT_LOOK_AROUND;
SET_PRESERVE_PATH 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SET_SCHEDULE
HUNT_LOOK_AROUND`. `0x7f HUNT_INVESTIGATE_FLINCH`: as 0x50, transferring to 0x80.

**Selection.** Two testers, and the distinction is load-bearing: `HasCondition` (`0x10269aa0`)
reads the condition set only; **`HasInterruptCondition` (`0x10269d30`)** requires an installed
schedule, translates through the class id-space (slot `+0x910`), and needs the bit in **both**
the condition set (`+0x5c5c`) and the cached interrupt mask (`+0x5c74`). The driving stimulus is
honoured only when the running program lists it; refinements use plain `HasCondition`.

`CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`) **case 3 (ALERT)**, in order: (1)
`FUN_102b8a60`, the see-unknown selector; (2) `FUN_102b8c40`: `HasInterruptCondition(LIGHT_
DAMAGE 0x4c || HEAVY_DAMAGE 0x4d)` → `m_vSavePosition = +0x5b9c`, `0x8a SCHED_TROIKA_SHOT_BY_
UNKNOWN`; (3) `HasCondition(DETECTED_ATTACK 0x0b)` → `0x56 SCHED_TROIKA_ALERT_TURN_TO_DETECTED_
ATTACK`; (4) `FUN_102b7370` door obstruction; (5) **`FUN_102b9060`, the sound-investigate
selector**; (6) `m_bGoToIdleState = 1; m_bForceStateChange = 1;` → `0x4b SCHED_TROIKA_ALERT_WAIT`.

`FUN_102b9060`:
```
if (HasInterruptCondition(INVESTIGATE_SOUND 0x25)) {
  if (HasCondition(HEAR_COMBAT 0x6d) || HasCondition(HEAR_BULLET_IMPACT 0x70)) {
    m_flNextInvestigateSoundTime(+0x623c) = curtime + DAT_10452dc4;  CommitBestSound();
    m_eAlertLevel(+0x63f4) = 3;
    if (m_afMemory & 0x8000000 INVESTIGATING) return 0x52;
    if (!(m_bfNPCFrenziedFlags(+0x5b84) & 0x10000) && RandomInt(0,99) < 100) return 0x50;  // roll is dead: always
    return 0x51;
  }
  if (HasCondition(HEAR_WORLD 0x6e)) { m_BestSound = m_LastSoundWorld; copy to +0x60dc; return 0x51; }
  if (HasCondition(HEAR_PLAYER 0x6f) || HasCondition(HEAR_DANGER 0x6a)) { CommitBestSound(); return FUN_102b8980(); }
}
if (HasCondition(SEE_SOUND_SOURCE 0x2d) && (r = FUN_102b8d20(this, 0x89, 0x73))) return r;
if (!(m_bfNPCFrenziedFlags & 0x10000) && HasInterruptCondition(HEAR_FLINCH 0x72)) { CommitBestSound(); return 0x50; }
return 0;
```
`FUN_102b8980`, the alert ladder (what `full_investigate` short-circuits): `if
(m_bFullInvestigate +0x6340) m_eAlertLevel = 3; switch (m_eAlertLevel +0x63f4) { 0: level = 1,
return 0x4c SCHED_TROIKA_ALERT_TURN_TO_SOUND; 1: level = 2, return 0x4d _ALERT_STEP_TOWARDS_
SOUND; 2/3: level = 3, return 0x51 + (INVESTIGATING ? 1 : 0) }`. Turn → step → walk/run.
`m_eAlertLevel` is written only by `FUN_102b5dc0` (values 1/2/3) and read only here;
UNRECOVERED: what ever resets it to 0. `FUN_102b8d20(this, hitSched, fearSched)` — the
`SEE_SOUND_SOURCE` third-party tail: `src = m_hBestSoundSource(+0x5b78)`, `foe = src->GetEnemy()`;
`IRelationType(foe)`: D_HT → compute `m_vSavePosition (+0x5dd0)`, return `hitSched` (alert
`0x89 RUN_TO_SAVED`, hunt `0x84 HUNT_RUN_TO_SAVED`); D_FR → `fearSched` (`0x73 FLEE_AND_COWER`);
else bump `m_flNextInvestigateSoundTime` by `DAT_1044eb0c`, 0.

`FUN_102b8a60`, the see-unknown selector: `if (HasInterruptCondition(IGNORE_UNKNOWN 0x03) &&
!INVESTIGATING) return 0x60; if (HasInterruptCondition(SEE_UNKNOWN 0x01) ||
HasInterruptCondition(UNKNOWN_ADVANCING 0x05) || HasCondition(INVESTIGATE_SIGHT 0x26)) {
m_eAlertLevel = 3; if (!INVESTIGATING) return 0x59; if (flags1 & 0x200000 LOOKED_AT_UNKNOWN)
return HasCondition(UNKNOWN_RUN_TIMER 0x04) ? 0x5e : 0x5c; return 0x5a; } if
(HasInterruptCondition(LOST_UNKNOWN 0x02)) return (INVESTIGATING && HasCondition(0x04)) ? 0x5f :
0x5d; if (flags1 & LOOKED_AT_UNKNOWN) return 0x5c; return 0;`. Note `INVESTIGATE_SIGHT` is a
plain `HasCondition` — it need not be in the mask.

**Case 0xb (HUNT)**, in order: `SEE_UNKNOWN` or `INVESTIGATE_SIGHT` (interrupt) → `0x81`;
`FUN_102b8c40` damage; `LOST_UNKNOWN` (interrupt) → `0x82`; any of `INVESTIGATE_SOUND 0x25,
HEAR_DANGER 0x6a, HEAR_COMBAT 0x6d, HEAR_WORLD 0x6e, HEAR_BULLET_IMPACT 0x70, HEAR_PLAYER 0x6f`
(interrupt) → re-arm `+0x623c`, `CommitBestSound`, `(HEAR_COMBAT || HEAR_BULLET_IMPACT)` → `0x7f`
else `0x80`; `HEAR_FLINCH` (interrupt) → `0x50`; `SEE_SOUND_SOURCE && FUN_102b8d20(0x84, 0x73)`;
then the hunt-expiry chain (`0x85/0x7c/0x7d/0x7e`). **The hunt state accepts raw `HEAR_*`
conditions; alert reaches sounds only through `INVESTIGATE_SOUND`, i.e. through the interest
predicate.**

Other entries: `0x5b _ATTACK` from `CAI_BaseNPCTroika::GetSchedule` (`0x102ae920`), combat state
only: `if (flags1 & 0x800000 ATTACK_UNKNOWN) { clear; if (!(flags2 & 0x80 NO_UNKNOWN_ATTACK) &&
!(frenzied & 0x80)) return 0x5b; }`. `0x62` and `0x58` are pure data (reached by
`TASK_SET_SCHEDULE`/`_FAIL_SCHEDULE` only). **Dead in code: `0x53`, `0x54`, `0x61`, `0x63`** — no
immediate in `0x102a0000–0x102c9000`, no blob names them; reachable only by name through
`ChangeSchedule`/`aiscripted_schedule`. UNRECOVERED: the `m_bfNPCFrenziedFlags` bit `0x10000`
that gates the flinch arms; `DAT_10452dc4` (investigate-sound re-arm) and `DAT_1044eb0c`.

**`m_afMemory` bits** (token table `FUN_1030c800`): `PROVOKED 1, INCOVER 2, SUSPICIOUS 4,
PATH_FAILED 0x20, FLINCHED 0x40, TOURGUIDE 0x100, LOCKED_HINT 0x400, TURNING 0x2000, TURNHACK
0x4000, HAD_ENEMY 0x8000, HAD_PLAYER 0x10000, HAD_LOS 0x20000, INVESTIGATING 0x8000000, CUSTOM4
0x10000000, CUSTOM3 0x20000000, CUSTOM2 0x40000000, CUSTOM1 0x80000000`. Three selectors read
`INVESTIGATING` to choose the first-response program over the `_OTHER` one.

**Tasks the port lacks, with arms** (shared registrar ids 0–0x114, Troika 0x115–0x149; `StartTask`
/ `RunTask`): `STORE_LASTPOSITION 0x17` (base `0x10282afd`: `m_vecLastPosition +0x5db8 =
origin; m_angLastAngle +0x5dc4 = angles`), `CLEAR_LASTPOSITION 0x18` (`0x10282b5b`),
`GET_PATH_TO_LASTPOSITION 0x1c` (`0x10285bb0`), `GET_PATH_TO_BESTSOUND 0x20` (`0x10285df8`:
`AI_NavGoal_t{type 4 LOCATION, dest = m_BestSound+0x20, tolerance = [0x1049a160] overriding the
schedule's, flags −1}` → navigator `SetGoal`), `WALK_PATH 0x23` (`0x10286438`), `FACE_IDEAL 0x2b`
(`0x10283cd5` / Troika run `0x102aae43`), `PLAY_SEQUENCE 0x52` (`0x10282dde`/`0x102891c8`),
`FORGET 0x6d` (`0x102829e9`), `GET_PATH_TO_BESTUNKNOWN 0x79` (Troika `0x102a3599`),
`GET_PATH_TO_PATROL_POINT 0x7a` (`0x102a39be`/`0x102ab00f`), `NEXT_PATROL_POINT 0x7d`
(`0x102a3b91`), `FACE_PATROL_INTEREST 0xb3` (`0x102a63bd`/`0x102ab974`),
`DO_PATROL_INTEREST_ACTIVITY 0xb5` (`0x102a64a6`/`0x102aba6c`), `ADD_EVENT_EXPRESSION 0xbe`
(`0x102a4a07`), `SET_PRESERVE_PATH 0xc4` (`0x102a3cfa`), `WALK_RUN_PATH_COMBAT_SOUND 0xd7`
(`0x102a4bd8`: `m_BestSound.type == 1 COMBAT || 0x10 BULLET_IMPACT` → `ACT_RUN` if the model has
it, else `ACT_WALK`; `SetMovementSequence`; clear `MEMORY:INCOVER`), `PLAY_COWER 0xe6`
(`0x102a5125`/`0x102ab83c`), `ALERT_LOOK_AT_BEST_SOUND 0xf9` (`0x102a5515`; shared run
`0x102ab76a`: look at the point, complete when `m_flLookTimer +0x5db4` elapses or the head-turn
virtual reports done, clearing `PLAYING_FACE_ANIM 0x08000000`), `LOOK_AT_PLAYER 0xfb`
(`0x102a5599`), `LOOK_AT_BEST_UNKNOWN 0xfc` (`0x102a55e3`), `UNLOOK_AT 0xfe` (`0x102a56a2`),
`CLEAR_NPC_FLAG 0x101` (`0x102a58ae` — the port's "no program clears a flag" premise is false:
0x60/0x61 clear `NO_UNKNOWN_ATTACK`, 0x62/0x63 clear `LOOKED_AT_UNKNOWN`), `WALK_PATH_HUNT 0x104`
(`0x102a5932`), `FACE_LASTANGLE 0x11d` (`0x102a6fd8`/`0x102ab900`), `PLAY_SOUND 0x11e`
(`0x102a7000`: `g_VSoundTable(0x1073dc28)->PlayNPCSound(this, idx, 2, 1.0, 1.25)` via
`0x101f5950`, start-only), `ALERT_LOOK_AT_UNKNOWN_ATTACKER 0x148` (`0x102a7722`, handle `+0x5b7c`,
`TaskFail(0x21)` when dead).

#### Patrol paths, walked (2026-09-12, story 10g)

**The object.** `m_sppPatrolPath` is a two-word cell at `+0x658c`: a byte "owned" at `+0x658c`
and the `CAI_PatrolPath*` at **`+0x6590`**; `m_sppPatrolPathHunt` is the sibling cell at
`+0x6594`/`+0x6598` (the hunt chain's `MADE_HUNT_PATH` clear in case `0xb` reads `+0x6598 == 0`).
`CAI_PatrolPath` (allocated by `0x10307d30`, freed by `0x10307db0`): `+0 type`, **`+4 schedule
id`**, `+8 repeat count`, `+0xc node count`, `+0x10 current index`, `+0x14 node ids[]`. The
type table at `0x1049df20` (five dwords per row: id, name, first-index cap, step, next type):

| type | name | first index | step | next type |
|---|---|---|---|---|
| 0 | `"0"` | 0 | +1 | 0 |
| 1 | `"1"` | last | −1 | 1 |
| 2 | `"2"` | 0 | +1 | 3 |
| 3 | `"3"` | last | −1 | 2 |

`NextPoint` (`0x10307b80`): `idx += step`; off either end → if `repeat < 1` return **true (path
exhausted)**, else `--repeat`, `type = next`, `idx = first(type)` (`0x10307c20`: `min(count−1,
cap)`). So `0` loops forward, `1` loops backward, `2`/`3` ping-pong, and `repeat` is how many
times the direction may wrap before the path is spent.

**The three writers, all entity inputs** (no keyfield, no spawn path): `InputSetupPatrolType`
(`0x1029eb30`: `"<repeat> <type> <schedule>"`, type through `0x103079c0` — an unknown name is
`Error("Invalid Path Type String…")` — schedule through `0x1029f370`: the name as given, then
`SCHED_%s`, then `SCHED_TROIKA_%s`, translated to the class id space; `0x1029f460(…, repeat,
type, sched, NULL, 1)` creates/resets the object with the schedule id); `InputFollowPatrolPath`
(`0x1029ed90`: a space-separated list of `info_node_patrol_point` / `info_node_hint` names,
each resolved by `0x102d2900` (type `10000 || 800` and `stricmp` on `m_strGroup`), `0x1029f460(…,
0, −1, 0, ids, 0)`: an existing object keeps its type/repeat/schedule and only receives the
nodes; a missing one is created with **schedule 0**); `InputWalkToNode` (`0x1029e840`:
`"<schedule> <node>"`, one node, schedule through `0x102c47e0`, `0x1029f460(…, 0, 0, sched,
[node], 1)`). `0x1029f460` ends with `NextPoint`-style reset (`0x10307b60`) and, **when the
object's schedule id is non-zero, rolls the interest chance (`0x1029f650`) and installs that
schedule at once** through `0x102ae750` (`SetSchedule(int, bForce=0)` → `0x102ae780`: refused
while the NPC or ideal state is dead (7) or the NPC is not alive (slot `0x278`); else
`ForceScheduleChange 0x102ae490` + `SetSchedule 0x10280e50`). So the program starts from the
input, not from the next idle selection; a `FollowPatrolPath` sent without a prior
`SetupPatrolType` builds a path with schedule 0 that the idle selector discards on its next pass
(`"WARNING: Patrol path for '%s' has no schedule."`, `0x1029f5d0` frees it). Also written by the
hunt-list builders `0x10306700` / `0x10306f60` into the hunt cell (`TASK_CREATE_HUNT_PATROL_LIST
0xae` / `TASK_FIND_HUNT_PATROL_TARGET 0xaf`, story 10h). `sp_tutorial_1` sends `SetupPatrolType`
then `FollowPatrolPath` 0.1 s later to `sentry2` and `monk_upstairs_podium` (the Society hub; no
graph node there, so `SetGoal` refuses every point — story 24).

**Selection** (`SelectSchedule` case 1 step 3, `0x102af660`): `+0x6590 != 0` → `path->+4 != 0` →
`0x1029f650(&m_sppPatrolPath)` (the interest roll, below) and **return `path->+4` verbatim**, one
of `0x46 IDLE_PATROL`, `0x64/0x66/0x68 INVESTIGATE_NODE/_WALK/_HUNT`, `0x65/0x67/0x69
FOLLOW_PATROL_PATH/_WALK/_HUNT`, or whatever the input named. `SCHED_TROIKA_IDLE_PATROL`
(`0x10600860`): `TASK_PATROL_PATH 0; WAIT_FOR_MOVEMENT; WAIT_PVS` (interrupts as
`INVESTIGATE_NODE` plus `GIVE_WAY`, `SEE_FEAR` listed twice); `TASK_PATROL_PATH 0x105` (arm
`0x102a594d`) picks activity `0x1115` or `0x1121` by a branch and falls to `ACT 9` when the model
lacks it, then hands the whole node list to the navigator — the base `PATROL_WALK/RUN` shape,
UNRECOVERED beyond the activity pick.

**The tasks.** `TASK_GET_PATH_TO_PATROL_POINT 0x7a` = `0x102aa640(&m_sppPatrolPath)` (Troika
`StartTask` idx 17): no object → `TaskFail(0x1d)`; current node id −1 → `TaskFail(0x1d)` **(not
"returns without completing" as read above: both the null-object and the −1 arms fail with
0x1d; a node index outside the network's array only falls through to the same fail)**; else the
node's hull position (`0x102fb0d0`, `m_eHull +0x1568`) into `AI_NavGoal_t{type 4, tolerance
[0x1049a1ac] = −1 (the schedule's), flags −1}` and `SetGoal(…, 2)`; success → `TaskComplete`,
refusal → `DevWarning("%s can't reach patrol point")` + `TaskFail(0x0c)`. Its `RunTask` arm
(`0x102ab00f` → `0x102aa860`) re-issues the same goal every think while the task still runs — it
never does after the start arm completed or failed, so it is dead for this task and live only for
`0x7b GET_PATH_TO_PATROL_POINT_HUNT` (start `0x102a39d9`, run `0x102ab02a` on the hunt cell).
`TASK_NEXT_PATROL_POINT 0x7d` = `0x102aa9e0` (idx 20): no object → `TaskFail(0x1d)`; `NextPoint`
true → free the object (`0x1029f5d0`); then the interest roll `0x1029f650` and `TaskComplete`.
`0x7e NEXT_PATROL_POINT_HUNT` (`0x102a3bac`) is the hunt twin. `0x7c GET_FULL_PATROL_PATH`
(`0x102a39f4`) hands the list to the navigator as one route (UNRECOVERED body).

**The interest roll and the two interest tasks.** Each patrol node may carry an interest record
(`node->+0xa0`, from `info_node_patrol_point`'s keys: `+0x468` the interesting place's name,
`+0x46c` a 0–99 chance). `0x1029f650`: `m_bPatrolInterest (+0x65a0) = RandomInt(0,99) <
chance`, evaluated once per selection and once per `NEXT_PATROL_POINT`. `0x1029f730` resolves the
record (cached at `+0x659c`) only while `+0x65a0` is set; `0x1029f780` resolves the record's
named entity to its `CAI_InterestingPlace` (`+0x6300`, the same field the interesting-place
programs use). `TASK_FACE_PATROL_INTEREST 0xb3` (start `0x102a63bd`): no place, or the place's
`match_orientation +0x570` clear → `+0x6300 = +0x659c = 0`, `TaskComplete`; else
`m_pMotor->SetIdealYaw(hint yaw 0x102d12e0)` and keep running — `RunTask 0x102ab974`: no place →
complete; else set the turn activity (slot `0x8f0`) unless `MEMORY:TURNING 0x2000`, and when
`FacingIdeal` (`0x10278c80`) clear both fields and complete. `TASK_DO_PATROL_INTEREST_ACTIVITY
0xb5` (start `0x102a64a6`): no place → fall through to `TaskComplete`; else `0x102a9f40(place,
record, 0)` — the same claim/INTO/`m_flWaitFinished = RandomFloat(min_time, max_time)` entry the
interesting-place tasks use; `RunTask 0x102aba6c`: `0x102aa210(place)` (the INTO→IDLE→OUTOF
loop) true → holster if the place's `+0x571` says so, fire `m_OnInterestingPlaceLeft` when
arrived, release (`0x102da600`), clear `+0x6300`/`+0x659c`/`m_bInterestingPlaceArrived`,
complete. With no interest record both tasks complete on their first think, which is every
patrol in the shipped corpus that authors none.

**Port divergences (owned by 10g).** The port has no `CAI_PatrolPath`; `ThinkPatrol` re-issues
a refused point every think and the three inputs are not wired to a program install. Unrecovered
after this pass: `0x7c`'s body, `TASK_PATROL_PATH`'s navigator call after the activity pick, the
`info_node_patrol_point` keys that fill `+0x468`/`+0x46c`.

#### The alert programs, verbatim (2026-09-12, story 10d)

Byte-read from the blobs; the ids from the registrar (`FUN_102b9810`, 262 pairs decoded from
its `mov [esp+0x10]/[esp+0x14]` and `push/push` forms — the table the section above numbered by
hand is confirmed entry for entry).

- `0x4b SCHED_TROIKA_ALERT_WAIT` (`0x105fffe8`): `TASK_RUN_DISPOSITION 5`; interrupts `NEW_ENEMY
  SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND
  INVESTIGATE_SIGHT IGNORE_UNKNOWN DETECTED_ATTACK PLAYER_ON_HEAD`. `TASK_RUN_DISPOSITION 0xba`
  (start `0x102a49bc`, run `0x102ab351`) is the disposition stance machine
  (`animation_and_movers.md`) run for the operand's seconds; the selector sets `m_bGoToIdleState`
  and `m_bForceStateChange` beside it, so the alert state ends with the program.
- `0x4c SCHED_TROIKA_ALERT_TURN_TO_SOUND` (`0x105ffd78`): `PAUSE_MOVING 0; SET_NPC_FLAG
  NO_UNKNOWN_ATTACK; ALERT_LOOK_AT_BEST_SOUND 0; WAIT_RANDOM 0.5; PLAY_SOUND Target_Suspect;
  WAIT_RANDOM 1.5; UNLOOK_AT 0; CLEAR_NPC_FLAG NO_UNKNOWN_ATTACK`; interrupts `NEW_ENEMY
  SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE REPEATED_DAMAGE SEE_SOUND_SOURCE
  PLAYER_ON_HEAD`. The turn rung neither stores a last position nor remembers `INVESTIGATING`.
- `0x4d SCHED_TROIKA_ALERT_STEP_TOWARDS_SOUND` (`0x105ffa68`): `STOP_MOVING; STORE_LASTPOSITION;
  REMEMBER INVESTIGATING; ALERT_LOOK_AT_BEST_SOUND; WAIT_RANDOM 0.5; PLAY_SOUND Target_Suspect;
  WAIT_RANDOM 1.5; SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_BESTSOUND; FACE_IDEAL;
  **WALK_PATH_TIMED 2**; SET_SCHEDULE ALERT_LOOK_AROUND`; interrupts `NEW_ENEMY SEE_ENEMY
  SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE INVESTIGATE_SOUND SEE_SOUND_SOURCE
  PLAYER_ON_HEAD` (the `0x51` set). `TASK_WALK_PATH_TIMED 0x24`
  is base (`WALK_PATH` for the operand's seconds, then complete) — the "step" is two seconds of
  walking toward the sound, and there is no `WAIT_FOR_MOVEMENT`.
- `0x4e SCHED_TROIKA_ALERT_LOOK_AROUND` (`0x105ff680`), the sound family's fail and exit program:
  `REMEMBER INVESTIGATING; PLAY_SEQUENCE ACT_IDLE; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND;
  WAIT 5.00; PLAY_SOUND SUSPECT_GIVEUP; WAIT_RANDOM 1.00; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM
  0.00; SET_NPC_FLAG FORCE_RELAXED_ANIMS; WAIT_RANDOM 1.0; SET_TOLERANCE_DISTANCE 5;
  GET_PATH_TO_LASTPOSITION; WALK_PATH; WAIT_FOR_MOVEMENT; FACE_LASTANGLE; CLEAR_LASTPOSITION;
  FORGET INVESTIGATING`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
  HEAVY_DAMAGE INVESTIGATE_SOUND SEE_SOUND_SOURCE PLAYER_ON_HEAD`. It is `0x62 _RETURN` without
  the `LOOKED_AT_UNKNOWN` clear. Note it walks back to `m_vecLastPosition`, which `0x4c` never
  stored: reached from `0x4c`'s failure (no `SET_FAIL_SCHEDULE` there → `FAIL`) it is not; reached
  from `0x4d`/`0x51`/`0x52` it is.
- `0x56 SCHED_TROIKA_ALERT_TURN_TO_DETECTED_ATTACK` (`0x105fe3f8`): as `0x4c` with
  `TASK_ALERT_LOOK_AT_DETECTED_ATTACK 0xfd` (start `0x102a5662`, shared run `0x102ab76a`) in place
  of the best-sound look; same interrupts.
- `0x8a SCHED_TROIKA_SHOT_BY_UNKNOWN` (`0x105f68c8`): `SET_NPC_FLAG DONT_INVESTIGATE;
  SET_TOLERANCE_DISTANCE 12; FIND_COVER_FROM_SAVEPOSITION 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS;
  RUN_PATH; WAIT_FOR_MOVEMENT; GET_PATH_TO_SAVEPOSITION 0; FACE_PATH 0; CLEAR_NPC_FLAG
  DONT_INVESTIGATE; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 5; WAIT_RANDOM 5;
  SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 5; WAIT 5; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND;
  WAIT 5; WAIT_RANDOM 5; SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 5; WAIT 20`; interrupts `NEW_ENEMY
  SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR INVESTIGATE_SIGHT INVESTIGATE_SOUND DETECTED_ATTACK`. Its
  selector `FUN_102b8c40`: `HasInterruptCondition(LIGHT_DAMAGE 0x4c) || (HEAVY_DAMAGE 0x4d)` →
  `m_bCondTookDamage (+0x5b80) = 0`, `m_vSavePosition = m_vecLastDamagePosition (+0x5b9c)`,
  `0x8a`; else 0. Shared with the hunt case.
- `0x89 SCHED_TROIKA_RUN_TO_SAVED` (`0x105f6d58`), the `SEE_SOUND_SOURCE` third-party answer and
  `GetSchedule`'s `m_fSavePositionWalk` answer: `SET_TOLERANCE_DISTANCE 24;
  GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH;
  WAIT_FOR_MOVEMENT; SET_TOLERANCE_DISTANCE 24; GET_PATH_TO_SAVEPOSITION 0; WALK_PATH_HUNT;
  WAIT_FOR_MOVEMENT; SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 3; WAIT_RANDOM 5;
  SET_ACTIVITY ACT_IDLE; WAIT_RANDOM 1; GET_PATH_TO_RANDOM_NODE 2048; WALK_PATH;
  WAIT_FOR_MOVEMENT`; interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR`.
- `0x48 SCHED_VTROIKA_TURN_TO_SOUND` (`0x10600428`, the flee state's sound answer): as `0x4c` with
  `TASK_LOOK_AT_BEST_SOUND 0xf8` (start `0x102a5515`, the same address as `0xf9`'s — one arm
  serves both ids) and `WAIT_RANDOM 1.5` alone (no `PLAY_SOUND`, no 0.5 wait).
- `0x105fdf58 SCHED_TROIKA_ALERT_COVER_FROM_UNKNOWN_ATTACKER`: `SET_FAIL_SCHEDULE 0x58;
  SET_TOLERANCE_DISTANCE 0; FIND_COVER_FROM_UNKNOWN_ATTACKER; SET_NPC_FLAG FORCE_RELAXED_ANIMS;
  RUN_PATH; WAIT_FOR_MOVEMENT;` then two lookaround/idle wait pairs; interrupts `SEE_ENEMY
  SQUAD_SEE_ENEMY SEE_FEAR ENEMY_DEAD ENEMY_UNREACHABLE CAN_RANGE_ATTACK1 CAN_MELEE_ATTACK1
  CAN_RANGE_ATTACK2 CAN_MELEE_ATTACK2 LOST_ENEMY`. No selector in `0x102a0000–0x102c9000`
  returns its id; data-only like `0x58`.
- `SCHED_TROIKA_ALERT_FACE` (`0x10600150`): `STOP_MOVING; SET_ACTIVITY ACT_IDLE; FACE_IDEAL`;
  interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE
  PLAYER_ON_HEAD`. No selector returns it.

`0x4c`, `0x4d` and `0x56` declare no fail schedule: a failed `GET_PATH_TO_BESTSOUND` in `0x4d`
runs `FAIL` (story 25), not `ALERT_LOOK_AROUND`. `m_eAlertLevel`'s reset question is settled in
"Sense and investigate leftovers" (nothing resets it). Unrecovered after this pass: the
`TASK_RUN_DISPOSITION` arms' body, `TASK_FIND_COVER_FROM_SAVEPOSITION` /
`GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK` / `FACE_PATH` arms (`0x8a`, `0x89` — combat-cover
vocabulary, 12b's neighbourhood).

#### The hunt programs and the expiry chain, verbatim (2026-09-12, story 10h)

Case `0xb`'s tail after the sound arms: `SEE_SOUND_SOURCE && FUN_102b8d20(0x84, 0x73)`; then
`+0x6598 == 0 → flags1 &= ~0x1000 (MADE_HUNT_PATH)`; `MADE_HUNT_PATH` clear → (`m_flHuntExpireTimer
<= curtime` → `0x85 HUNT_FINISH`; slot 168's enemy alive → `0x7c HUNT_SETUP`; else `0x7d
HUNT_SETUP_NO_ENEMY`); set → `0x7e HUNT`.

- `0x7c SCHED_TROIKA_HUNT_SETUP` (`0x105f8dc0`): `SET_FAIL_SCHEDULE HUNT_FAILED; STOP_MOVING;
  SET_ACTIVITY ACT_IDLE; STORE_LASTPOSITION; REMEMBER INVESTIGATING; SET_TOLERANCE_DISTANCE 20;
  GET_PATH_TO_LASTENEMY_LKP; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; FIND_HUNT_PATROL_TARGET;
  SET_NPC_FLAG MADE_HUNT_PATH; WAIT 1`.
- `0x7d _SETUP_NO_ENEMY` (`0x105f8b50`): `SET_FAIL_SCHEDULE HUNT_FAILED; STOP_MOVING; SET_ACTIVITY
  ACT_IDLE; STORE_LASTPOSITION; FIND_HUNT_PATROL_TARGET; SET_NPC_FLAG MADE_HUNT_PATH; WAIT 1`.
- `0x7e SCHED_TROIKA_HUNT` (`0x105f8918`): `SET_FAIL_SCHEDULE HUNT_FAILED; SET_TOLERANCE_DISTANCE
  20; GET_PATH_TO_PATROL_POINT_HUNT; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; NEXT_PATROL_POINT_HUNT`.
- `0x85 SCHED_TROIKA_HUNT_FINISH` (`0x105f7820`): `CLEAR_NPC_FLAG MADE_HUNT_PATH; STOP_MOVING;
  SET_ACTIVITY ACT_IDLE; SET_TOLERANCE_DISTANCE 20; GET_PATH_TO_LASTPOSITION; SET_NPC_FLAG
  FORCE_RELAXED_ANIMS; WALK_PATH_HUNT; WAIT_FOR_MOVEMENT; FACE_LASTANGLE; FORGET INVESTIGATING;
  SUGGEST_STATE STATE:ALERT`. `SCHED_TROIKA_HUNT_FAILED` (`0x105f74f8`) is identical with
  `WALK_PATH` for `WALK_PATH_HUNT`.
- `0x84 HUNT_RUN_TO_SAVED` (`0x105f7b28`): `0x89`'s first nine tasks (through the second
  `WAIT_FOR_MOVEMENT`), interrupts `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR`.
- `HUNT_LOOK_AROUND` (`0x105f7d70`, the `0x80/0x81/0x82` exit): `PLAY_SEQUENCE ACT_IDLE;
  SET_ACTIVITY ACT_ALERT_FIDGET_AGRO_LOOKAROUND; WAIT 3; WAIT_RANDOM 3; SET_ACTIVITY ACT_IDLE;
  WAIT_RANDOM 1`. `HUNT_TURN_LEFT` / `_RIGHT` (`0x105f72d8` / `0x105f70b8`): `PLAY_SEQUENCE
  ACT_ALERT_L45_INTO (R45); WAIT_RANDOM 2; PLAY_SEQUENCE ACT_ALERT_L45_OUTOF (R45); SET_ACTIVITY
  ACT_IDLE; WAIT_RANDOM 0`; no selector returns either.

All of these share the interrupt set `NEW_ENEMY SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE INVESTIGATE_SOUND INVESTIGATE_SIGHT IGNORE_UNKNOWN DETECTED_ATTACK` (the run-to-saved
pair excepted). The hunt patrol list is a second `CAI_PatrolPath` in the `+0x6594` cell built by
`TASK_FIND_HUNT_PATROL_TARGET 0xaf` (`0x10306f60`) / `TASK_CREATE_HUNT_PATROL_LIST 0xae`
(`0x10306700`) with type 0 and no schedule, walked by the `_HUNT` twins of the patrol tasks.
Unrecovered: the two list builders' node choice, `GET_PATH_TO_LASTENEMY_LKP`, `m_flHuntExpireTimer`'s
writer.

## The flee state and the cower, disoriented and lost programs (2026-09-08)

Blob census: 691 blobs; 62 carry bare names without `SCHED_` (the base table's `COWER`,
`STANDOFF`, `DIE`…). The registration pair comes in three codegens (`mov [esp+0x10/+0x14]`,
other displacements, and `push id; push name`); the nearest preceding pair self-verifies on 617
of 627 named blobs.

| id | name | blob |
|---|---|---|
| 0x70 / 0x71 | SCHED_TROIKA_FLEE_AND_COWER_TURN_TO_PLAYER / _NEAR | 0x105fa230 / 0x105fa0a0 |
| 0x72 | SCHED_TROIKA_FLEE_AND_COWER_SCREAM | 0x105f9f60 |
| 0x73 | SCHED_TROIKA_FLEE_AND_COWER | 0x105f9d28 |
| 0x74 / 0x75 | _STALL / _STALL_FAILED | 0x105f9b28 / 0x105f9978 |
| 0x76 | _NO_ENEMY | 0x105f9740 |
| 0x77 / 0x78 | SCHED_TROIKA_COWER / _HINT | 0x105f9598 / 0x105f9410 |
| 0x109 / 0x10a / 0x10b | SCHED_TROIKA_COWER_SIMPLE / _HINT / _NOSEE | 0x105e5788 / 0x105e5590 / 0x105e5398 |
| 0x12d / 0x12e | SCHED_TROIKA_DISORIENTED / SCHED_TROIKA_LOST | 0x105dfe28 / 0x105dfca0 |

Neighbours: `0x6f FLEE_AND_DIE`, `0x107 FLEE`, `0x108 FLEE_RANDOM`, `0x12f COMFORT`, `0x130
CALMED`, `0x132 LAUGHING`. The base-class `COWER` (`0x10604f98`: `STOP_MOVING; PLAY_SEQUENCE
ACT_COWER`) is Source's `SCHED_COWER`; its base id is UNRECOVERED (compiled-in enum).

**Programs.** `0x12d DISORIENTED`: `SET_NPC_FLAG DONT_INVESTIGATE; SET_NPC_FLAG NO_DIALOG;
SET_FAIL_SCHEDULE Idle_Stand; SET_PRESERVE_PATH 0; STOP_MOVING; PLAY_SEQUENCE ACT_DISORIENTED;
WAIT_PVS`; no interrupts; `DELAY_INTERRUPTS`. `0x12e LOST` identical with `ACT_LOST`. Their
effective mask is `{NPC_FREEZE}` alone (`DONT_INVESTIGATE` suppresses the overlay) and
`WAIT_PVS` holds them until the body enters the player's PVS.
`0x109 COWER_SIMPLE`: `CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_NPC_FLAG COWERING; SET_NPC_FLAG
ONE_HIT_KILL; PLAY_COWER ACT_COWER_INTO; SET_COWER ACT_COWER; WAIT 10; WAIT_RANDOM 20;
SUGGEST_STATE STATE:IDLE`; interrupts `SEE_ENEMY SEE_FEAR SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE
REPEATED_DAMAGE INVESTIGATE_SOUND` (`SEE_FEAR` twice, verbatim). `0x10a`/`0x10b`: same tasks,
empty interrupts. `0x77 COWER`: `SET_FAIL_SCHEDULE 0x109; FLIP_NEXT_IDEAL_YAW 1; FACE_ENEMY;
FLIP_NEXT_IDEAL_YAW 0; SET_SCHEDULE 0x109` (turn the back to the enemy); interrupts damage +
`DETECTED_ATTACK`. `0x78 COWER_HINT`: `SET_FAIL_SCHEDULE 0x10a; FACE_HINTNODE; SET_SCHEDULE
0x10a`. `0x73 FLEE_AND_COWER`: `SET_NPC_FLAG IN_FLEE_SCHED; SET_FAIL_SCHEDULE 0x74;
SET_TOLERANCE_DISTANCE 12; GET_PATH_TO_COWER_NODE; RUN_PATH_FLEE; WAIT_FOR_MOVEMENT;
SET_PRESERVE_PATH 0; CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_SCHEDULE 0x77`; interrupt
`COVER_FAILURE`. `0x74 _STALL`: fail `0x10b`, `GET_PATH_TO_RANDOM_NODE 1024`. `0x76 _NO_ENEMY`:
`GET_PATH_TO_COWER_NODE_SAVE_POS`, fail `0x74`. `0x75 _STALL_FAILED`: `SET_NPC_FLAG IN_FLEE_SCHED;
SET_ACTIVITY ACT_IDLE; WAIT 2; WAIT_RANDOM 1; CLEAR_NPC_FLAG IN_FLEE_SCHED; SET_SCHEDULE 0x77`.
`0x72 _SCREAM`: `SET_NPC_FLAG IN_FLEE_SCHED; STOP_MOVING; WAIT_RANDOM 0.2; SET_PRESERVE_PATH 1;
SET_SCHEDULE 0x73`. `0x70/0x71 _TURN_TO_PLAYER(_NEAR)`: `SET_NPC_FLAG IN_FLEE_SCHED;
PAUSE_MOVING; LOOK_AT_PLAYER 0.2 (0.1); SET_PRESERVE_PATH 1; SET_SCHEDULE 0x73`.

**Selection.** The flee state is `m_NPCState == 8`, entered only in `CAI_BaseNPCTroika::
SelectIdealState` (`0x102ad660`) from idle and alert on `COND_SUPERNATURAL_FLEE_LEVEL 0x21` or
`COND_CRIMINAL_FLEE_LEVEL 0x1f` (setting `flags1 |= 0x100 INITIAL_FLEE`); `case 8` returns 8 —
**terminal**; `SetState` writes `m_bfNPCStateFlags = 0x85` for it. `SelectSchedule` case 8, in
order: `COVER_FAILURE 0x39` → `0x73`; none of `SEE_ENEMY, SEE_FEAR, LIGHT/HEAVY/REPEATED_DAMAGE,
0x21, 0x1f` → (`DETECTED_ATTACK` → `0x56`; `INVESTIGATE_SOUND` → re-arm `+0x623c`, clear
`INITIAL_FLEE`, `CommitBestSound`, `0x48 SCHED_VTROIKA_TURN_TO_SOUND`; else `0x77 COWER`);
`INITIAL_FLEE` not set → `0x73` (every pass after the first); clear it, `m_flNextFleeSoundTime =
curtime + RandomFloat(10, 20)`, the flee vocalisation (slot `+0x7c8`); damage → `0x72`; no law
level and `SEE_FEAR` → slot `0x950`(`m_hLastSeenFearEnt`), `0x72`; supernatural branch (tested
before criminal): offender valid → slot `0x950`; closest player ≠ offender → offender valid ?
`0x73` : (`m_vSavePosition = m_vecPLSupernaturalLocation`, `0x76`); `MakeAISound(type 8, player
eye, DAT_1072bc88, 10.0, DAT_1072bcc2)`; `ReportSupernaturalAct` (`0x1017f4a0`) unless
`m_bPLSupernaturalActFleeOnly` (then `0x1017fd60` only with `SEE_PLAYER`); `m_flPlayerDist <
DAT_10483aac && RandomInt(0,99) < 80` → `0x71` else `0x70`; criminal branch mirrors it with
`m_hCriminalOffender`, `ReportCriminalAct` (`0x1017f2a0`), `m_vecPLCriminalLocation`.
`TranslateSchedule` (`0x102b12f0`): `0x77` → `0x78` when `m_pHintNode` is type `0x2774`; `1`/`0x6b`
→ `0x132 LAUGHING` under `D_MILDLY_CRAZY 0x80000`. `GetSchedule` (`0x102ae920`) idle chain:
`KNOCKBACK 0x28` → `0x14c`; `COMFORT 0x27` → `0x12f`; `D_CALM 0x10000` → `0x130`; `D_FOLLOW
0x100000` → `0x131`; `D_POSSESSED 0x40000` → `0x131`; dialogue partner → `0x6a`. `D_AFRAID` is not
a flag but `SCHED_TROIKA_D_AFRAID` (`0x105db1f8`), a discipline-installed flee leg ending in
`0x77`. **Nothing selects `0x12d` or `0x12e`**: `DISORIENTED` is the terminal `SET_SCHEDULE` of
16 programs (`D_MESMERIZE`, `D_PURGE`, `D_BLOODSHOT_BOSS`, `D_HALLUCINATION`, `D_HYSTERIA`,
`D_TRANCE`, `D_BLOODSUCKERS_COMMUNION`, `LAUGHING`, `DO_BLOODBOIL/VOMIT/THEFTOFVITAE/BLOODYEYE/
MADNESS/HAUNTING/SLEEP/MESMERIZE_ACTIVITY`) and of five discipline `HitInfo` `AI_Schedule`
keys in retail `pack101.vpk`; **`LOST` has no producer anywhere in the shipped game.** `FLEE`/
`FLEE_RANDOM` are likewise never returned by a selector. `CNPC_VHuman`/`CNPC_VPedestrian`
selectors do not touch the family.

**Activities and the feed.** `ACT_DISPOSITION_MESMERIZED 0x104e`, `ACT_DISORIENTED 0x1068`,
`ACT_LOST 0x1069`, `ACT_COWER_INTO 0x1097`, `ACT_COWER 0x1098`, `ACT_COWER2 0x109b`,
`ACT_COWER3 0x109e`. **`AttemptFeed` (`0x10168910`) reads the victim's `m_IdealActivity`
(`+0xff0`)**, not the current activity, and auto-accepts on `0x104e || 0x1068 || 0x1069 ||
0x1098` only. `TASK_PLAY_COWER` (`0x102a5125`) rolls `m_iCowerAnimOffset +0x6414 = RandomInt(0,2)
× 3` on `ACT_COWER_INTO` and `TASK_SET_COWER` (`0x102a516c`) reuses it, so the loop activity is
`ACT_COWER`/`COWER2`/`COWER3`: **a cowering NPC auto-accepts a feed one time in three**; the other
variants and the `_INTO` window go through the opposed roll. This corrects `feeding.md`'s
activity-based reading.

**Flags the family writes.** `COWERING` (bit 10): readers `BuildScheduleTestBits` (no `COMFORT`
while cowering) and the hearing quarter-radius. `ONE_HIT_KILL` (bit 30): one reader,
`OnTakeDamage` `0x102beda0` — any non-light hit kills outright (slot `0x240`). `IN_FLEE_SCHED`
(bit 7): set by every flee leg, cleared first thing by `COWER_SIMPLE*` so the interest overlay
re-enables while cowering.

**Tasks missing from the port** (Troika StartTask / base StartTask / Troika RunTask / base RunTask;
`—` = default arm): `SUGGEST_STATE 0x06` (`0x102a1b2b` / `0x10286c0d`; reads `frenzied & 1`,
routes `STATE:` into the ideal-state request), `GET_PATH_TO_RANDOM_NODE 0x1f` (— / `0x10285d7f` /
— / `0x10289718`), `FACE_HINTNODE 0x2f` (`0x102a382a` / `0x10283a36` / — / `0x10288b4c`),
`PLAY_SEQUENCE 0x52`, `GET_PATH_TO_COWER_NODE 0x84` (`0x102a2882`: threat = enemy or self, radius
= operand, node query through slot `0x688` into `+0x5ddc`), `_SAVE_POS 0x85` (`0x102a2bd8`,
anchored on `m_vSavePosition +0x5dd0`), `PAUSE_MOVING 0xa6` (`0x102a1f06`), `SET_PRESERVE_PATH
0xc4` (`0x102a3cfa`: `0` → `flags1 &= ~0x08`, else `|= 0x08`), `PLAY_COWER 0xe6` (`0x102a5125` /
run `0x102ab83c`), `SET_COWER 0xe7` (`0x102a516c` / run `0x102ab4b2`), `LOOK_AT_PLAYER 0xfb`,
`CLEAR_NPC_FLAG 0x101`, `RUN_PATH_FLEE 0x103` (`0x102a58d7`: `ACT_PANIC_RUN 0x1093`, else activity
`0x13`), `FLIP_NEXT_IDEAL_YAW 0x106` (`0x102a59eb`). **`DAT_10483aac = 512.0f`** (the "player near" distance for `0x71` vs `0x70`).
`DAT_1072bc88`/`DAT_1072bcc2` are `.data` cells filled at startup from `sound_volume_table.txt`
(the flee sound's radius row and type byte) — read them from the table, not the image.

**Story 21a recovery (2026-09-12).** The table cells: `CSoundVolumeTable` (loader `0x101af9f0`,
`"VDATA\System\sound_volume_table.txt"`, blocks `VolumeLevels`, `OccludedVolumeLevels`,
`SoundTypes`, `MiscData`) is the object at **`0x1072bc20`**: `SoundTypes` (`0x101afcf0`) walks the
34-name table `0x10597c4c` in file order and writes, per type index, the radius dword at `base +
4·i` (the `VolumeLevels` row the type's volume names) and the occludable byte at `base + 0x88 +
i`. So `DAT_1072bc88` / `DAT_1072bcc2` are index **26 = `NPC_FLEE`**, volume `"5"` → **240
units, occludable 1**; and, correcting the disciplines section, `DAT_1072bc40` / `0x1072bcb0`
are index 8 **`PLAYER_GUNSHOT_PISTOL`** (`"3"` → 1200) and `DAT_1072bc58` / `0x1072bcb6` index 14
**`BULLET_IMPACT`** (`"7"` → 250, occludable), not `NPC_DISCIPLINE_ALERT` (index 27, `0x1072bc8c`
/ `0x1072bcc3`). The `MakeAISound` first argument `8` is the `CSound` type bit, not a row.

`+0x6364` is the witnessed criminal level, obfuscated: the one writer is **`0x1028ea60(level,
location, offender)`** — called three times by the player-law sweep `0x1028efc0` and by
`CNPC_VPedestrian` slots 433/461 — which stores `encode(level)` (`0x1042fde0`, then the XOR/ADD
mix with `0x68d8635 / 0xae8746f / 0xffa91d8 / 0x197279ca / 0xa641cacd`), two bytes at
`+0x6360/+0x6361`, `m_vecPLCriminalLocation +0x6380` and the offender handle `+0x638c`; every
reader inverts the mix and calls `0x1042fe90`. The supernatural twin
(`m_iPLSupernaturalLevelWitnessed`) is stored plain. The hint types: `0x2774` = 10100
`info_hint`, `0x27d8` = 10200 `info_node_cover_corner` ("The navigation and reaction keyfields",
`CNodeEnt::Spawn`); `TranslateSchedule`'s `0x77 → 0x78` therefore fires when the held hint is an
`info_hint`. The base `SCHED_COWER` id is **`0x1e`** ("The kernel's failure route and the base
programs"). UNRECOVERED after this pass: the two bytes `+0x6360/+0x6361`, `0x1042fde0`'s
transform.

## The cover and kick chooser, and the combat leftovers (2026-09-08)

**`FUN_102b7690(bCover, bCorner, bKickOver, bKickAt)`**, callers `SelectSchedule 0x102ae920`
`(1,0,0,0)`, the dodge selector `0x102b7cf0` `(1,1,1,1)`, the melee selectors `0x10385e40` /
`0x10396050` `(0,1,0,1)`, every call gated on **slot 592 `CanSeekCover()` `0x102953e0`**:
`COND_ENEMY_OCCLUDED 0x48` → true; `m_flCanSeekCoverTimer ≤ curtime` → true; `timer − 1.0 <
curtime && !COND_CAN_RANGE_ATTACK1 0x4f` → true; else false (sole override `CNPC_VLasombra`
`0x103893c0`: true while `m_flCoverDisableOverride` is in the future).
- Arm A, kick-prop acquisition: `bKickAt && allow_kick_hint_use && !m_pHintNode && !(flags1 &
  0x800 DODGING) && no prop && curtime ≥ +0x6438` → `+0x6438 = curtime + 2.0`; `m_hKickPhysicsProp
  +0x643c = FUN_102b6650` (needs an enemy; `UTIL_EntitiesInBox` ±512/±512/±64, cap 20,
  `m_edtDerivedType & 4` PHYSICS_PROP; **`prop->m_bNpcKickable +0x788`** (key `npc_kickable`);
  nearest by 2-D distance; `0x102b62e0`).
- Arm B: a live prop → **`0xa9 KICK_PROP_AT_ENEMY`**, before any hint search.
- Arm C: `!m_pHintNode && !DODGING && no prop` → `+0x6448 = enemy`; mask `cover 1 | corner 2 |
  (kickOver && flag) 4 | (kickAt && flag) 8` → `FUN_102b7110` (search `0x102d2980` flags 8,
  distance slot 550 = 1024, one retry when `stay_entrenched`; on success `m_iPeekOutCount +0x640c
  = 0`, `m_iFailedCoverLOSChecks +0x6404 = 0`, reserve `0x102d1350`, corner lean side `+0x63fd`
  from the 2-D cross of enemy−hint with the hint yaw). Category bits: 100/101/10200 → 1,
  10300 → 4, 10301 → 8, 10400 → 0x10, 10000/10100 none; **mask bit 2 matches nothing**, so the
  melee call can only return a kick-at node.
- Arm D on `m_pHintNode->m_nHintType`: 10300 → **`0xa7 HINT_KICK_OVER`**; 10301 → **`0xa8
  HINT_KICK_AT_ENEMY`**; 100/101/10200 → the cover arm; else 0. Cover arm: not `AT_COVER_HINT
  0x2000` → clear `COVER_VS_MELEE_MODE` (flags2 `0x100`), **`0x9b TAKE_COVER_HINT`**; else
  `bMeleeThreat = !(enemy's weapon flags & 0x6000)`; slot 609 `0x102b6b50` (the shoot-at hint,
  type 10400, retry `+0x6440 = curtime + RandomFloat(2, 2.5)`) → **`0xa2 …_SHOOT_AT_HINT`**; else
  `0x102b5de0` (can I still shoot from cover): false → `m_iPeekOutCount++`; `< 5 && !0x48` →
  **`0x9d PEEK_OUT`**; else clear the mode, `stay_entrenched ? 0x9e PEEK_OUT_WAIT :
  ClearHintNode(60) + 0`; true → `peek = max(peek − 2, 0)`, `roll = RandomInt(0, 99)` (drawn
  before the branch); ranged enemy: not already in `0x9e` and mode clear → **`0xa3
  …_HINT_ATTACK`**, else clear the mode and `roll > 29 ? 0xa0 PEEK_OUT_FIRE : 0xa1
  PEEK_OUT_RETURN`; melee enemy: same guard → **`0xa4 …_VS_MELEE`**, else set the mode →
  **`0xa5 …_VS_MELEE_ATK`**. `ClearHintNode(t)` `0x10295ab0`: 60 s hint cooldown
  (`m_flNextUseTime +0x5ec`), clear `AT_COVER_HINT`, reset the attack extents.
- Programs: `0xa9`: `SET_TOLERANCE_DISTANCE 0; GET_PATH_TO_KICK_PROP; SET_NPC_FLAG
  FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT; SNAP_TO_KICK_PROP; KICK_PROP; CLEAR_NPC_FLAG
  FORCE_RELAXED_ANIMS`, interrupts `NEW_ENEMY, KICK_PROP_INVALID`; `0xa7` ends `FACE_HINTNODE;
  KICK_HINT`, `0xa8` `FACE_ENEMY; KICK_HINT_AT`, interrupts `NEW_ENEMY, HINT_INVALID`. `0xa4` is
  the only program that sets `COVER_VS_MELEE_MODE` by task.

**The kick predicate `0x102b62e0`**: 2-D dot of the 3-D-normalised `(prop − me)` and `(enemy −
prop)` **> cos 10°** (`0x1049ae94`), then a clear trace from `(prop.x, prop.y, prop.z + 8)` to the
enemy's slot-192 point, mask `0x200400b`, `fraction ≥ 1 && !allsolid && !startsolid`.
`m_bNpcKickable`'s only code writer is `TaskFail` `0x1029adb0` (zeroes it and drops the handle):
a kickable prop is consumed by the first task failure of its kicker. Retail authors `npc_kickable
1` on six barrels (`sm_junkyard_1` ×5, `sm_warehouse_1`). **`COND_KICK_PROP_INVALID 0x2a` has no
producer** (dead interrupt); `COND_HINT_INVALID 0x29` is set at `0x10293160`, `0x102d30b9`,
`0x1038943e`.

**The occluded selector's latches.** `flags2 & 0x40000` is NPCFlag **`D_POSSESSED`**
(`0x80040000`; `0x40000` alone on flags1 is `BOTCHED_ATTACK`), produced only by the possession
arm; `frenzied & 0x100` is produced by `CNPC_VFrenzyShadow::vfunc420` (`0x5ddf`), the possession
arm (`0x3b1c`) and the frenzy arm (`0x9fbd`). `0x103675a0` and `0x103b2550` are **jump thunks to
the base selector** owned by `CNPC_VBatSwarm` (vtable `0x104aab04`) and `CNPC_VSheriffSwarm`
(`0x104c7284`); `CNPC_VBach` (`0x10364280`) is the only real override among 64 classes.

**Hint type 10000** is never a `m_pHintNode`: no category bit, no `FValidateHintType` case
(`0x10295c20` handles `0x27d8`, 100/101, `0x2774`, `0x283c/d`, `0x28a0`; base returns 0), so both
searches reject it. Its only lookup is by **name**: `0x102d2840` walks the hint list for type
`10000 || 800` with `stricmp(hint->m_strGroup +0x5f0, name)`, `0x102d2900` returns the node id;
callers `InputFollowPatrolPath 0x1029ed90` and `InputWalkToNode 0x1029e840`, which tokenize the
input and build the `CAI_PatrolPath` at `+0x6590` from node ids. `hint_groups` does not filter
patrol points. `CNodeEnt::Spawn` `0x102d78d0` → `0x102d7d30` maps classnames to types
(`_cover_med` 100, `_cover_low` 101, `_cover_corner` 0x27d8, `_crosswalk` 11000, tzimisce claws
14000/0x36b1, `_kick_over` 0x283c, `_kick_at` 0x283d, `_shoot_at` 0x28a0, werewolf/sabbat/bach/
chang families 16000+, `_manbat_fly_to_point` 20000); 10000 is the FGD default on
`info_node_hint`/`info_node_patrol_point`. UNRECOVERED: what authors type 800.

**`FinViewCone3dNew` `0x103264d0`**, called by `FInViewCone 0x10326750` with the target's slot-192
point, its cone scalar (slot 29) and `m_flFieldOfView`: test 1 `dot(target − eye, fwd) > 0`
(strict, unnormalised); test 2 `apex = eye − k·fwd` (`k` = the float of the ConVar at
`0x10937a8c`, UNRECOVERED name, shared with `CWeaponMelee::RequestActivity`), `dot(fwd,
normalize(target − apex)) × coneScalar > m_flFieldOfView` (strict). The scalar multiplies the
cosine, not the threshold; the pulled-back apex widens the cone with proximity (the debug wedge
`0x1029c4a0` draws it the same way). The 2-D variant is taken when the ConVar at `0x10936f74`
reads 2.

**Story 12b recovery (2026-09-12): the kick programs and their tasks, verbatim.**
`0xa9 SCHED_TROIKA_KICK_PROP_AT_ENEMY` (`0x105f21a0`): `SET_TOLERANCE_DISTANCE 0;
GET_PATH_TO_KICK_PROP 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT;
SNAP_TO_KICK_PROP 0; KICK_PROP 0; CLEAR_NPC_FLAG FORCE_RELAXED_ANIMS`; interrupts `NEW_ENEMY
KICK_PROP_INVALID`. `0xa7 HINT_KICK_OVER` (`0x105f2590`): `SET_TOLERANCE_DISTANCE 0;
GET_PATH_TO_HINTNODE 0; SET_NPC_FLAG FORCE_RELAXED_ANIMS; RUN_PATH; WAIT_FOR_MOVEMENT;
SNAP_TO_HINT 0; FACE_HINTNODE 0; CLEAR_NPC_FLAG FORCE_RELAXED_ANIMS; KICK_HINT 0`; `0xa8
HINT_KICK_AT_ENEMY` (`0x105f2390`) the same with `FACE_ENEMY` and `KICK_HINT_AT`; both interrupt
on `NEW_ENEMY HINT_INVALID`. Tasks (ids from the task registrar `0x10316ff0`; arms from the
Troika `StartTask` jump table `0x102a7ab8`/`0x102a77f8`):

- `0x111 GET_PATH_TO_KICK_PROP` (`0x102a6128`): `m_hKickPhysicsProp +0x643c` dead → `TaskFail(0x25)`;
  no enemy → `TaskFail(6)`; goal = prop origin displaced **64 units** (`0x42800000`) along
  normalise(prop − enemy) — the side of the prop facing away from the enemy — as `AI_NavGoal_t{type 4,
  tolerance [0x1049a1ac] = −1, flags 0xa}` into `SetGoal`; the arm returns without completing (the
  base `RunTask` decides, as for the other `GET_PATH_TO_*` arms).
- `0x112 SNAP_TO_KICK_PROP` (`0x102a6289`): prop alive → `TaskComplete`, else `TaskFail(0x25)`.
  Despite the name it moves nothing.
- `0x113 KICK_PROP` (`0x102a62db`): prop dead → `TaskFail(0x25)`; else
  `RestartIdealActivity(0xc84)` (`0x10289ee0`; `0xc84` is the `ACT_KICK` row of the activity
  table), **`0x102b6890(prop)`** applies the kick, then the handle is cleared (`+0x643c = -1`).
  Run arm `0x102ab83c` (sequence finished → complete).
- `0x10f KICK_HINT` (`0x102a5f86`): `m_pHintNode` → `RestartIdealActivity(0xc84)`, else
  `TaskFail(4)`. `0x110 KICK_HINT_AT` (`0x102a5fcc`): the same, then the hint's target name
  (`0x10006a14(hint)`) resolved through `0x100f7770` into `+0x643c` (`"Warning: Kick hint (%s)
  is trying to find a physics object (%s) that is not found."`), then the `0x102b6890` kick on
  it. Both run through `0x102ab83c`.
- **The kick `0x102b6890(prop)`**: direction = (enemy origin + `_DAT_10451ad0` z, or my own
  origin when there is no enemy) − prop centre (slot `0x370`); the horizontal angle of that
  direction versus my facing (`0x1013d580`) is clamped to **±20°** (`_DAT_1044eb0c = 20.0`,
  the same cell as the stranger re-arm) about my yaw; speed = the prop's physics object mass
  term (`IPhysicsObject` slot `0x30` × 100.0, capped `_DAT_10447ee0`); the impulse vector gets
  `_DAT_10457f60` added to z and is applied through `IPhysicsObject` slots `0xa0`/`0x9c`
  (`GetPosition`/`ApplyForceCenter`). No damage is dealt here; the prop's own impact does it.

Unrecovered after this pass: `TASK_GET_PATH_TO_HINTNODE` / `SNAP_TO_HINT`'s arms (shared with
the cover family), the `0x1049a1b0` tolerance cell's value, `_DAT_10451ad0` / `_DAT_10447ee0` /
`_DAT_10457f60`.

**The possession arm's virtuals**: slot 304 = `CBaseCombatCharacter::GiveBaseFightingItems`
(Troika `0x102b5b20`: no melee (slot 307) and no ranged (slot 308) weapon → `GiveItem
("item_w_fists")`, `AddMiscFlag(0x10 Gave_Fighting_Wpns)`); slot 614 = `ResetThinkTimers()`
`0x102c23f0`; slot 595 = `AcquireNearestHatedTarget()` `0x102b4cc0` (box ±1024/±1024/±128, flag
mask `0x40`, targetable, alive, not hidden, `IRelationType == D_HT`, nearest → slot 596
`SetEnemy`). Possession calls 304 then 614 then 595; frenzy calls 595 first.
