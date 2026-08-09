# VtMB inventory research case

This case recovers the original server-side inventory model and joins it to the
`sp_tutorial_1` item, container, trigger, Python and dialogue graph.  It is an
offline investigation: the evidence is the hash-pinned retail binaries, the
patch-first exported data/map corpus, decoded saves and Ghidra output.  It does
not run or modify the game and it does not implement the Unreal inventory.

## Questions answered

- How are carried items represented, owned, stacked, equipped and persisted?
- What do `HasItem`, `GiveItem`, `RemoveItem`, `AmmoCount`, `GiveAmmo` and
  `HasWeaponEquipped` actually test or mutate?
- How does the keyring participate in ordinary item queries?
- How do `item_container*` inputs transfer or destroy items and fire outputs?
- When does `trigger_inventory_check` evaluate its named item?
- What exact behavior does the tutorial demand for the lockpick, chopshop key,
  tire iron, `.38` and its ammunition?

All of these are closed by the findings in the specifications and
`docs/vtmb/inventory.md`.  The vendor buy/sell price formula remains economy
scope, UI skin/layout remains UI scope, and individual weapon combat behavior
remains weapon/animation scope; none blocks the inventory ownership and transfer
contract.

## Best offline discovery sequence

1. Copy a completed analyzed Ghidra project to a workstream-private directory.
2. Run the server specification for Python methods, item/owner operations,
   containers, barter, drop, trigger behavior, fields and the trigger vtable.
3. Run the raw server specification for the two data-table handlers whose bytes
   need on-demand disassembly.
4. Run the client grep specification for inventory, hotkey, drop and barter UI
   command producers.
5. Compare decoded saves from before and after pickup/container transfers.
6. Join those semantics to the exported `.ents`, `tutorial.py`,
   `jack_tutorial.dlg`, `hunterv.dlg`, and patch-first item data.

The Ghidra commands verify the configured module against its specification hash,
serialize each headless run, and write only derived output below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/`:

```powershell
uv run elysium research inventory research/cases/inventory/specs/inventory_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_inventory" --kinds funcs,asm,xrefs,fields,vtables,grep
uv run elysium research inventory research/cases/inventory/specs/inventory_server_raw.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_inventory" --kinds asm
uv run elysium research inventory research/cases/inventory/specs/inventory_client.json --binary "<VtMB>/Vampire/cl_dlls/client.dll" --project-dir "<work>/research/ghidra/project_inventory" --kinds grep
```

The save comparison is read-only:

```powershell
uv run elysium research probe_sav "<before-pickup>.sav" --entity item_
uv run elysium research probe_sav "<after-pickup>.sav" --entity item_
```

Always copy an analyzed project and pass that private directory through
`--project-dir`; concurrent research must never open the shared database.  For a
focused rerun, append `--address <address>` and narrow `--kinds`.  The
tracked specification preserves the seed addresses and field probes; the
generated decompilation remains game-derived and must not enter Git.

## Consumers

- Generic behavior: `docs/vtmb/inventory.md`
- Map-specific closure: `docs/vtmb/sp_tutorial_1-event-surface.md`
- Completion and implementation status: `docs/project/roadmap.md`
