# 0018/8 R2 -- hint census and live-check recipe

Source: `.entities.glb` units under `$ELYSIUM_WORK_ROOT/exports_v2/maps/`, read through
`UE_map_sidecars.read_units` + `map_ai_infra.family_of` (the pipeline's own hint rule, so the row set
equals what `stage_rows` bakes). Reach: `docs/vtmb/npc-kernel/reach/{sp_tutorial_1,sm_hub_1}.tsv`;
schedule texts: `Content/ElysiumCorpus/ai/schedules/`. Numbers are Source units, entity row = BSP index.

## 1. Census

| | sp_tutorial_1 | sm_hub_1 |
|---|---|---|
| hint rows (family "hint") | 49 | 274 |
| classes | patrol_point 37, cover_corner 8, cover_low 4 | cover_corner 206, patrol_point 34, cover_med 27, crosswalk 6, cover_low 1 |
| `StartHintDisabled` nonzero | 0 (all authored `0`) | 0 (56 authored `0`) |
| carry `Group` (patrol name) | 37 | 34 |
| carry `target_name` | 10 | 0 |
| carry `targetname` | 3 (`monk1_1..3`, patrol) | 30 (6 crosswalk, 24 `cover_front_N`/`cover_rear_N`) |
| `StartHidden 1` | 4 | 24 (all the named cover_med) |
| `group_id` | "1" x49 | "1" x234, absent x40 (patrol + crosswalk => 0xFFFFFFFF) |
| `hint_rating` / dist min / max / angle | 3 / 256 / 32000 / 60 on every cover row | same keys on the 234 cover rows |
| parented rows | 0 | 0 |
| `info_hint` (10100, cower) | 0 | 0 |
| shoot-at 10400 / kick 10300, 10301 | 0 | 0 |

| HintType | class mask (`hint+0x474`) | tutorial | hub |
|---|---|---|---|
| 100 cover_med | 1 | 0 | 27 |
| 101 cover_low | 1 | 4 | 1 |
| 10200 (0x27d8) cover_corner | 1 | 8 | 206 |
| 10000 patrol_point | 0 | 37 | 34 |
| 11000 crosswalk | 0 | 0 | 6 |
| mask 1 total (tactical-searchable) | | 12 | 234 |

Mask 4 / 8 / 0x10 (kick_over, kick_at, shoot_at): none on either map. Mask 1 is the only mask with
candidates. No `EnableHint`/`DisableHint` output on either map (216 across the 108).

**First five in retail LIST order** (head = last BSP row; `0x102d2e30` inserts at the head):

| pos | tutorial (row, class, Group, origin, nodeid) | hub (row, class, origin, nodeid) |
|---|---|---|
| 0 | 1764 patrol `zz1` (-7236,3233,6719) 605 | 2399 corner (-1684,-968,-5876) 67 |
| 1 | 1763 patrol `zz2` (-7128,3249,6719) 604 | 2398 corner (-1688,-840,-5876) 66 |
| 2 | 1760 patrol `zz3` (-7118,3338,6719) 601 | 2397 corner (-1540,-833,-5876) 65 |
| 3 | 1759 patrol `zz4` (-7108,3476,6719) 600 | 2396 corner (-1532,-972,-5876) 64 |
| 4 | 1717 patrol `room4` (-7373,3576,7055) 598 | 2395 corner (-416,-964,-5876) 63 |

Every tutorial hint has an empty classname-level name except the three `monk1_*`; the 12 cover rows
(list positions 34-45) are rows 465 (pos 34), 464, 463, 462 (all cover_low), then 461..454 (corner).
Cover rows sit at x -1144..-1804, y 168..524, z 4..9: the warehouse.

## 2. Reached programs that search

Reached texts contain no `TASK_FIND_HINTNODE` (0x40), `TASK_FIND_LOCK_HINTNODE` (0x41) or
`TASK_LOCK_HINTNODE`: the only `.sch` files naming them are `cnpc_crow/sched_crow_*` (Crow is on neither
map's chain). `TASK_CLEAR_HINTNODE` and `TASK_PLAY_HINT_ACTIVITY` are likewise absent. The 0x10365780
site is `CNPC_VBach` (absent). So both maps' searches come from native call sites only:

| Site | Retail call | Reached by (texts) | Classes (both maps' chain) | Selecting condition | Finds on the map |
|---|---|---|---|---|---|
| Troika `StartTask` cower arm (`0x102a2882`) | `0x102d1af0(type 0x2774=10100, flags 2, (r+8192)*0.5)` | `TASK_GET_PATH_TO_COWER_NODE` in `SCHED_TROIKA_FLEE_AND_COWER`, `SCHED_TROIKA_D_AFRAID`, `SCHED_TROIKA_FLEE` (`_NO_ENEMY` uses the `_SAVE_POS` twin) | VHumanCombatant, VPedestrian, VVampire, VRat, makers' children | a flee/cower selection (criminal/supernatural witness flee, fear) | **nothing**: zero type-10100 rows on either map. Must MISS and fall to the flee path |
| tactical `0x102b7110` -> `0x102d2980(flags 8, mask, slot 550)` | mask = `p1 \| p2<<1 \| (p3&&allowKick)<<2 \| (p4&&allowKick)<<3` | `0x102b7690`, from `SelectSchedule 0x102ae920` `(1,0,0,0)`, dodge selector `0x102b7cf0` `(1,1,1,1)`, melee selectors `0x10385e40`/`0x10396050` `(0,1,0,1)`; programs `SCHED_TROIKA_TAKE_COVER_HINT` (0x9b) etc. | VHumanCombatant, VPedestrian, VVampire (Troika human line); VRat is `cnpc_vscurrying`, not offered the human selectors | state 2 (combat) + enemy + `CanSeekCover` (slot 592: `COND_ENEMY_OCCLUDED 0x48`, or `m_flCanSeekCoverTimer <= curtime`, or timer-1 < curtime and no `COND_CAN_RANGE_ATTACK1`) + no `m_pHintNode` + not DODGING + no kick prop | mask 1 only: 12 rows tutorial, 234 hub. Melee arms (mask 8) find nothing on either map |
| shoot-at slot 609 `0x102b6b50` -> `0x102d2980(flags 8, mask 0x10, weapon +0x8c0 or 1024)` | after `RandomFloat(2.0,2.5)` cooldown | arm D of `0x102b7690` when `m_pHintNode` is cover-typed and `AT_COVER_HINT 0x2000`: -> `SCHED_TROIKA_TAKE_COVER_HINT_SHOOT_AT_HINT` / `..._RANGE_ATTACK1_SHOOT_AT_HINT` | same Troika human line | holding a cover hint, at cover | **nothing**: no 10400 rows. A miss still burns the 2.0-2.5 s draw |
| kick chooser `SCHED_TROIKA_HINT_KICK_OVER/_AT_ENEMY` | arm D on type 10300/10301 | as above | -- | -- | unreachable: no such hints |

Note the signature is `(npc, flags, mask, radius)`: shoot-at is flags 8 / mask 0x10, tactical is flags 8 /
mask = argument. The brief's "mask 8" for both reads the flags word. With flags 8 and bit 0 clear, the
walk stops at the FIRST admitted node from `cursor->next` (score unused, outScore = FLT_MAX).

## 3. Live-check recipe (sp_tutorial_1, warehouse)

Reachable, but gated. `thug_3` (row 472, `npc_VHumanCombatant`, `additionalequipment item_w_thirtyeight`,
`player_reaction D_HT 5`, `allow_kick_hint_use 1`, `stay_entrenched 0`, `hint_groups` all 32, origin
(-1725,471,0), `StartHidden 1`) is the ranged thug. `thug_2` (row 471, bat, origin (-1430,603,0)) is
melee: its arm is `(0,1,0,1)`, mask 8, finds nothing. Use `thug_3`.

1. Unhide: `elysium_entity_fire tutwareelevdrd Open` (row 194; `OnOpen` -> `thug_2/thug_3 ScriptUnhide`).
   Cheaper than walking the tutorial; teleport the player into the warehouse (x -1300..-900, y 300..500,
   z ~0) after, with a clear line to `thug_3`.
2. Trigger: the player is the enemy (`D_HT 5`); shoot `thug_3` or stand in its view. Combat state with an
   enemy makes `SelectSchedule` offer `0x102b7690(1,0,0,0)`; `CanSeekCover` is true on the first tick the
   timer is due or on `COND_ENEMY_OCCLUDED` (break LOS behind a pillar to force it).
3. Expected pick: search `0x102d2980(flags 8, mask 1, radius 1024 [slot 550, programs.md:1091])` with
   `m_hHintCoverObject` = the player. All 12 rows admit on mask 1 and group (1 & 0xFFFFFFFF). Walk order
   from the cursor (NULL on a fresh session: head to tail) is rows **465, 464, 463, 462** (type 101,
   `IsHintCoverValidLoose` `0x102974f0`, const 1.1) then **461 ... 454** (type 10200,
   `IsHintCoverValid` `0x10297430`, const 0.731). The first row to pass `0x10296c40` wins:
   height diff <= 64, hint-to-player 2-D distance in [256, min(weapon range, 32000)], and
   `dot(norm(hint-player), norm(npc-player)) >= 0.2`, plus a hint LOS trace (`m_bForceCoverLOSCheck`
   is raised for the first search). Player east of the NPC at about (-1000,450) makes every row
   west of x -1256 satisfy the distance and dot terms, so the expected first hit is **row 465
   (-1588,264,4, nodeid 38)**, else the earliest of 464/463/462/461 that passes; verify by listing the
   result, not by prediction. The cursor moves on any earlier search (a miss zeroes it), so run this
   before other NPC searches, or expect `cursor->next` rotation.
4. Observe: `elysium_entity_get name=thug_3` for `m_pHintNode` (+0x5ddc) and `AT_COVER_HINT`;
   `elysium_entity_get index=465` (hints are unnamed: address by row index) for `m_hHintOwner` (= thug_3
   handle) and `m_flNextUseTime` (0 before). Success shows owner set, `m_iPeekOutCount` 0, then
   `SCHED_TROIKA_TAKE_COVER_HINT` in flight. A refused claim would clear `m_pHintNode` at once.
5. Release: kill `thug_3` (`Event_Killed`: release delay 5.0) or let cover end (`ClearHintNode(60)`
   on give-up). Expect `m_hHintOwner` invalid, `m_flNextUseTime = curtime + 5` (or +60). A second NPC's
   search inside that window must skip the row (unusable: `curtime < m_flNextUseTime`), at equal time
   it is usable (strict).
6. Cower must MISS: `elysium_entity_fire` a flee condition is not needed; if a hostile-witness flee
   runs, `TASK_GET_PATH_TO_COWER_NODE` finds no 10100 row and `m_pHintNode` stays -1. That is the
   census-correct outcome, not a defect.

`DisableHint` visibility: **hub**, row 190 `cover_front_10` (type 100, (-1173,583,-111)) is
addressable by name: `elysium_entity_fire target=cover_front_10 input=DisableHint`, then
`elysium_entity_get name=cover_front_10` (`m_iDisabled` = 1) and repeat step 3's combat near it: the row
must vanish from admission (unusable test 1) with no other change; `EnableHint` restores. Python:
`elysium_script_eval "Finds('cover_front_10')[0].DisableHint()"` (same form as
`scripts/temple/temple.py:68`). **Tutorial** offers only unnamed cover rows: fire at the classname
(`info_node_cover_low`, fans out over the 4 low rows) or eval on the row via `elysium_entity_get index=465`
first; disabling all four then makes the search start at row 461.

## 4. All 108 maps

| Measure | Value |
|---|---|
| hint rows total | 3156 |
| per map min / max | 0 (`ch_cloud_1`; 54 maps have none) / 274 (`sm_hub_1`) |
| types seen (count) | 100:431, 101:269, 10000:620, 10100:17, 10200:1438, 10300:1, 10400:22, 11000:22, 14xxx none, 15000-15018:~130, 16000-16005:52, 17000-17005:39, 18000-18003:88, 19000:12, 20000:13 |
| disabled at start (`StartHintDisabled` != 0) | 96 rows in 7 maps: `sp_observatory_2` 44, `sm_beachhouse_1` 25, `ch_fulab_1` 11, `ch_temple_2` 5, `la_bradbury_2` 5, `sm_warehouse_1` 5, `la_empire_2` 1 |
| `Group` / `target_name` / `StartHidden` | 790 / 391 / 131 |
| `group_id` | "1" 2194, absent 600, "2" 115, "3" 70, "4" 57, "8" 41 |
| Enable/DisableHint outputs authored | 216; one Python site (`temple.py:68`, `Bottleneck_Cover`) |

Neither witness map exercises disabled-at-start; if the landing paragraph needs that arm live,
`sm_beachhouse_1` or `sm_warehouse_1` is the witness.

## Unrecovered / caveats

- Slot 550 radius (1024) is programs.md's reading; shape.md calls it "ideal range".
- `0x10296c40`'s facing-projection bounds (0.731 / 1.1) and weapon `+0x8c0` are not recovered, so the
  first pick among rows 465..454 is verified live, not derived here.
- Retail LIST position of nodes is by BSP row order; `StartHidden` on hint rows is a `CBaseEntity`
  key and its retail effect on the hint search is not walked here.
