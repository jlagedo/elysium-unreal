# Brief H — the live check on the two witnesses (0018/8, wave 3)

Read `CLAUDE.md`. You drive the RUNNING game over the `elysium` MCP server (tools
`mcp__elysium__elysium_*`: status, entity_get, entity_fire, entity_list, player_teleport,
player_noclip, script_eval, log_tail, map_load, console_exec). Calls run on the game thread:
issue them ONE AT A TIME, never in parallel. You edit NO files except
`docs/specs/0018-world-ai-infrastructure/story8/live-check.md`, which you write at the end.
Report ≤300 words: the observed values, pass / fail per step, anything unexpected. No dumps.
`elysium_entity_get` on an NPC returns ~15k tokens — call it only when you must, and read only
`live_state` and the named fields; on a hint it is small. Do not call `elysium.npc.list` or
`elysium.ent_dump` (both dump thousands of lines).

State now: the game is on `sp_tutorial_1`, a new game; `tutwareelevdrd` has been fired `Open`
(both thugs are unhidden); the player was teleported to Unreal (-2540, -1143, 20) cm, yaw 180
(that is Source (-1000, 450) units; the world negates Y and multiplies by 2.54). Hint row 465
(`elysium_entity_get index=465`) shows `hint_rating` 2.5 (the rulebook replacement is live),
owner none, next use 0.

The recipe (from `docs/specs/0018-world-ai-infrastructure/story8/census.md` § 3, read it):
1. Confirm `thug_3` (index 472) sees the player and enters combat: its `live_state` "Enemy",
   "Schedule", "Mind". If it is not in combat, provoke it: `elysium_player_teleport` closer with a
   clear line (thug origin Source (-1725, 471, 0) → Unreal (-4381.5, -1196.3, 0)), e.g. 300 units
   east of it at Unreal (-3620, -1196, 20) yaw 180; then wait ~10 s of game time (poll
   `elysium_status` game_time) and re-read. If the player must attack, use
   `elysium_console_exec` with the game's attack / damage verb if one exists (`elysium.ent_fire
   thug_3 TakeDamage 1` is a registered input on the NPC — use `elysium_entity_fire target=thug_3
   input=TakeDamage param=1`).
2. Once in combat with the player as enemy: the Troika selector `0x102b7690(1,0,0,0)` runs the
   tactical search `0x102b7110` → `FindHintByClassMask(8, 1, 1024)` when `CanSeekCover` is true
   (`COND_ENEMY_OCCLUDED`, or the cover timer due). Break line of sight if needed (teleport the
   player behind a pillar / crate in the warehouse, around Unreal (-2540, -1143) there are
   crates; try a few spots) and wait. Read `thug_3`'s `m_pHintNode` / "Schedule" (expect
   `SCHED_TROIKA_TAKE_COVER_HINT` 0x9b or a program in that family) and the chosen hint's
   `m_hHintOwner` = #472 via `elysium_entity_get index=<that hint>` (the 12 cover rows are 454–465).
   Record WHICH row won and the cursor's effect on a second search if one happens.
3. Release: fire `Kill` at `thug_3` (or `TakeDamage 1000`) and read the hint again: owner cleared,
   `m_flNextUseTime` = game_time-at-death + 5.0 (`Event_Killed` delay 5.0) — or, if the program
   ended by itself, + 60.0 (`ClearHintNode(60)`). Say which.
4. Cower must MISS: no type-10100 row exists; if any NPC ran the cower arm (a flee), its
   `m_pHintNode` stays -1. Only report if you observe one.
5. `DisableHint` visibility, tutorial: `elysium_entity_fire target=info_node_cover_low
   input=DisableHint` fans out over the 4 low rows (465..462); confirm row 465 `m_iDisabled` = 1
   and, if a second combat search happens (e.g. unhide and provoke `thug_2`... no, thug_2 is melee,
   mask 8 — it finds nothing; instead re-provoke `thug_3` before killing it: do step 5 BEFORE
   step 3), the winner is a corner row (461..454), not 465..462.
6. Then `elysium_map_load map=sm_hub_1`, wait until `spawn_done`, idle ~3 minutes of game time
   (poll `elysium_status` every ~30 s), and read `elysium_log_tail category=LogOutputDevice` /
   `category=Ensure` / grep for "Ensure" and "Assert" over `elysium_log_tail lines=2000`
   (filter mentally; do not paste): report the count of ensure / assert lines. Also on the hub:
   `elysium_entity_fire target=cover_front_10 input=ScriptUnhide` then `DisableHint`, read
   `elysium_entity_get name=cover_front_10` → `m_iDisabled` 1; `EnableHint` → 0.
7. Python visibility: `elysium_script_eval source="FindEntityByName('cover_front_10').DisableHint()"`
   — if `FindEntityByName` is not in the host, try `Finds('cover_front_10')[0].DisableHint()` as
   `scripts/temple/temple.py:68` does; read the hint's `m_iDisabled` after.

Write `live-check.md` with a table: step, what was done, observed, verdict. Keep it ≤80 lines.
