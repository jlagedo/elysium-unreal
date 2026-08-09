# VtMB computer-terminal research case

This case recovers the original `CBaseTerminal` / `CPropHacking` interaction
contract behind VtMB computer props. It is an offline investigation: the
evidence is the hash-pinned retail server and client modules, the patch-first
exported map and `hackterm` corpus, decoded saves, and derived Ghidra output. It
does not run or modify the game and it does not implement the Unreal terminal.

## Questions

- Which base classes and virtuals own use eligibility, entry, active input,
  cancellation, and exit?
- What server-to-client messages open, update, and close the terminal panel,
  and which client class consumes them?
- What command grammar and screen-state machine does `AcceptCmd` implement?
- How do `difficulty` and `skilltype` select the feat, roll, retry, and lockout
  rules?
- In what order do dependency checks, text, sounds, `runscript`, and
  `OnTrigger0` through `OnTrigger7` execute?
- How are local and global email flags loaded, mutated, and saved?
- What do `m_HackFlags`, `m_nMaxInput`, screen-saver timing, and
  `keypad_strings` control?

The static entity/data/save surface and the `sp_tutorial_1` `tuthack` chain are
bounded in `docs/vtmb/computer-terminals.md`. The first native pass also joins
the server session virtuals, client character-texture renderer, `hackcmd`
transport, input flags, and Function dependency → runtext → output → runscript
order. The remaining parts of the questions above are kept explicit in that
document and the specifications.

## Best offline discovery sequence

1. Copy the completed analyzed Ghidra project to a workstream-private
   directory; never open the shared project directly.
2. Run the server specification to recover the base and derived vtables,
   terminal use lifecycle, command dispatcher, skill check, outputs, email
   state, screen saver, and file loader.
3. Run the client specification to find the replicated terminal class, panel,
   message consumers, and command producers.
4. Join the native results to the exported `sp_tutorial_1.ents`,
   `tutorial_computer.txt`, `tutorial.py`, decoded saves, and the 22-map entity
   census.
5. Record only corroborated behavior in the canonical VtMB document; keep raw
   decompilation and game-derived output below `ELYSIUM_WORK_ROOT`.

The driver verifies each module against the tracked hash and writes derived
output below `$ELYSIUM_WORK_ROOT/research/ghidra/out/`:

```powershell
uv run elysium research computer-terminals research/cases/computer-terminals/specs/computer_terminal_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_terminal_<task>" --kinds funcs,xrefs,fields,vtables,grep
uv run elysium research computer-terminals research/cases/computer-terminals/specs/computer_terminal_client.json --binary "<VtMB>/Vampire/cl_dlls/client.dll" --project-dir "<work>/research/ghidra/project_terminal_<task>" --kinds funcs,xrefs,fields,vtables,grep
```

For a focused rerun, append `--address <address>` and narrow `--kinds`. The
tracked specifications preserve the research questions, address seeds, field
probes, and binary identity. Generated decompilation is game-derived and must
not enter Git.

## Consumers

- Canonical behavior: `docs/vtmb/computer-terminals.md`
- Generic output queue: `docs/vtmb/entity_io.md`
- Tutorial closure: `docs/vtmb/sp_tutorial_1-event-surface.md`
- Completion and implementation status: `docs/project/roadmap.md`
