# VtMB player zone-authority research case

This offline case recovers the world-owned safe-area state and the client/server decisions that
turn it into HUD presentation and pre-commit player verb legality. It does not run or modify the
game. Generated decompilation and Ghidra projects remain below `ELYSIUM_WORK_ROOT`.

## Questions

- What does `CWorldEvents::SetSafeArea` retain, replicate and publish for values 0, 1 and 2?
- Which map entities and Python calls author that state, and what is the initial value on spawn or
  restore?
- Which server predicates reject attacks, feeding and Disciplines before their gameplay commit?
- Where is the Bloodbuff-while-lockpicking Elysium exception implemented?
- How does the client choose the combat, Masquerade or Elysium HUD icon, and does it disable
  quickbar actions or only present server authority?
- Is zone state saved across travel, restored, or re-authored by each map?

Zone semantics belong in `docs/vtmb/game_runtime.md`. Discipline eligibility belongs in
`docs/vtmb/disciplines.md`; the player/world relationship belongs in
`docs/vtmb/player-entity.md`.

## Procedure

1. Recover the server `CWorldEvents` datamap input, its retained field and every read/write xref.
2. Trace the replicated or messaged state into `client.dll` and the HUD icon consumer.
3. Follow `vdiscipline_int`, attack and feed admission backward from their irreversible commits.
4. Classify the Bloodbuff/lockpick branch from native state, not help text.
5. Survey authored `SetSafeArea` calls and map defaults without treating patch scripts as native
   behavior.
6. Reserve frame-exact presentation and refusal feedback for controlled retail acceptance.

```powershell
uv run elysium research player-zone-authority research/cases/player-zone-authority/specs/player_zone_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_player_zone_<task>" --kinds funcs,asm,xrefs,fields,datamaps,grep
uv run elysium research player-zone-authority research/cases/player-zone-authority/specs/player_zone_client.json --binary "<VtMB>/Vampire/cl_dlls/client.dll" --project-dir "<work>/research/ghidra/project_player_zone_client_<task>" --kinds funcs,asm,xrefs,fields,grep
```

Use `--dry-run` to validate either specification. Generated evidence is written below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/player-zone-authority-{server,client}/`.

## Recovered boundary

- The singleton world retains clamped `m_nAreaType`: `0` combat, `1` safe/Masquerade and `2`
  Elysium. `worldspawn` key `safearea`, `world.SetSafeArea(...)` and the native `safearea` callback
  author the same field. `DT_WORLD` replicates it as two bits.
- A changed value applies policy to every connected player before publication. Elysium equips
  `item_w_unarmed` and removes all owned native and targeted Discipline effects. Safe-area entry
  ends compiled indices 3 and 11, Celerity and Protean. Combat entry performs no immediate
  teardown.
- The shared player-action predicate reaches weapon, feed and Discipline decisions and reports
  Elysium/no-frenzy state. Feed eligibility and both Discipline execution families reject before
  commit; `Weapon_CanSwitchTo` permits only `item_w_unarmed` in Elysium. All remain
  server-authoritative.
- The sole Bloodbuff exception is native and stateful: compiled Discipline index 4 is admitted
  when the player's retained action target reports compact player action 300, `LockPick`. The
  branch precedes the ordinary Elysium blocker.
- Client `DT_World` receives the enum at `+0x430`; the HUD maps it to `area_icon_combat`,
  `area_icon_safearea` or `area_icon_elysium` and recreates the sprite when it changes. The focused
  quickbar dispatcher sends `vdiscipline_int` without reading the zone, so no client-side refusal
  is established.
- Activity production and NPC witnessing remain zone-independent, but the sole terminal criminal
  and supernatural incident thunks admit their downstream police/Masquerade consumers only while
  the world value is nonzero. Combat area therefore suppresses consequences at the late incident
  boundary.
- Replication and per-map re-authoring are closed. The current static pass did not establish a
  save-datamap record for `m_nAreaType`, so same-map save/load retention and exact refusal feedback
  remain controlled retail questions.
