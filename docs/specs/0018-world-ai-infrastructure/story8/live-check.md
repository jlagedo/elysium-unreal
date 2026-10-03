# 0018/8 live check — the two witnesses (brief H)

Run 2026-09-30 against the running game over the `elysium` MCP server. Start state as the brief
gives it: `sp_tutorial_1`, new game, `tutwareelevdrd` fired `Open`, player at Unreal
(-2540, -1143, 20). Game times are `elysium_status.game_time`.

| Step | Done | Observed | Verdict |
|---|---|---|---|
| 1 combat entry | read `thug_3` (#472) at t=118; teleported player to (-3620, -1196, 20) yaw 180; waited to t=160; fired `TakeDamage 1`; re-read at t=181 | t=118: `Schedule FALL_TO_GROUND (0x3e) task 0`, `Conditions FLOATING_OFF_GROUND`, `Enemy (none)`, locomotion `on_ground false`, `jump_phase descend`, `flags` 73728 (no FL_ONGROUND). t=160: `SEE_HATE\|SEE_PLAYER\|FLOATING_OFF_GROUND`, player 765 cm SEEN in cone, `m_hLastSeenHateEnt #1868`, still `Enemy (none)`, still `FALL_TO_GROUND task 0`, `Mind current=Idle`. After `TakeDamage 1`: `Last damage (none)`, no damage condition, `health` 10 -> 95 (`vhealth` 0 -> 1), schedule unchanged | **FAIL (blocked)** — see note 1 |
| 2 cover search | not reachable: no combat, no `0x102b7690` selector run | every cover row 454–465 kept `m_hHintOwner #<null>`, `m_flNextUseTime 0`; `m_hHintCoverObject #<null>` on the NPC. `m_pHintNode` is not in the `entity_get` field readout; it could only be judged from the hint side | **NOT RUN** |
| 3 release | `Kill` on `thug_3` at t=280.4 | #472 `dead`, `inert`; row 461 (and 465) owner `#<null>`, next use `0.0`. `Kill` removes the entity, so the `Event_Killed` 5.0 release never runs. With no claim held, there was nothing to release | **NOT RUN** (nothing claimed) |
| 4 cower miss | watched | no flee ran on the NPCs I read | not observed |
| 5 DisableHint (tutorial) | `entity_fire target=info_node_cover_low` -> error `no live entity with targetname or classname`: the live classname is `ai_hint`, and `info_node_cover_low` is only the authored class (`live_state."Authored class"`). Disabled the 4 low rows by origin through Python instead: `[e.DisableHint() for e in FindEntitiesByClass('ai_hint') if origin in {462..465}]` -> 4 | row 465: `Disabled yes`, datamap field `StartHintDisabled` 1 (m_iDisabled), `hidden true`. Row 461 (corner): `Disabled no`. The second search that would choose a corner row did not happen (step 1) | **PASS** (the write); winner **NOT RUN** |
| 6a hub load | `map_load sm_hub_1` at t=282; MCP dropped once during travel, then came back | `spawn_done true`, t=316. A dialogue was open (3 choices) for the whole idle; queue 10–17 pending | PASS |
| 6b ensure/assert | `log_tail category=Ensure` / `LogOutputDevice` (2000 lines); counted `ensure\|assert` in `Saved/Logs/ElysiumUE.log` for the whole session (47.8k lines, tutorial + hub) | Ensure 0, LogOutputDevice 0; whole-session log: **0 ensure, 0 assert**. No `Error:`/`Fatal` lines after the hub travel. Idle ran t=316 -> 492 (~3 min, polled `elysium_status` about every 30 s); recount at the end is still 0/0 | **PASS** |
| 6c hub DisableHint | `cover_front_10` (#190): `ScriptUnhide` -> `DisableHint` -> read -> `EnableHint` -> read | after Disable: `StartHintDisabled` 1, `Disabled yes`, `hidden true`. `EnableHint` right after: **still `Disabled yes`** — the hint was hidden, and the hidden-entity gate (`FElysiumHint::SwallowsInput`, retail `Q_strnicmp(input,"ScriptUnhide")`) swallows it. `ScriptUnhide` then `EnableHint` -> `StartHintDisabled` 0, `Disabled no`, `hidden false` | **PASS** (with the note 2 order) |
| 7 Python | `script_eval FindEntityByName('cover_front_10').DisableHint()` (the host has `FindEntityByName`; `Finds` not needed) | returned Void with no error; read after: `StartHintDisabled` 1, `Disabled yes`, `hidden true` | **PASS** |

## Notes

1. **HANDED ON — body/lifecycle defect, not a hint defect: `thug_3` stuck in
   `SCHED_FALL_TO_GROUND`.** Evidence: #472 `thug_3` is `StartHidden 1`, unhidden by
   `tutwareelevdrd` `OnOpen` -> `ScriptUnhide`. Its readout from t=118 to t=181:
   `Schedule FALL_TO_GROUND (0x3e) task 0`, `Conditions FLOATING_OFF_GROUND`, `flags` 73728,
   locomotion `on_ground false` / `jump_phase descend`, and `Body owner: None gen=0 parked=None`.
   The `Body` line itself read `skeletal (standing)`, not `(none)`; I record the exact text. A cop
   with a working body (#1957) reads `Body owner: Schedule gen=1`, `on_ground true`, with the SAME
   `flags` 73728. So flags bit 1 is up on neither NPC, and `on_ground` / `Body owner` are what tell
   them apart. The port's `CheckOnGround` (`ElysiumNpcBaseMotor.cpp:601`, retail `0x1026e5e0`)
   raises 0x73 from the motor's floor sample. It clears 0x73 only on flags bit 1 or a non-zero nav
   type, so an NPC whose character never lands stays in 0x3e forever. The coordinator's reading is
   that an NPC unhidden from StartHidden never got its Unreal character. That sits outside this
   story; steps 2–3 do not depend on it (see the hub rerun below).
2. **DisableHint also hides.** `InputDisableHint` calls `ScriptHide` (retail `0x102d0a20`), so the
   hint goes `hidden`/`inert`. After that, every hint input except a prefix of `ScriptUnhide` is
   swallowed, which means `EnableHint` alone cannot undo `DisableHint`. The port does this on
   purpose and cites retail. The brief's order (Disable -> Enable) therefore reads 1 -> 1. Retail
   scripts that re-enable a hint must `ScriptUnhide` first.
3. `TakeDamage 1` raised `health` from 10 to 95 and `vhealth` from 0 to 1. It wrote no
   `Last damage`, no `m_flLastDamageTime`, and no damage condition. I have not recovered this
   against retail; it is a question for the damage path, not something to patch here.
4. Two tool-surface gaps: `entity_fire` cannot address an unnamed hint by index or by authored
   class, and the NPC readout has no `m_pHintNode` field.
5. A few MCP calls went out in parallel by mistake: teleport+status, two `entity_list`s,
   `io_history`+status, and `ScriptUnhide`+`EnableHint` on `cover_front_10`. Because of that pair
   I could not say in this run whether `ScriptUnhide` alone clears the disabled word; the hub
   rerun below settles it.

## Hub rerun of steps 2–3 (coordinator follow-up, sm_hub_1, t=575–775)

| Step | Done | Observed | Verdict |
|---|---|---|---|
| pick | `entity_list npc_VCop live_only` -> 4 cops; picked `patrol_cop_north` #1957, the one nearest the `cover_*_8/9/10` rows | `Body skeletal`, `Body owner Schedule gen=1`, `on_ground true`, `SCHED_TROIKA_FOLLOW_PATROL_PATH_WALK (0x67)`. **Weapon `item_w_baton` — capability melee (0x18000)**; alternate `item_w_glock_17c` "(unread)". All 24 `cover_front/rear_N` rows are `StartHidden 1` (hidden) | picked |
| provoke | `SetRelationship "player D_HT 5"` (accepted; the readout turned `D_HT priority 5`), then several teleports 500–800 cm in front of the patrolling cop | t=592–700: never `SEEN`. **`sm_hub_1`'s `logic_auto` OnMapLoad opens `havenbum.StartPlayerDialog` (`dlg/Santa Monica/Havenbum.dlg`) at t=293, and that dialogue stayed open**. The cop walked past the player several times, readout `Closest player ... unseen`. Closed it with `havenbum EndDialog` at t≈700 | see note A |
| combat | after the dialogue closed: teleport to (-6139,-3450) | the cop saw the player: `m_bWasEverInCombat 1`, `m_hLastSeenHateEnt #2597`, a burst of `TaskFail 0xc: Don't have a route` and `***Combat state with no enemy!`, then `SCHED_TROIKA_ALERT_WAIT (0x4b)`, Mind Alert. Its relationship dropped back to `D_NU 0`. Cause: that teleport point was off the map, and the player fell through the world (z -113776). The chase had no route, and the enemy was lost | invalid run (my teleport) |
| retry | `SetRelationship` again; two more teleports along its route | the cop returned to 0x67 patrol, unseen at 2.6k cm. No second sighting before I stopped | not reached |
| cover claim | — | `m_hHintCoverObject #<null>` throughout. No `cover_*` owner. The cop is melee, so its Troika arm is the melee `(0,1,0,1)` form (census § 3, like `thug_2`): mask 8 and no mask-1 cover search. Even a clean combat entry with this cop would not reach `FindHintByClassMask(8,1,1024)` | **NOT REACHABLE with a baton cop** |
| release | `TakeDamage 1000` at t=772.4 | log `#1957 patrol_cop_north(npc_VCop) died`; list `dead true`, `inert true`. No claim was held, so there was nothing to release | kill path OK; release not observed |
| ScriptUnhide alone | `cover_front_10` was hidden with disabled word 1 (from step 7's Python `DisableHint`); fired `ScriptUnhide` only, then read | `StartHintDisabled` 1 -> **0**, `Disabled no`, `hidden false`. This matches retail slot 78 `0x102d0890`, which clears `m_iDisabled` on unhide. (`cover_front_11` was hidden with the disabled word already 0: StartHidden alone does not set it) | **PASS** |
| ensure/assert | whole-session `Saved/Logs/ElysiumUE.log`, 136.6k lines | **0 ensure, 0 assert**. The 31 `Error:` lines are all editor-plugin `LogPython` at startup (lines < 1600); 0 after that | PASS |

A. **Open dialogue blinds perception.** From hub load until `EndDialog`, no NPC ever registered
   the player as seen (`Closest player unseen`), even at 200 cm in the cop's path. After
   `EndDialog`, the first placement got a sighting at once. Retail may be doing the same (the
   player is in a dialogue); I have not recovered the gate's address. A live check on the hub
   must close `havenbum` first.

B. **What is still needed for steps 2–3:** a RANGED Troika NPC with a body, in combat, with its
   enemy occluded. Candidates: a `thug_3`-type NPC that does not need unhiding, or a hub NPC whose
   authored `additionalequipment` is a firearm. #1957 does not qualify: its authored weapon is a baton.
   I did not read the other three cops (#1928, and #2557/#2558 `copcar`). Which census row reaches such an NPC is for the census owner to name.

## Third attempt at steps 2–3 (sp_tutorial_1, ranged never-hidden humans, t=986–1180)

| Step | Done | Observed | Verdict |
|---|---|---|---|
| reload | `map_load sp_tutorial_1` | `spawn_done` at t=986 (the clock does not reset). **The map came back with earlier state, epoch 3**: `thug_3` is dead (my earlier `Kill`), and the low cover rows 462–465 are still `Disabled yes` / hidden from the step-5 Python `DisableHint`. So a search here starts at the corner rows 461..454 | noted |
| pick | read `Hunter1` #1619 | `on_ground true`, `SCHED_TROIKA_IDLE_DISPOSITION (0x6b)`, `Body owner None gen=1`, mac_10 ranged. **But `hint_groups "6"` -> `m_iHintGroups` 32**, and every warehouse cover row has `group_id 1` (mask 0x1): 32 & 1 = 0, so Hunter1 admits none of the 12 rows. The export lists `sentry3`, `mercenary_upstairs` and `condotierre_upstairs` with all 32 groups; I switched to `sentry3` #1659 (`m_iHintGroups -1`, mac_10, `on_ground true`, but `Schedule (none)`, `m_IdealSchedule 0`) | Hunter1 unusable for the search |
| move | `TeleportToEntity thug_3` | log: `TeleportToEntity destination 'thug_3' resolved to no live entity` (thug_3 is dead). Used Python `SetOrigin((-4381.5,-1196.3,10))` instead (Unreal cm); origin confirmed | moved |
| provoke | `SetRelationship "player D_HT 5"` (readout shows `D_HT priority 5`); player at (-3620,-1196,20), then (-4381,-600,20) in its facing cone | t=1132: `Closest player 602cm, SEEN, in cone`, yet **`Conditions (none)`**: no SEE_PLAYER, no SEE_HATE. Still `Schedule (none)`, `Enemy (none)` | no combat |
| provoke 2 | same with `Hunter1` (SetOrigin (-4300,-1300,10), SetRelationship) to see whether combat starts at all | after its 16 s dormant think caught up (t=1167): `709cm, SEEN, in cone`, `D_HT priority 5`, **`Conditions (none)`**, `m_hLastSeenHateEnt #<null>`, still 0x6b task 1 | no combat |
| stop (step 8) | ~85 s of game time with the player seen and hated | neither NPC entered combat; no cover object; rows 454–465 kept owner `#<null>`, next use `0.0` | **NOT REACHED** |
| ensure/assert | whole session | 0 / 0; no `Error:` after editor startup | PASS |

C. **Why neither NPC reached combat (a lead, not a fix).** `SEE_PLAYER`/`SEE_HATE` come from
   `ElysiumNpcConditions.cpp:315-404`, which iterates `Npc.Senses.Sighted()`. `SEE_PLAYER` is set
   for any sighted player, whatever the relationship, so an empty `Conditions` means `Sighted()`
   did not contain the player. The readout's "SEEN" comes from a different pass (the player-LOS
   stamp). For comparison, `thug_3` (authored `D_HT 5`, `npc_perception 3`) did get
   SEE_PLAYER|SEE_HATE, and so did the hub cop, briefly. Differences I saw in the two tutorial
   NPCs, none of them confirmed as the cause:
   - authored `player_reaction D_LI 0`, overridden at runtime by `SetRelationship`;
   - `npc_perception` 9 and 7;
   - both were moved by `SetOrigin` out of the Society interior's slow 16 s think band;
   - `sentry3` never started a schedule (`Schedule (none)`, `m_IdealSchedule 0`).
   Recovering why the sense pass leaves the player out of `Sighted()` (the `CAI_Senses::Look`
   gate) belongs to the senses owner. Unrecovered.

D. **What a clean steps-2–3 run needs:** a ranged Troika human that has a body, authored hostile
   to the player (not flipped by `SetRelationship`), running schedules, with `hint_groups`
   covering group 1, placed within 1024 units of the warehouse rows. On this save, re-enable rows
   462–465 first (`ScriptUnhide` on each) if the census's first-hit prediction (row 465) is what
   is being tested.
