# VtMB player-entity research case

This offline case recovers the complete server-side player object: inherited datamap, construction
and spawn, command-timed update lifecycle, world aliases, camera/body relationships and the save or
level-transition joins to carried entities. It consolidates established player slices without
re-running their domain research. It does not run or modify the game and does not implement the
Unreal player. Generated decompilation and copied Ghidra projects remain below
`ELYSIUM_WORK_ROOT`.

## Questions

- What are all 277 saved player fields, which class declares each, and which are authoritative,
  derived, cached or presentation-only?
- What runs in constructor, spawn, `PreThink`, player `Think`, movement and `PostThink`, in exact
  order for replayed and new user commands?
- Which of those branches belong to movement, use, feeding, disciplines, weapons, law, stealth,
  animation, camera or teardown, and which remain unclassified?
- Which of the 157 dynamic records are externally named inputs, think registrations, save fields or
  adapters, and what are their exact types and handlers?
- How do `!player`, `!pvsplayer`, `FindPlayer`, activator provenance and the temporary
  `!playercontroller` relationship name the same player without sharing one mechanism?
- How do the durable player, map-local entity, body/view state, four viewmodels and carried
  moveable entities construct, save, transition, restore and retire?

The completed contract belongs in `docs/vtmb/player-entity.md`. Domain details remain in their
existing owning documents; this case owns the player-wide ordering and identity joins for RE51.

## Procedure

1. Use a workstream-private copy of the analyzed Ghidra project. Never mutate the shared project.
2. Run the server specification against the hash-pinned patch-first `vampire.dll`.
3. Dump the player, combat-character and inherited datamaps. Reconstruct the dynamic player
   builder with `parse_datamap_builder.py`; distinguish externally named inputs from think records
   instead of classifying every callback-bearing `VOID` row as an input.
4. Join the `CBasePlayer` and `CHL2_Player` vtables to construction, spawn, user-command,
   PreThink/Think/PostThink, damage/death and save/transition callers.
5. Build the 277-field ledger from the save manifest plus datamap records, then use field xrefs to
   classify writers and readers. A saved field is not automatically authoritative.
6. Trace special player lookup and cinematic-controller creation/removal separately from ordinary
   target-name matching.
7. Reuse conclusions from the movement, stealth, animation, feeding, inventory, discipline and
   camera cases only where their pinned addresses join this lifecycle. Keep their detailed findings
   in their owning documents.
8. Reserve controlled retail capture for remaining transition timing, camera embodiment and
   teardown acceptance after static order is known.

The driver verifies the binary hash and writes derived output below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/player-entity/`:

```powershell
uv run elysium research player-entity research/cases/player-entity/specs/player_entity_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_player_entity_<task>" --kinds funcs,asm,xrefs,fields,vtables,datamaps,grep
```

For a focused first pass, append
`--address 1015af10 --address 10350830 --address 1016be10`. Use `--dry-run` to validate the
specification without running Ghidra.

The lifecycle follow-up resolves the two vtables first, then extracts the concrete slots and
direct helpers named in the specification. The current pinned build maps slot 436 to
`CHL2_Player::PreThink`, 437 to `CBasePlayer::PostThink`, 444 to `UpdateClientData`, 449 to
`SetAnimation`, 458 to `ItemPostFrame`, 470 to the stealth debug dump, 471 to the timed stealth
recompute, 472 to retained suit-power bookkeeping and 484 to an empty final hook.

The dynamic player datamap follow-up consumes the generated builder dump:

```powershell
uv run elysium research parse_datamap_builder "<work>/research/ghidra/out/player-entity/1015af10_PlayerDatamapBuilder.decomp.txt" --recs 10580f24 --count 157
```

The builder tail assigns record array `0x10580f24` and count `0x9d`. The reconstruction yields ten
externally named input records. Its apparent eleventh entry is the unnamed
`CBasePlayerPlayerDeathThink` registration (`VOID`, flag `0x20`, callback `FUN_101668b0`), not an
AcceptInput name.

## Initial boundary

- The player is one engine-created `player` entity and exactly one save entity carries
  `FENTTABLE_PLAYER`.
- Its inherited save chain contains 277 fields; the existing documentation classifies families,
  not every record and consumer.
- `!player` and `!pvsplayer` use the special leading-`!` single-result path.
- User commands synchronously run PreThink, player Think, movement/impacts and PostThink before the
  ordinary world think and event-queue pass.
- `PreThink` has recovered vehicle, game-over/lock, death and late-stealth gates. Death-think
  returns only after the VtMB rule pass, item pre-frame, water, client-data and time-based-damage
  work have run.
- `PostThink` has a recovered live main body and common tail: use/item/animation/frame/weapon/
  sound/physics precede simulated-entity cleanup, keyring, hunger, status and cinematic-state work.
- Criminal, supernatural and investigate activity levels are independent `0..5` channels. The two
  timed setters raise/refresh finite `pl_min_act_timer`-floored deadlines, while investigate is a
  direct replacement; duration `-1` is not indefinite.
- Witnessed criminal incidents feed delayed police response. Witnessed supernatural incidents feed
  a `debug_masquerade_timer`-limited `ChangeMasqueradeLevel(+1)` and may independently feed police
  response. Upstream admission is distributed across player activity producers, per-NPC visual
  witness thresholds and NPC schedule translation; the sole terminal incident thunks then require
  non-combat world area state before police or Masquerade policy.
- Police response retains the highest queued severity and original randomized deadline, requires
  its witness handle to survive, applies response-grace delta spawning through the global NPC
  maker or a wait area, then tracks cop pursuit, hunter pursuit and heightened alert separately.
- Slot 472 executes inherited CHL2 suit-power bookkeeping, but no VtMB datamap or authored-content
  consumer has been found; it is mechanism baggage, not yet a reproduction requirement.
- The 157-record player datamap exposes ten external inputs; the callback-bearing unnamed
  `PlayerDeathThink` row explains the previous count of eleven.
- The `player` factory allocates one `0x2548` CHL2 player, the constructor establishes owned
  storage, client admission binds it and virtual slot 103 performs the gameplay spawn reset.
- Ordinary player `Think` only dispatches a due scheduled think; the unnamed datamap callback is
  the staged death state machine, whose terminal helper conditionally calls `Spawn` rather than
  deleting the entity.
- Restore rebases saved deadlines and map placement under the shared spawn/restore gate. The
  destructor frees player-owned helpers and containers, not the item and camera entities named by
  handles.
- The complete 277-field ledger, camera/body joins, carried-item transition and live world-side
  teardown still remain open. Law admission still needs controlled retail acceptance across
  thresholds, occlusion, ignore windows and refusal presentation; static zone policy is closed.

No new decompilation finding is claimed until the generated context is inspected and written into
`docs/vtmb/player-entity.md`.
