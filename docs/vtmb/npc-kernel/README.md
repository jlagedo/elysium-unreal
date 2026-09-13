# The NPC kernel ledger

The whole of `CAI_BaseNPC`'s family in `vampire.dll` — every class, vtable slot, field and
reachable function — as generated tables, so a story starts from a query instead of a
re-discovery. Everything else in this directory is written by

    uv run elysium research kernel_ledger          # regenerate
    uv run elysium research kernel_ledger --check  # verify the committed tables, write nothing

from the whole-body corpus (`research/tooling/ghidra/driver/corpus.py`; the SQLite under
`$ELYSIUM_WORK_ROOT/research/ghidra/corpus/`). The tables carry addresses, names, offsets,
edges and counts — the category `docs/vtmb/` already commits — and never a decompiled body.
The provenance line at the top of each file names the module hash and the corpus dump date.

## How to read it

Start from the question:

| Question | Table |
|---|---|
| Which classes are NPCs, what do they derive from, which entity classnames spawn them? | `classes.md` |
| Who fills vtable slot *N* — base, Troika, which species override — and does the port cite it? | `slots.md` |
| What is `+0xNNNN`, who writes it, who reads it, what does the port call it? | `fields.md` |
| What does function `0x10……` touch, fill, call, and where is it already cited? | `functions.md` |
| In what order do the kernel's functions depend on each other? | `order.md` |
| Which kernel functions does the rest of the game call — the producers other subsystems own? | `entries.md` |
| What has neither the port nor the oracle mentioned yet; which bodies are damaged? | `coverage.md` |
| Which functions still have no name? | `unnamed.md` |
| Which `docs/vtmb` section walks address `0x10……`? | `index.md` |
| The raw call graph inside the closure | `graph.tsv` |

**The closure** is every function filling a slot of a family class (a class whose primary vtable
reaches the NPC slot range), the helper classes the kernel owns (`CAI_Motor`, `CAI_Navigator`,
`CAI_Hint`, `CAISound`, the goal and behavior classes, the scripted-sequence classes,
`CNPCMaker`…), `NPCThink` and `RunAI`, and their callees to a bounded depth. The walk stops at the
entity base classes (`CBaseEntity`, `CBaseAnimating`, `CBaseFlex`, `CBaseCombatWeapon`,
`CBasePlayer`): their bodies that fill inherited NPC slots are rows, their callees are not.

## What is a fact and what is a heuristic

- **Fact.** Vtable slot bodies, direct call edges, resolved virtual edges, datamap fields
  (name, offset, width, type), strings a body references, decompiler damage warnings. These
  are the corpus's own tables.
- **Candidate.** A `slot-candidate` edge: the body dispatches through `this` at a slot the
  family fills, so any family body at that slot may be the callee. *Outside touches* in
  `fields.md`: a body outside the closure touched the offset through a receiver the typer could
  not name — another structure may collide numerically at that offset.
- **Heuristic.** READ versus WRITE. The corpus records which offsets a body touches, not the
  direction; the ledger classifies each site from the decompiled C (`= v`, `++`, `op=`, or the
  destination of a copy routine is a write; everything else a read). A body that both reads and
  writes an offset appears in both columns. Offsets reached through `param_1` in a body that
  never names `this` are marked `?` — the receiver is guessed. The port-member column in
  `fields.md` is the identifier declared where a header cites the offset, a guess too.
- **Not trusted.** A body marked ‼ has a damaged decompilation; its edges and touches are
  partial. Read it with `corpus asm <addr>`.

## Conventions the tables rely on

- The port cites a retail function as `0x10……` and a field as `+0xNNNN` in a comment; the
  oracle does the same in prose. Those two spellings are what the citation columns join on. A
  citation of an address *inside* a body counts for that body.
- `order.md` layers the call graph only (direct and resolved virtual edges). Field dependencies
  are annotations: *Producers later* names fields a function reads whose every writer sits in a
  later layer; *Unwritten* names fields nothing in the closure writes — their producer is another
  subsystem's, listed under *Outside touches*.
- `functions.md` *Callers* counts closure edges by kind (`d`irect, `v`irtual,
  slot-`c`andidate) and, separately, direct callers outside the closure.

## What it does not do

- It does not model inheritance in `fields`: the table is the flattened `CAI_BaseNPCTroika`
  layout, and offsets a species adds are listed in a second table with their owning class.
- It does not name functions. `unnamed.md` is the backlog; a recovered name lands in
  `corpus names` with its evidence and the next regeneration picks it up.
- It does not decide what to port. It says what exists, who depends on what, and what is
  already cited; the spec's build order is derived from it, not stored in it.
