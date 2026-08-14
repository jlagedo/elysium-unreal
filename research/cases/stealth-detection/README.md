# VtMB stealth-detection research case

This offline case recovers the faithful player-stealth observer from authored light and skill
tables through NPC sight, stimulus memory and the found/lost output edges. It also isolates
`trigger_stealth_mod` from the generic trigger machinery. It does not run or modify the game and
does not implement the Unreal stealth system. Generated decompilation and copied Ghidra projects
remain below `ELYSIUM_WORK_ROOT`.

## Questions

- How are the four `stealth.txt` tables parsed, indexed and combined with the player's Sneaking
  feat and sampled light?
- Which player fields carry light at feet/centre/head, the derived vision-distance scalar and the
  derived vision-cone scalar, and when are they refreshed?
- How do an observer's authored `npc_perception`, `vision` and `hearing` values combine with the
  target's stealth state, distance, view cone, occlusion and movement or sound?
- What does `trigger_stealth_mod` change on enter, leave, enable, disable and save/restore?
- Which transition produces `OnFoundPlayer`, `OnLostPlayerLOS` and `OnLostPlayer`, and which parts
  are direct visibility, remembered stimulus, enemy selection or schedule policy?
- Which part of the transaction is useful for the stealth HUD without making presentation
  authoritative?

Established NPC update, memory, relationship and enemy-selection facts remain in
`docs/vtmb/npc-ai-reverse-engineering.md`; feat construction remains in
`docs/vtmb/skills-and-checks.md`; tutorial demand remains in
`docs/vtmb/sp_tutorial_1-event-surface.md`. The completed observer contract belongs in
`docs/vtmb/stealth.md`.

## Reproduction

Use a workstream-private copy of the analyzed Ghidra project. Generated output remains below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/stealth-detection/`:

```powershell
uv run elysium research stealth-detection research/cases/stealth-detection/specs/stealth_detection_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_stealth_detection_<task>" --kinds funcs,asm,xrefs,fields,vtables,consts,grep
```

For a focused rerun, append `--address <address>` and narrow `--kinds`. Use `--dry-run` to
validate the specification without running Ghidra.

## Evidence boundary

Static evidence closes table parsing, field writes, branch ordering, output production and memory
mutation. Engine-owned light and trace answers remain service inputs rather than Source mechanisms
to port. Exact detection timing under changing light, partial occlusion and save/restore requires a
controlled retail incident after the static transaction is joined.

## Recovered boundary

- `Stealth.txt` is four tables. The player derives sight-range scalar, cone scalar and hearing
  reduction from one light row and the effective Sneaking feat; it refreshes every 0.1 seconds but
  samples feet, centre and head round-robin, so a complete light triplet takes about 0.3 seconds.
- VHuman visual admission multiplies observer effective vision distance by the target's sight
  scalar. The separate cone query consumes the target cone scalar.
- The closest-player cone/LOS cache refreshes every 2 seconds, bypasses its far trace at or below
  512 units, and retains blocked in-cone LOS for 8 seconds. The committed-enemy path then requires
  ten consecutive failed checks before `OnLostPlayerLOS`.
- Eligible player sound insertion subtracts the target's table-derived hearing distance from the
  sound radius and floors at zero before ordinary NPC hearing and memory.
- `trigger_stealth_mod` adds its authored integer on begin and removes it on end. Raw overlapping
  contributions stack; only the effective getter clamps to `[-10,+10]`.
- `OnFoundPlayer`, `OnLostPlayerLOS`, and `OnLostPlayer` are different transactions: committed
  LOS admission, debounced LOS loss, and later eluded-memory/enemy loss respectively.

The consuming contract is `docs/vtmb/stealth.md`. The separate `stealthkillrules.txt` victim and
deaf-zone transaction remains outside this case, as do abnormal modifier-volume teardown and
controlled live timing acceptance.
