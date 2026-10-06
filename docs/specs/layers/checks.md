# Consistency checks

| check | failures | of | |
|---|---|---|---|
| every open core row is in exactly one story, or recorded as not work (done, engine, unreachable) | 0 | 1236 | pass |
| rows the planners could not place (listed in stories.json with the reason) | 0 | 1236 | pass |
| every open upward hook's target is owned by a story of the layer above | 0 | 694 | pass |
| no story depends on a later layer | 0 | 447 | pass |
| no dependency cycle between stories | 0 | 447 | pass |
| every story has at least one test record | 0 | 447 | pass |
| every story row is in exactly one of its slices | 0 | 1170 | pass |
| every brief is at most 4 KB | 0 | 588 | pass |

## Harness additions the records ask for

- NEW: catalog_row (13)
- NEW: entity_call action (10)
- NEW: stat_value (7)
- NEW: probe: door_pose (6)
- NEW: set_velocity action (6)
- NEW: configure_movetype action (6)
- NEW: discipline_activate (6)
- NEW: probe: toggle_state (5)
- NEW: collision_bounds probe (5)
- NEW: trace_fields probe (5)
- NEW: touch tap (5)
- NEW: active_effect (5)
- NEW: set_parent action (4)
- NEW: physics_push action (4)
- NEW: physics_order tap (4)
- NEW: physics_collision action (4)
- NEW: touch (4)
- NEW: player_feed_start (4)
- NEW: entity_flags probe (3)
- NEW: stored timer RefireTime probe (3)
- NEW: probe: move_done_time (3)
- NEW: blocked (3)
- NEW: physics_object probe (3)
- NEW: velocity probe (3)
- NEW: physics_shadow tap (3)
- NEW: base_velocity probe (3)
- NEW: push_transaction tap (3)
- NEW: origin probe (3)
- NEW: local_time probe (3)
- NEW: restored_datamap_field probe (3)
- NEW: sound_query action (3)
- NEW: sound_lookup trace (3)
- NEW: inventory_items (3)
- NEW: weapon sound trace (3)
- NEW: movement_fixture (3)
- NEW: player_move (3)
- NEW: placeevict (3)
- NEW: active_discipline fixture (3)
- NEW: sound stimulus action (3)
- NEW: dialogue fixture action (3)
