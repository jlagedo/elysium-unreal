# The NPC kernel ledger

The whole of `CAI_BaseNPC`'s family in `vampire.dll` — every class, vtable slot, field and
reachable function — as generated tables, so a story starts from a query instead of a
re-discovery. Everything else in this directory is written by

    uv run elysium research kernel_ledger          # regenerate
    uv run elysium research kernel_ledger --check  # verify the committed tables, write nothing

from the whole-body corpus (`research/tooling/ghidra/driver/corpus.py`; the SQLite under
`$ELYSIUM_WORK_ROOT/research/ghidra/corpus/`). The shape tables — `layout.md`, `layout.tsv`,
`signatures.md`, `signatures.tsv` — are written by

    uv run elysium research kernel_shape          # regenerate
    uv run elysium research kernel_shape --check  # verify, write nothing
    uv run elysium research kernel_shape --residue <path>   # the rows no reading has settled

from the same corpus, the datamap records `datamap_types` replays
(`$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-vampire.dll.json`), Source SDK 2013's
headers, and the two reading overlays beside the tool (`kernel_fields.tsv`,
`kernel_signatures.tsv`). The tables carry addresses, names, offsets,
edges and counts — the category `docs/vtmb/` already commits — and never a decompiled body.
The provenance line at the top of each file names the module hash and the corpus dump date.

Two tables are not written from the corpus alone. `checklist-<band>.md` is the ledger joined with a
**verdict overlay**, `research/tooling/ghidra/driver/kernel_verdicts.tsv`; `checklist-0-9.md` and
`checklist-10-18.md` are the committed ones (story 29d added the second), and any other band renders
on demand:

    uv run elysium research kernel_ledger --checklist 10-18   # render one more band's checklist
    uv run elysium research kernel_ledger --bodies 0-9        # the reading packs, out of repo
    uv run elysium research merge_verdicts --from <dir>/verdicts-*.tsv --band 0-9
    uv run elysium research merge_verdicts --audit            # what each band still owes

A verdict is one word a porting story wrote against one retail function after reading its
decompiled body, plus the port target and the one-line reason: **rule** (a formula, a threshold,
an ordering or a state write; ported verbatim onto the port's virtual with a test), **mechanism**
(Unreal supplies it; the target names the service seam and nothing is ported), **present** (the
port already runs it; the target names the port function), **dead** (nothing can observe it),
**unsettled** (read and not settled, with the reason — counted apart, because it is a
recorded failure to reach a verdict rather than a verdict).

**The strict rule (spec 0019 story 1).** The 29-series verdicted by closure — "is it reachable?" —
and came back 2,205 `rule`, 0 `dead`, with the `CAI_Motor` ground step, the physics tick, the debug
overlays and SafeDisc's `CSecureType` scrambler all ported. Story 1 re-judged every row under one
question: *what would notice this body's absence?* The answer leads the row's evidence as a tag:

    [0019/1 obs=<kind>: <what names it>]     rule / present — the observable
    [0019/1 seam=<Unreal service>]           mechanism — what replaces the body
    [0019/1 dead=<why nothing sees it>]      dead

with `; was <verdict>` inside the bracket where the pass changed the word. The kinds are
`keyfield`, `input`, `output`, `schedule` (a `TASK_*`, `COND_*`, `SCHED_*` or operand a schedule
text names), `script`, `dlg`, `rulebook`, `save`, `timing` (a number or state the player sees or
hears), `witness`, and `via` — a helper observable only through the rule that uses it, written
`via: 0x<row>` or `via: slot <N>`, which the check refuses when the row it leans on is not itself
`rule` or `present`. A row nobody can name an observable for is `mechanism` or `dead`.

    uv run elysium research kernel_lists            # render delete-list.md and seam-list.md
    uv run elysium research kernel_lists --check    # every row tagged, both lists current
    uv run elysium research kernel_lists --packs    # the judgment packs, out of repo
    uv run elysium research kernel_lists --fold <judged.tsv>…   # judgments -> a merge_verdicts batch

The target column keeps the PORT target through a re-verdict: a `dead` row whose target still names
a port body is a body owed deletion (`delete-list.md`), a `mechanism` row whose target names one is
a body owed a seam (`seam-list.md`), and spec 0019 story 6 writes `-` when the body is gone.
`gen_kernel_shape` emits a `dead` row's `hand:` / `default:` spelling exactly as before, so a
judgment changes nothing the runtime does.

The overlay is the record and the rendered checklist is a view of it, which is the whole point: the
checklist is regenerated from the corpus on every run, and a verdict has to survive that. A row
with no overlay entry renders an empty verdict. **A verdict counts as a citation** — it says the
body was read and what was done with it — so `coverage.md`'s `## Verdicts by layer band` table
measures each band's core functions against port citations, oracle citations *and* verdicts, and
its **Neither** column is the acceptance measure of stories 29c, 29d and 29e.

A `rule` row's target is one of four spellings, and the last two are read by `gen_kernel_shape`:
`FElysiumSomething::Method` (the port method that carries the body), `registry:<slot>` (a species
override of a constant-returning virtual: a row in the class registry, not code),
`default:<literal>` / `default:void` (retail's whole body is one literal, so the generator emits
that body and the automation suite calls it), and `hand:<PortMethod>` (the body is written by hand
in the substrate, so the generator emits no definition and the linker checks the claim).

Story 29c-1 made `hand:` the common case — 235 slots carry it — and widened it from `rule` and
`present` to `mechanism` as well, because a mechanism routed through a named service seam is a
written body like any other. `--report` prints the remaining stubs **broken down by story band**
with their verdicts, which is what makes "the stub count for this band" a measure a story can be
held to rather than a single number over the whole vtable.

The reading packs under `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/` carry decompiled
bodies and are never committed, the same rule the rest of `research/` follows.

The tables have one consumer that is not a reader:

    uv run elysium research gen_kernel_shape          # regenerate
    uv run elysium research gen_kernel_shape --check  # verify the committed C++, write nothing
    uv run elysium research gen_kernel_shape --report # the census summary, writing nothing

`research/tooling/gen_kernel_shape.py` transcribes `layout`, `signatures`, `classes` and the slot
bodies into project source — `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShape.cpp` (the
census the runtime asserts its own shape against), `ElysiumNpcKernelSlots.inl` (one `virtual` per
Troika-line slot the port has no body for) and `ElysiumNpcKernelSlots.cpp` (their stubs, each
tallying `elysium.stubs` with the retail address and the story that owns it). It reads the corpus
through `kernel_shape.build`, so a table that drifts from the ledger fails generation rather than
being emitted. Nothing in `uv run elysium build` runs it.

## How to read it

Start from the question:

| Question | Table |
|---|---|
| Which classes are NPCs, what do they derive from, which entity classnames spawn them? | `classes.md` |
| Who fills vtable slot *N* — base, Troika, which species override — and does the port cite it? | `slots.md` |
| What is `+0xNNNN`, who writes it, who reads it, what does the port call it? | `fields.md` |
| What type is `+0xNNNN`, what fills the words no datamap declares, what does a species add? | `layout.md` |
| What does slot *N* take and return? | `signatures.md` |
| What does function `0x10……` touch, fill, call, and where is it already cited? | `functions.md` |
| In what order do the kernel's functions depend on each other? | `order.md` |
| Which kernel functions does the rest of the game call — the producers other subsystems own? | `entries.md` |
| What has neither the port nor the oracle mentioned yet; which bodies are damaged? | `coverage.md` |
| Which functions still have no name? | `unnamed.md` |
| What did the porting story decide about every function of layers 0–9, and why? | `checklist-0-9.md` |
| The same, for the middle layers 10–18 | `checklist-10-18.md` |
| Which port bodies did the strict pass judge unobservable, and where do they stand? | `delete-list.md` |
| Which port bodies are mechanisms owed an Unreal seam, and which service? | `seam-list.md` |
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
- **Shape tiers.** `layout.md` and `signatures.md` say per row where the answer came from:
  `datamap` (a record states name, type, count), `interior` (SDK 2013's sub-layout of an output,
  a `CUtlVector`, a `Vector`, an array element), `sdk` (SDK 2013 declares the slot's method and
  the image pops the words it declares), `sdk-order` / `doc` / `walked` (a reading, with the
  addresses it read, from the overlays), `evidence` (no reading: the width and kind every untyped
  access agrees on, or the image's `RET n` arity with the prototypes' kinds), `open` /
  `unsettled` (nothing settles it, and why). A body's `this` counts as an NPC only when it fills
  a family vtable or is typed on a family class; a closure body with an untyped receiver can add
  to a word others establish but never makes one.
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
- It does not name functions. `unnamed.md` is the backlog (a `FUN_` or the dump's `vfuncN`); a
  recovered name lands in `corpus names` with its evidence (`corpus harvest` proposes them) and
  the next regeneration picks it up. A name's tier travels with it: `binary` (the image, or the
  VC6 SP5 archive's bytes), `doc`, `inferred`, `accessor` (coined from the one word the body
  touches -- the member is the fact, the spelling is convention). An `unsettled` overlay row's reason is shown in
  `unnamed.md` and in `coverage.md`'s `CAI_BaseNPC` slot list. *Core* in `coverage.md` is a
  family or helper class method, or a body touching an offset past `CBaseCombatCharacter`'s
  layout, so a name that moves a body into `CBaseEntity`'s namespace also moves it out of the
  core and behind the walk's boundary.
- It does not decide what to port. It says what exists, who depends on what, and what is
  already cited; the spec's build order is derived from it, not stored in it.
