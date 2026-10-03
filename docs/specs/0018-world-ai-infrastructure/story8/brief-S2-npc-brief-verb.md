# Brief S2 — a compact NPC readout for unattended runs (spike)

Read `CLAUDE.md`. Problem: `elysium.ent_dump <npc>` and MCP `elysium_entity_get` on an NPC return
~450 fields (~15k tokens), so an agent driving the game over MCP cannot afford to poll them. Add
ONE console verb, `elysium.npc_brief <targetname|index> [<more>...]`, that prints ≤25 lines per
NPC. You edit ONLY `Source/ElysiumUE/Private/Debug/ElysiumEntityDebugSubsystem.cpp` (where
`elysium.ent_dump` is registered — put the new verb beside it, same registration pattern) and, if
a helper must be declared, its header. Do NOT touch `Debug/ElysiumGreenRoom*`, `ElysiumArena*` or
`ElysiumMcpTools.cpp` (another agent owns the first two; the MCP tool is out of scope). Do not
build. Report ≤200 words.

Lines to print (read the values through the same accessors `elysium_entity_get`'s `live_state`
uses — grep `"Mind"`, `"Schedule"`, `"Enemy"`, `"Conditions"`, `"Body owner"`, `"Closest player"`
in `ElysiumMcpTools.cpp` / the NPC's `GetDebugState` and reuse those helpers, do not re-derive):
1. `#<index> <targetname>(<classname>) live/dead hidden=<0|1> origin=<x y z>`
2. `state: <m_NPCState> ideal=<...> mind=<the "Mind" line>`
3. `schedule: <the "Schedule" line, id + task index> fail=<m_failSchedule>`
4. `enemy: <the "Enemy" line> last=<...> sightings=<n> dist=<m_flEnemyDist>`
5. `conditions: <the "Conditions" line>`
6. `hint: node=<BaseScheduleHost.HintNode> (owner? via HintWords: name/index, type, owner, next use)  cover_obj=<m_hHintCoverObject> at_cover=<AT_COVER_HINT flag> shoot_at=<m_hShootTargetOverride>`
7. `body: <the "Body" / "Body owner" lines> on_ground=<motor on_ground> flags=<flags>`
8. `player: <the "Closest player" line> relationship=<the "Relationship to player" line>`
9. `think: next_ai=<m_flNextAIThink> next_normal=<m_flNextNormalThink> last_ai=<m_flLastAIThink>`
10. `weapon: <the "Weapon" line>`
A non-NPC entity prints line 1 and `not an NPC`. Unknown name: one error line.
