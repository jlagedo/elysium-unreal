# Brief H2 — drive the Green Room cover scenario over MCP (spike)

Read `CLAUDE.md`. The game is running as `uv run elysium gr --arena` (the Green Room arena
stage). You drive it over the `elysium` MCP server (`mcp__elysium__elysium_*`), ONE call at a
time. Prefer `elysium_console_exec` with the compact verbs below over `elysium_entity_get` on an
NPC (15k tokens). You write only
`docs/specs/0018-world-ai-infrastructure/story8/arena-run.md` (≤80 lines, a table: step, done,
observed, verdict). Report ≤300 words. No dumps.

Verbs (new this spike; if one is missing or errors, report its exact output and stop that step):
- `elysium.gr_scenario cover` — stages the network, four cover hints and `arena_gunman` (a
  `npc_VHumanCombatant` with a .38, `player_reaction D_HT 5`, all hint groups) through the map
  `Load` path, and places the player at the room's start facing the gunman's pad.
- `elysium.gr_hints` — every hint: index, name, type, class mask, node id, disabled, owner, next
  use, origin.
- `elysium.gr_los on|off` — moves the player behind the cover block (on) or back to the start.
- `elysium.npc_brief arena_gunman` — ≤25 lines: state, schedule, enemy, conditions, hint node /
  owner / cover object, body + on_ground, player line, think stamps, weapon.
- `elysium_status` for `game_time` / `spawn_done`.

Steps:
1. `elysium_status`: the stage is `Playing`, `spawn_done` true. `elysium.gr_scenario cover`;
   record its printout. `elysium.gr_hints`: expect 4 hints, class mask 1, node ids 0..3 matching
   their row, owner none, next use 0, `hint_rating` 2.5 if it prints it.
2. `elysium.npc_brief arena_gunman` every ~5 s of game time for ~30 s: it must have a body,
   `on_ground` true, a real schedule (not FALL_TO_GROUND for more than one think), `SEE_PLAYER` /
   `SEE_HATE` conditions once the player is in view, an enemy = the player, and a combat schedule.
   Record the sequence of schedules. If it never leaves FALL_TO_GROUND or never gets an enemy, that
   IS the finding: record every field of the brief at that moment and stop after ~60 s.
3. When in combat with the player as enemy: `elysium.gr_los on` (occlude), then poll
   `npc_brief` + `gr_hints` every ~5 s for ~30 s. Expect `CanSeekCover` → the tactical search →
   a hint with `owner = arena_gunman`, the NPC's `hint:` line naming it, and a TAKE_COVER-family
   schedule. Record which hint won (the first admitted from the head is the LAST-spawned node),
   and the cursor's effect if a second search runs.
4. `elysium.gr_los off` and wait ~20 s: record what happens to the claim (a release on schedule
   change writes `next use`).
5. Kill: `elysium_entity_fire target=arena_gunman input=TakeDamage param=1000`; `gr_hints`: the
   owned hint's owner cleared and `next use` = death time + 5.0 (Event_Killed's delay).
6. `elysium.gr_scenario cover` again (idempotent restage) and repeat step 2 once to show the
   restage works; then `elysium_log_tail lines=2000` filtered mentally for Ensure / Assert /
   `Error:` — report counts.
