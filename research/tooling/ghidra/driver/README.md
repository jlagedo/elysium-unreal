# Ghidra RE workspace (the VtMB binaries)

Headless Ghidra against the VtMB modules: the runner, the tracked analysis scripts, and the
passes that name a module's C runtime, global constructors, class hierarchy, datamaps and
console variables before any case-specific work starts.

**Start at *Preparing a program*.** Those passes are ordered — a later one reads the names an
earlier one wrote — and every module already in the project has been through them.

The workspace sections further down record the addresses each investigation settled on: the
main-menu item geometry and pause item set compiled into `GameUI.dll` (`CBasePanel`,
`CGameMenuButton`; see `docs/project/rebuild-strategy.md` → *Menu — full-fidelity spec (M0)*,
section E), the script API surface, and the choreographed scenes.

## What's here

| Path | Tracked | What |
|---|---|---|
| `DumpMenu.java`  | ✅ | recon: dumps RTTI classes, menu-related strings + xrefs, decompiles seed functions |
| `DumpGrep.java`  | ✅ | parameterized recon: strings/classes/function names by regex → decompile the matches |
| `DumpFuncs.java` | ✅ | targeted decompiler: follows seed functions (+callees) and vftables → C pseudocode |
| `DumpAsm.java`   | ✅ | raw disassembly of a function, or a flat run at any address (disassembles on demand) |
| `DumpXrefs.java` | ✅ | every reference to an address + the function containing each referencing site |
| `DumpConst.java` | ✅ | the dword at an address as hex/int/float (the decompiler renders these as `_DAT_`) |
| `DumpConVars.java` | ✅ | every ConVar/ConCommand: object address ↔ name ↔ default; labels each object `cvar_<name>` |
| `DumpAdjustorThunks.java` | ✅ | the `sub ecx,<n>; jmp` stubs in a secondary base's vftable, and the class holding each |
| `MakeFuncs.java` | ✅ | promotes disassembled-but-unowned code into functions (CALL targets + post-padding starts) |
| `DumpPyMethods.java` | ✅ | a CPython `PyMethodDef` table: name, `ml_doc`, thunk→body, then every body decompiled |
| `DumpInitTable.java` | ✅ | every global constructor, ranked by data touched — the datamap and registration builders |
| `DumpRtti.java`  | ✅ | the C++ class hierarchy with base displacements; labels each vftable with its owner |
| `DumpDatamaps.java` | ✅ | every `datamap_t` in a module: class name, base chain, builder, record base and count |
| `NameFromStrings.java` | ✅ | names functions from the VProf scopes and asserts compiled into the image; maps the source tree |
| `ApplyDatamapTypes.java` | ✅ | every datamap as a Ghidra structure, hung on its class, so a member access reads as a name |
| `DumpCorpus.java` | ✅ | the whole module as JSONL: every function decompiled with the decompiler's warnings about it, every string's referrers, every named global, every class structure |
| `DumpVtables.java` | ✅ | every class vftable slot by slot — the ground truth virtual call edges are joined against; names unambiguous slots under `name=1` |
| `DumpListing.java` | ✅ | every function's disassembly as JSONL, into its own database — the fallback for damaged C |
| `SplitFuncs.java` | ✅ | splits a function whose body swallowed its neighbours, on CALL-target and post-padding evidence only |
| `RecoverJumpTables.java` | ✅ | reads a switch's jump table out of the image when the decompiler abandoned it |
| `RecoverSignatures.java` | ✅ | recovers each function's parameter count from `RET <imm16>` or the caller's `ADD ESP,<n>` |
| `RecoverThisCall.java` | ✅ | gives a C++ method `__thiscall`, so `this` stops decompiling as `in_ECX` and its virtual dispatches name a class |
| `DumpExternals.java` | ✅ | every function a module imports from another DLL, by the `EXTERNAL:` address its call edges carry |
| `ApplyPythonApi.java` | ✅ | applies CPython 2.1.2's own declared prototypes to the Python imports, and makes the format strings those calls pass into strings |
| `BuildCrtFid.java` | ✅ | populates a FID database from imported library objects (headless "Populate FidDb") |
| `AttachFid.java` | ✅ | registers a FID database so the Function ID analyzer queries it; used as a pre-script |
| `ApplyFid.java`  | ✅ | names a program's CRT functions from the database, for programs analyzed before it existed |
| `parse_datamap_builder.py` | ✅ | reconstructs a `datamap_t` from its **decompiled builder** (the half an image read misses) |
| `crt_fid.py`     | ✅ | builds the VC6 SP5 CRT database and applies it: `stage`, `build`, `apply` |
| `symbol_sweep.py` | ✅ | drives `NameFromStrings`: `survey`, `apply`, `clear` |
| `datamap_types.py` | ✅ | drives `ApplyDatamapTypes`: `report`, `apply` |
| `corpus.py`      | ✅ | drives `DumpCorpus`, `DumpVtables`, `DumpExternals`, `DumpListing` and `ApplyPythonApi`, loads SQLite, and answers every corpus query |
| `repair.py`      | ✅ | drives the repair passes: `boundaries`, `jumptables`, `thiscall`, `signatures`, each `report` then `apply` |
| `pyapi.py`       | ✅ | extracts the CPython 2.1.2 C API from `Include/*.h` into the prototype file `ApplyPythonApi` applies |
| `corpus_mcp.py`  | ✅ | the same queries as MCP tools (`vtmb_*`), registered in `.mcp.json` as `vtmb-corpus` |
| `run.ps1`        | ✅ | headless runner (import + post-script) |
| `crt/`, `crtfid/`, `fid/` | ❌ gitignored | the staged Microsoft archives, their import projects, and the built `.fidb` |
| `project/`       | ❌ gitignored | the analyzed Ghidra project DB (derived from the user's own binary) |
| `names/`, `types/`, `corpus/` | ❌ gitignored | the naming, typing and corpus reports, and `corpus.sqlite` |
| `$ELYSIUM_EXPORT_ROOT/`           | ❌ gitignored | decompilation dumps (game-derived) |

`project/` and `$ELYSIUM_EXPORT_ROOT/` are **derived from the user's own game binary** — bring-your-own,
never committed, same policy as maps/textures. Only the scripts + this doc are tracked.

## External dependency (not vendored)

Ghidra **12.1.2** lives at `$ELYSIUM_WORK_ROOT/cache/ghidra_12.1.2_PUBLIC/` (sibling of this scripts
dir — it must NOT be nested inside `$ELYSIUM_WORK_ROOT/research/ghidra/`, or Ghidra's headless script
compiler tries to OSGi-bundle the whole install and fails). Large third-party
install, gitignored — bring-your-own, same policy as the game binaries. Java 21+
on PATH (tested: Temurin 25). Override with `run.ps1 -GhidraDir <path>`.

## Workflow

```powershell
# 1) one-time: import + auto-analyze the binary (MSVC RTTI recovers C++ class names)
research/tooling/ghidra/driver/run.ps1 -Import "E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\cl_dlls\GameUI.dll"

# 2) recon — classes, menu strings, first decompiles  -> $ELYSIUM_EXPORT_ROOT/menu_recon.txt
research/tooling/ghidra/driver/run.ps1 -Script DumpMenu

# 3) targeted decompile — feed seed funcs + vftable addrs found in step 2
#    (CBasePanel vftable = 1004ff3c; its ctor/CreateGameMenu = 10003ef0)
research/tooling/ghidra/driver/run.ps1 -Script DumpFuncs -ScriptArgs "funcs=10003ef0 vtables=1004ff3c out=E:/dev/elysium-unreal/$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/basepanel.txt"
```

`DumpFuncs` args (space-separated `key=value`): `funcs=` seed entry points (hex,
decompiled with callees), `vtables=` vftable addresses (every slot decompiled),
`depth=` callee recursion (default 1), `cap=` max functions (80), `out=` output file.

`-Program` selects the imported binary (`engine.dll`, `GameUI.dll`, …).

**The parameter is `-ScriptArgs`, not `-Args`.** `$Args` is a reserved PowerShell
automatic variable, so `-Args "…"` binds to nothing, the script runs with an empty
arg list, and the failure is silent — every regex reads `null`, zero functions are
decompiled, and the dump lands at the default `out=` path.

**Leave a gap between runs.** The headless JVM releases the project lock a few seconds
*after* it prints its result, so two runs launched back-to-back race: the second aborts
with `LockException: Unable to lock project` and writes nothing. Worse, a run that dies
while saving leaves a zero-filled `db.<n>.gbf` in
`project/vtmb.rep/idata/00/~<id>.db/`, and every later open of that program fails with
`IOException: Unrecognized file format` (`LocalBufferFile.readHeader`) — the fix is to
delete that newest version file so the previous one becomes current, or to re-import.
Sleep ~8 s between invocations, or work in a scratch project (`-ProjDir`/`-ProjName`).

**A handler reached only from a data table is undisassembled, not merely unowned.**
`MakeFuncs` promotes disassembled code into functions and cannot help here — Ghidra never
turned the bytes into instructions, because the only reference is a function pointer inside
a struct array it read as data. `DumpFuncs` reports 0 functions and `DumpXrefs` reports the
string as referenced by `<none>`. `DumpAsm` is the tool: it calls `disassemble()` on demand,
so a raw run at the table's pointer produces the listing. (`engine.dll`'s `clc_*` message
handlers are the worked example — table at `0x201ac834`, records
`{const char *name; void (*func)(); int type;}` stride 12. Reading such a table's bytes
straight out of the PE is faster than any script; the section walk is ~20 lines of Python.)

**Multi-value args take one address per run.** The arg string passes through
`run.ps1` (splits on ' '), `analyzeHeadless.bat`/cmd.exe (splits on ';' and ','),
and Ghidra headless (splits on '='), so a `funcs=a;b` list arrives as separate
tokens, silently mis-pairs every following `key=value`, and the run writes to the
default `out=` path instead of yours. Invoke once per address and give each its
own `out=`.

## Preparing a program

A freshly imported module is mostly `FUN_…`, and a large share of that is Microsoft's, not
Troika's. Three passes make every later dump readable. They are one-time per program and
persist in the project.

```powershell
uv run elysium research crt_fid stage          # unpack LIBC.LIB + LIBCMT.LIB, hash-verified
uv run elysium research crt_fid build          # import, analyze and hash them into vc6sp5.fidb
uv run elysium research crt_fid apply vampire.dll   # name the CRT, then census the constructors
research/tooling/ghidra/driver/run.ps1 -Program vampire.dll -Script DumpRtti `
  -ScriptArgs "out=…/rtti-vampire.dll.txt"     # class hierarchy + vftable labels
```

`build` takes about half an hour: each archive holds 638 COFF members and each becomes its own
analyzed program. `apply` is a minute per module.

**The CRT database is the VC6 SP5 one the game actually links**, built from the archives rather
than from Ghidra's shipped approximations — which cover no VC6 service pack, and name 42
functions in `vampire.dll` against this database's 207. `stage` verifies each archive's sha256
before use, because a database built from the wrong service pack would describe a runtime the
game does not link. The Microsoft archives are third-party reference material under
`$ELYSIUM_WORK_ROOT`; they never enter the checkout.

`run.ps1 -Import` attaches the database as a pre-script, so a module imported afterwards comes
out of auto-analysis with its CRT already named and needs no `apply`. **The attachment does not
survive the JVM** — Ghidra rewrites its preferences file at exit without the user-added-file
keys — so the pre-script is load-bearing rather than belt-and-braces. `-NoFid` opts out;
`-FidDb` points at another database.

**`DumpFuncs` stops recursing at a library function.** It tests the Function ID analyzer's own
bookmarks, so the test is exact rather than a name heuristic. Seeds and vtable slots are always
honoured — only callee recursion is filtered, so asking for a CRT function by address still
dumps it. `crt=0` includes them.

### The constructor census beats grepping for a builder

`DumpInitTable` reads the bounds `__cinit` hands to `__initterm` and enumerates every global
constructor: 2393 in `vampire.dll`, 2071 in `client.dll`. Both names come from the FID pass, so
the census cannot run on an unlabelled program — which is why `crt_fid apply` runs it second.

The chain has two hops. A table slot points at an incremental-link thunk; the thunk reaches a
16-byte per-variable initializer the compiler emitted; that initializer calls the real builder,
again through a thunk. The census follows both and scores each initializer together with its
direct callees, reporting whichever touches the most data. Ranking `vampire.dll` that way puts
`FUN_1031a600` (`CBaseCombatCharacter`, 3686), `FUN_1015af10` (the player class, 1243) and
`FUN_1027a820` (`CAI_BaseNPC`, 592) at ranks 1, 2 and 5, alongside unidentified builders of
comparable weight — no string grep involved. Default-named initializers are renamed
`staticinit_<hex>`, which shows up in every later dump; `name=0` censuses without writing.

### RTTI gives every vftable an owner

`DumpRtti` walks each complete object locator — stored in the slot *before* a vftable's first
method — to the class hierarchy descriptor and its base array, then labels the vftable and gives
it a plate comment carrying the base chain.

| module | classes | multiple inheritance |
|---|---|---|
| `client.dll` | 1897 | 1556 |
| `vampire.dll` | 875 | 192 |
| `engine.dll` | 241 | 32 |
| `GameUI.dll` | 123 | 2 |
| `vguimatsurface.dll` | 19 | 0 |

`MaterialSystem.dll`, `StudioRender.dll` and `stdshader_dx8.dll` carry **no RTTI at all** — built
without `/GR`, zero type-descriptor strings in the image. The script says so explicitly, and
raises an error in the other case (descriptors present, no locator parsed), because a bare zero
cannot otherwise be told apart from a failed walk.

Multiple inheritance is why the walk earns its keep. A secondary base's vftable sits at a
non-zero offset with its own locator, and its slots are reached through adjustor thunks, so
attributing an override by chasing thunk targets mis-assigns exactly those classes —
four fifths of `client.dll`. `CAI_BaseHumanoid` is the worked example: vftables at `10497cd8`
(+6576, `IAI_MovementSink`) and `10497cc0` (+24388, `CAI_ExpresserSink`).

### The datamap census supplies `DumpDatamap`'s arguments

`DumpDatamap` reads one map whose address is already known, and its `recs=`/`count=` have to be
found first by xref'ing the builder. `DumpDatamaps` is the enumerating pass that removes that
step: a map identifies itself in the un-run image even though its records do not, because
`dataClassName` and `baseMap` are statically initialized while `dataDesc` and `dataNumFields`
are written at static-init.

So a scan for the shape recovers every class's map and the whole inheritance chain with no
decompiler pass, and the builder is one xref away — it ends in `mov [map+4], <count>;
mov [map], <recs>`, and both immediates read straight off the listing. The report emits the
ready-to-run `parse_datamap_builder` line per class. On `vampire.dll` it reproduces the three
maps recorded below exactly — `CBaseCombatCharacter` `1061664c` / recs `10616694` / 305,
`CBasePlayer` `10580edc` / `10580f24` / 157, `CAI_BaseNPC` `105c9814` / `105c9874` / 102 — and
finds 342 in total.

**The `{zero, zero, name, base}` shape is not unique to `datamap_t`.** vgui's own chained
per-class maps are identical in shape, and `GameUI.dll`, which defines no entity at all, is made
entirely of them. A record array is what tells them apart, so a candidate is only labelled when
one is substantiated — either a builder assigns it, or the image already holds it and the first
`typedescription_t` carries a readable `internalName`. Everything else is listed in a separate
section and labelled nothing, because naming one `datamap_t` would assert a type nothing
establishes.

| module | datamaps | unidentified name-and-base records |
|---|---|---|
| `vampire.dll` | 342 | 3 |
| `client.dll` | 16 | 248 |
| `engine.dll` | 1 | 21 |
| `GameUI.dll` | 0 | 40 |
| `vguimatsurface.dll` | 0 | 7 |

Each map is labelled `datamap_<Class>` and its builder renamed `datamap_<Class>_builder`, so the
constructor census's `staticinit_<hex>` entries become readable. A class may carry more than one
map, and those labels are qualified by address. Re-running is idempotent, and a record the pass
can no longer substantiate loses the label an earlier run gave it.

### The ConVar census is anchored on a vftable, not on a call-site shape

`DumpConVars` with no `ctor=` finds the registration constructors itself: a constructor is a
function that stores the `ConVar`, `ConCommand` or `ConCommandBase` vftable into its object.
**Run `DumpRtti` first** — those labels are the anchor.

Guessing from the shape of a call site does not work, and failing that way is quiet. VtMB's AI
schedule, condition and squad-slot registrations take a static object plus a string literal
exactly as a ConVar does; a shape-based sweep of `vampire.dll` returns 655 `"schedule"`,
`"condition"` and `"squadslot"` rows as though they were console variables.

Call sites reference the incremental-link thunk rather than the constructor, so the thunks are
swept too — without that every constructor reports zero registrations. A default value is
usually untyped data, so the pushed pointer is read as a C string rather than printed as an
address:

```
20a6bb60     mat_waterswirl    "mat_waterswirl"  "0.02"
20d63c00     r_shadowlod       "r_shadowlod"     "-1"
106d0798     bat_attract       "bat_attract"     …
```

| module | constructors | registrations | objects labelled |
|---|---|---|---|
| `vampire.dll` | 11 | 704 | 695 |
| `engine.dll` | 8 | 550 | 550 |
| `client.dll` | 10 | 545 | 535 |
| `GameUI.dll` | 10 | 10 | 0 |

Each object is labelled `cvar_<name>`, which is the point: a reader that decompiled to
`DAT_104d205c` now names the console variable. The script owns the `cvar_` namespace and clears
it before writing, so re-running is idempotent and corrects an earlier run.

### Adjustor thunks are rare, and the count does not follow the inheritance count

A vftable slot on a secondary base holds a `sub ecx,<n>; jmp <method>` stub rather than the
method, so following the jump attributes the override to the wrong class. `DumpAdjustorThunks`
names those stubs and reports the class and slot holding each.

**The yield is single digits** — 5 in `client.dll`, 1 in `engine.dll`, 0 in `vampire.dll` —
even though `DumpRtti` reports 1556 and 192 multiple-inheritance classes in the first two. MSVC
only emits a thunk where a secondary base's virtual is actually overridden, which is rare here.

The byte shape alone is not the thunk: several CRT routines contain a `sub ecx,<n>` followed by
a jump in ordinary code, and matching on that produces `_strcmp_adj-2` and a `-270762648`
displacement. A match counts only when the pair is a whole function or stands outside every
function *and* a data slot points at it.

```
100f2700  this-=16   FUN_100f1c40    CHudChat_at16[3]
10184560  this-=368  FUN_101839d0    VHotkeysUI_at368[6]
```

### Headless traps in this pipeline

- **A COFF object carries no compiler identity**, so Ghidra's opinion service picks `gcc`. FID
  refuses to build a library whose members disagree on compiler spec, which is how it surfaces.
  Forcing `-Processor x86:LE:32:default -Cspec windows` on import fixes that *and* the members
  the loader otherwise rejects outright.
- **`analyzeHeadless.bat` exits 0 even when the analyzer aborts or a script throws.** The exit
  code is not a result; `crt_fid.py` scans the log instead.
- **`-process` matches only the project root without `-recursive`.** A program imported from a
  container file nests under the container's own folders and is reported as not found.
- **A `.lib` imports as a container**: `-Import <archive> -Recursive` descends it through
  Ghidra's COFF archive filesystem, one program per member, no custom script needed.

## The whole-body corpus

The passes above prepare a program one question at a time. These three take it whole, so a
question stops costing a headless run and the project lock: the module is materialized once and
every later query reads a local database. A cross-reference claim — *nothing else reads this
field*, *nothing else overrides this slot* — becomes provable rather than a sweep repeated per
question.

The order is load-bearing. The corpus preserves whatever names and types the project holds at
the moment it is taken, so a dump taken before the naming and typing passes bakes in `FUN_` and
raw displacements and has to be thrown away.

```powershell
# repair what the analyzer got wrong, before anything photographs it
uv run elysium research repair all report vampire.dll             # read the reports first
uv run elysium research repair all apply                          # every module

# recover identity
uv run elysium research symbol_sweep survey                       # read-only, every module
uv run elysium research symbol_sweep apply vampire.dll
uv run elysium research datamap_types apply vampire.dll client.dll engine.dll
uv run elysium research corpus vtables --name --create            # cheap; names slots, and
                                                                  # makes functions MakeFuncs
                                                                  # structurally cannot seed
uv run elysium research pyapi                                     # CPython 2.1.2 prototypes
uv run elysium research corpus pyapi --apply                      # the modules that embed it

# take the photograph
uv run elysium research corpus externals                          # cheap; the imported names
uv run elysium research corpus dump                               # the slow step
uv run elysium research corpus listing                            # the slow step, again
uv run elysium research corpus build

uv run elysium research corpus readers 0x1564 --class CBasePlayer
uv run elysium research corpus callers 10161200
uv run elysium research corpus closure CBasePlayer --out <path>
```

`corpus reindex` rebuilds everything derived from rows already loaded — the search index and the
virtual half of the call graph — without re-reading a dump, which is how a change to *those rules*
costs seconds instead of an overnight run.

`corpus` also answers `func`, `code`, `asm`, `callers`, `callees`, `vtable`, `slot`, `grep`,
`str`, `globals`, `fields`, `twin`, `outliers`, `suggest` and `stat`; `corpus_mcp.py` exposes the
same set as `vtmb_*` MCP tools. It is launched directly rather than through
`uv run elysium research`, because the CLI writes a run report to stdout and stdout there carries
only protocol messages — so it reads `.elysium.local.env` itself.

### VtMB dispatches virtually, so a direct call graph is blind where the rules live

`vampire.dll` records 43,004 direct call edges and contains 31,270 indirect dispatch sites, and
**79.6 % of its class methods have no direct caller at all**. A `callers` query that answers
"none" for `CBaseCombatCharacter` or `CAI_BaseNPC` is not reporting an unused method; it is
reporting that the call is virtual.

`DumpVtables` supplies (class, slot) → implementation; the decompiler's
`(**(code **)(*(int *)this + 0xNN))()` supplies (caller, receiver, slot). **slot = offset / 4.**
`corpus build` joins them.

**A virtual edge is an inference and is labelled as one.** `callers` prints three sections and
never merges them:

- **direct** — a static call reference;
- **virtual** — the site's receiver class holds this function at that slot;
- **possible** — a dispatch at the same slot whose receiver class the code does not state.

Only the receiver's *own* class is joined. A call through a base pointer can land on any derived
override, and manufacturing those edges would replace one silent wrongness with another —
`corpus slot <N>` reports the whole override set instead.

**The vtable walk exists in two scripts.** `ApplyDatamapTypes` walks it to type `this`;
`DumpVtables` walks it to dump the slots. A Ghidra script compiles alone, so a change to one
belongs in the other — the two bounds especially (next `vftable_*` label, and following raw `E9`
stubs as well as Ghidra thunks).

**`MakeFuncs` cannot reach vtable-only code, and reports success while missing it.** Its two
seeds are CALL targets and post-padding starts. Code that is *only* ever reached through a data
pointer is never a CALL target, so it is only seeded when the linker happened to pad in front of
it. Run on `client.dll` and `engine.dll` it found 16,137 and 6,958 seeds and created **zero**
functions, while 2,663 and 1,038 vtable slots still pointed at code with no function at all.
(It is not useless — the same run created 682 functions in `GameUI.dll` and 535 in
`vguimatsurface.dll`, whose padding happens to line up. It is just not a bound on the problem.)

The slot itself is the missing evidence: a vftable entry *is* the statement that its target is a
function. `DumpVtables create=1` (`corpus vtables --create`) disassembles and creates at the slot
target, which is the only seed that reaches this code. It recovers two different things, and
which one depends on how the module was linked:

| module | slots thunked | created | what was created |
|---|---:|---:|---|
| `vampire.dll` | 175,199 / 175,513 | 5,735 | `E9` stubs Ghidra had not modelled as thunks |
| `client.dll` | 96 / 95,714 | 559 | bodies with no function at all |
| `engine.dll` | — | 274 | mixed |
| `GameUI.dll` | — | 154 | mixed |
| `vguimatsurface.dll` | — | 51 | mixed |

**Nearly every `vampire.dll` slot goes through an incremental-link stub**, so creating at the
slot address makes a *thunk*, not a body — `100074d7` becomes `CAISound::thunk_FUN_10027450`,
two bytes of `JMP`. That is the fix for the trap above rather than new code: once the stub is a
modelled thunk, a call reaching it resolves to the body, and `getCallingFunctions` stops losing
the edge. `client.dll` links its slots directly, so its 559 really are bodies nothing else found.

Either way the result is marked `thunk` in the corpus and filters out of a function census.

### Query cost is setup cost, not query cost

A `vtmb_*` call averages **1.8 ms** over a warm server. It averaged 16 ms, and none of that was
the query — the four hot tools sat at ~22 ms each while the SQL underneath ran in 0.02 ms. Three
causes, all of them setup:

**`CREATE INDEX IF NOT EXISTS` matches on the index's NAME, not its definition.** Widening one in
place is a silent no-op: the old single-column index survives, every query still plans against
it, and nothing says so. Eleven indices here were redefined and none of them changed until
`_ensure_indices` compared the stored SQL and rebuilt the ones that differed. Same trap as
`CREATE TABLE IF NOT EXISTS` refusing to add a column, which `_migrate` handles.

**`functions`' PRIMARY KEY is `(module, addr)` and cannot serve a lookup that knows only the
address** — which is how every reference query begins. Without `functions_addr` SQLite scans all
71,235 rows, and each row carries its own decompilation, so the scan drags 162 MiB of TEXT past
the page cache. 45 ms, on the first step of `func`, `code`, `callers`, `callees`, `asm` and
`twin` alike.

**Never apply a function to the column you are indexing.** `WHERE addr = ? OR lower(addr) =
lower(?)` re-introduced the full scan even with the index present; normalise the argument in
Python instead. The same applies to `name = ? OR name LIKE '%x%'` — OR-ing a leading-wildcard
LIKE beside an equality denies the index to both halves, so try the exact match first and fall
back only if it misses.

Two more, cheaper: `_staleness` hashed all eight DLLs on **every** query (~80 ms), and is now
keyed on their size and mtime so a repeat pays nothing while any real change still re-hashes;
and both databases are opened once per process rather than per call, with `mmap_size` set so a
173 MiB file is mapped rather than copied page by page.

`grep`'s prefilter stays deliberately conservative: it only fires on a pattern with no
metacharacter at all. A literal lifted out of an alternation or an optional group is not
required to appear in a match, so prefiltering on one would silently drop hits — a regex scan
costs 99 ms, and a silent miss costs a wrong conclusion.

### A decompilation that is damaged says so

`corpus code` prints a `DAMAGED DECOMPILATION` banner carrying the decompiler's own warnings.
Two of them mean the C cannot be read as the function:

- `Removing unreachable block` — the C **is not the whole function**;
- `Could not recover jumptable` / `Treating indirect jump as call` — its **control flow is
  wrong**, a switch printed as a call.

`corpus asm <ref>` is the fallback, from `listing.sqlite` beside the corpus. The listing lives in
its own database because it is comparable in size to every decompiled function put together, and
every MCP query opens the corpus.

### The repair passes, and what each is actually worth

Measured on `vampire.dll`:

| pass | yield | reading |
|---|---|---|
| `signatures` | **2,201** functions corrected; `CBaseCombatCharacter::MeleeSwingStep` goes from **114 invented parameters to 4** | the large one. 7,024 functions state their argument bytes in their own `RET <imm16>`, which is exact, not inferred |
| `boundaries` | **0 of 16** oversized functions split | the outliers are *real*: `datamap_*_builder` static-init, and `FUN_104126e0` — 68 KB that turns out to be VtMB's complete `ACT_*` activity registry, every activity name paired with its ID |
| `jumptables` | **6** tables read from the image | small. 265 of the 605 computed jumps state no base at all — they are register-indirect tail calls, not switches, and are left alone |
| `thiscall` | **699** methods in `vampire.dll`, **1,379** in `engine.dll` | `engine.dll` had **2** before, so nearly every method there was decompiling its receiver as `in_ECX` |

The order matters: boundaries first because every later pass reads function bodies; `thiscall`
before `signatures`, because the convention decides whether the receiver is a stack parameter at
all and the count is read against it; signatures last because a split function needs one too.
`repair all` runs them in that order.

**`thiscall` demands two independent facts and refuses on one.** A function gets `__thiscall`
only when it is a method — in a class namespace, or held by some class vftable — *and* it reads
ECX before writing it. The second is the same fact the decompiler reports when it invents
`in_ECX`. Either alone is not evidence: a method that never touches ECX in the window may take
no receiver on that path, and a non-method that reads ECX first is reading whatever its caller
left there. A wrong convention shifts every parameter, which is worse than no convention.

**A pass that finds no evidence changes nothing and says so.** `SplitFuncs` will not invent a
boundary, `RecoverJumpTables` will not invent a table, and `RecoverSignatures` leaves a function
alone when neither the `RET` nor the call sites state an argument count — 15,568 of them in
`vampire.dll`. An unknown prototype is better than a fabricated one, which is the whole point of
the signature pass.

### `grep`'s index has to agree with `grep` about what a match is

`grep` searches for **substrings**, so `/Melee/` must find `CBaseCombatCharacter::MeleeSwingUpdate`.
An FTS5 index built with a word tokenizer does not: it holds that identifier as one token, and
`MATCH "Melee"` returns nothing for it. The prefilter then narrows the scan to a set that does
not contain the hits, and `grep` reports a number that is simply wrong — silently, and with the
confident shape of an answer:

| pattern | with a word tokenizer | truth |
|---|---:|---:|
| `/Melee/` | 152 | **1,258** |
| `/Disciplin/` | 6 | **451** |
| `/NextAttack/` | 0 | **52** |
| `/haracter/` | 0 | **1,634** |

`code_fts` is therefore built `tokenize='trigram'`, which indexes every 3-character window and
*is* substring semantics, case-insensitively — the same as `grep`'s own `re.IGNORECASE`. It costs
roughly 3.5× the code size on disk (the corpus goes 173 → 275 MiB) and that is the price of an
answer that is not silently short.

Two rules follow, and both are in `_prefilter`: the index is only consulted for a pattern with
**no metacharacter at all** (a literal lifted out of an alternation is not required to appear in
a match), and only for a literal of **three characters or more** (a trigram index cannot answer a
query shorter than one trigram, and returns nothing rather than everything). `command_grep` also
reads `code_fts`'s stored definition before trusting it, because a corpus built by the old
tokenizer still answers `MATCH` — just wrongly.

### A search term is not a LIKE pattern

`corpus func %` used to answer with twenty arbitrary functions, because the reference was
interpolated straight into `'%' || ? || '%'` and `%` is the wildcard. `_contains()` escapes `%`,
`_` and `\`, and every caller pairs it with `ESCAPE '\'`. The same shape was in `str` and
`globals`.

### An imported function is a call edge that joins to nothing

Ghidra files a call into another DLL under a synthetic `EXTERNAL:0000001f` address, and no other
table in the corpus holds a row there — so `callees`' join dropped **7,131** edges across the
eight modules, every call into the CRT, Win32, `TIER0`, or `VAMPIRE_PYTHON21`. `DumpExternals`
reads the external function list alone (seconds, no decompilation) and `callees` names them in
their own section. Before it, `PyServerSystem::vfunc0` read as "3 callees, one of them `_strstr`";
after, it reads as `Py_InitModule4`, `PyCFunction_New`, `PyClass_New`, `PyModule_AddObject` — the
Python bridge's module registration.

### The embedded interpreter is CPython 2.1.2, and the source is an exact oracle

`vampire_python21.dll` states `2.1.2` in its own data, so one released source tree accounts for
it. `ApplyPythonApi` applies that tree's declared prototypes to the imports, which is what makes
`PyArg_ParseTuple`'s format string readable — and the format string is the argument list of a
script-API function. Facts belong in `docs/vtmb/python_bridge.md` and `docs/vtmb/script_api.md`;
the mechanism is that a `char *` parameter plus a string shorter than the analyzer's five-character
minimum is invisible until *both* are fixed, which is why `ApplyPythonApi` does the two together.

Unpack the tree under `$ELYSIUM_WORK_ROOT/research/reference-source/Python-2.1.2/`. It is
third-party reference source and is never committed.

### Names come from the strings the compiler left

A VProf scope pushed at a function's entry is that function's name, an input handler opens by
pushing `C<Class>::Input<Name>`, and an assert carries the source file it was written in.
`NameFromStrings` attributes each such string to the function that references it and renames on
the strong tier only — a qualified name referenced within `entry=` bytes of the entry point.

**The window has to clear the prologue.** A marker referenced at `+0x41` is ordinary: the frame
is set up before the scope is pushed. The default is 128 bytes, and the deltas cluster there;
past ~256 the references are overwhelmingly inlined callees naming themselves.

**A name claimed by many functions is an inlined helper, not a collision.** `CBaseEntity::SetSolid`
is claimed by 71 functions in `vampire.dll` because its scope marker rides into everything that
inlined it. Those are reported with the claim count and never written.

Ownership is the plate comment: a renamed function carries a `NameFromStrings:` line naming the
string and its address, which is both the evidence and the marker `clear` uses to revert.
Attribution is `getFunctionContaining`, so disassembled-but-unowned code contributes nothing —
the survey reports that count per module, and `MakeFuncs` is the answer when it is non-zero.

### A datamap is only half in the image

**VtMB's `typedescription_t` states no byte width** — its `+0x24` is zero on every record — so
`ApplyDatamapTypes` derives the width per `fieldType` from the corpus itself: sort a class's
records by offset, and an adjacent pair gives `(next.offset - offset) / count`.

**Take the smallest supported width, not the modal one.** Padding can only enlarge the gap to
the next field, so a one-byte bool followed by three bytes of alignment votes "4" as loudly as a
real int does, and a plurality types every bool in the module four bytes wide.

**Records past the statically initialized prefix read back zero.** One index of every
`mov [<abs>],<imm>` in the module's code resolves both a map's own `dataDesc`/`dataNumFields`
and the individual records' names — `CBaseCombatCharacter` reads 46 records from the image and
has 282. A record whose name is stored from a register cannot be recovered this way and is
counted, not skipped silently.

**A class structure must live in the ROOT category.** Making a namespace a `GhidraClass` mints
an empty same-named structure there, and a structure filed in a category of its own loses to
that placeholder: `this` then prints as a one-byte type (`this[0xaa1]`) while the real
6,576-byte structure sits unused.

**A vftable slot holds a 5-byte `E9` thunk that Ghidra often does not model as a thunk**, so the
jump has to be followed from the bytes as well as through `getThunkedFunction`. Bound the walk
at the next `vftable_*` label: stopping at the first slot that is not a `Function` truncates any
table with a gap — the player's busy predicate is slot 412 — and not bounding it at all makes
the walk run into the next class's table and claim its methods.

### The ledger has two tiers, and the second one is candidates

`corpus readers <offset>` answers "who touches this". A typed method states its members by name,
so those rows carry a class and a field. An untyped one states an offset against a pointer the
decompiler named itself, and that row carries the offset alone.

Keeping both is what makes the question answerable across a whole module rather than across the
typed part of it — but the untyped tier is candidates, not consumers, because unrelated
structures collide numerically. A query for a sequence descriptor's `+0x2F8` returns the
`rules.txt` parser too, which writes its own record at the same displacement.

### Two headless traps in these passes

- **`ParallelDecompiler` does not return results in the order the functions were handed to it.**
  Pairing by index files every function's body under a neighbour's name, and nothing complains.
  Return the result's own `getFunction()` alongside its C and pair on that.
- **Headless splits a `key=value` script argument on the `=` before the script sees it**, so a
  script that only parses the joined form reads `null` for every argument. Accept both.

## Disassembled ≠ owned (the plugin DLLs)

`stdshader_dx8.dll` and the other plugin DLLs reach every subsystem through interface
vtables, so the analyzers disassemble most of `.text` but wrap almost none of it in a
`Function`. Everything that walks the FunctionManager then reports the code as absent:
`DumpGrep`'s `getFunctionContaining` returns `<none>` for a string's referencing site,
and `DumpFuncs -ScriptArgs "range=…"` finds a handful of stubs in a region full of code.
`EnableAIF` does not fix this — the bytes were already disassembled; only ownership is
missing.

```powershell
research/tooling/ghidra/driver/run.ps1 -Program stdshader_dx8.dll -Script MakeFuncs   # 525 functions created
```

Run it once per program (headless saves the result), then the ordinary recon works. Two
further notes from the RE-A9 session:

- A string's *address* is not its function's entry point. `MOV EAX,[0x100232a8]; RET`
  starts at the `A1` opcode, one byte before the operand a raw byte-search reports —
  searching for the operand address finds no vtable slot, searching for the entry point
  finds it immediately.
- Shader classes store their name/help/fallback as `.data` pointer slots read by
  one-instruction accessors, so `GetName` is found through the **vtable**, not through an
  xref on the string.

## Decompiler caveats (engine.dll)

- Functions with 10 cdecl args and reused parameter stack slots (the particle
  track parser at `0x200c9460`) defeat the decompiler's argument recovery: it
  attributes a call's pushed args to a neighbouring vtable call. Read the call
  site's raw listing (`DumpAsm`) to recover the real signature — the `ADD ESP,<n>`
  after the CALL gives the arg count.
- The same function's decompilation drops ~60 blocks as *"Removing unreachable
  block"* — that output is **not** the whole function. `0x200c97a5–0x200c99f5`
  (the keyframe time-fixup pass) exists only in the listing.

## Key addresses (GameUI.dll, imagebase 0x10000000)

- `0x10003ef0` — `CBasePanel` ctor / `CreateGameMenu` (loads `Resource/GameMenu.res`,
  hardcoded item fallback). Sets CBasePanel vftable `0x1004ff3c`.
- `0x10004250` — `RunMenuCommand` (dispatch: `OpenNewGameDialog` → …).
- `0x10033910` — `CGameMenu` ctor (the `operator_new(0x90)` menu container).
- `0x1004ff3c` — `CBasePanel` vftable (PerformLayout / OnThink / PaintBackground live here).

## Key addresses (client.dll chargen, imagebase 0x10000000)

- `0x10173600` — `chooseteam`: opens `CharEditPanel` in mode 2.
- `0x10173620` — `questlog`: opens the same panel in mode 0.
- `0x10173640` — `createplayer`: grants `giftxp 9000` and opens the panel in mode 1.
- `0x10173670` — the `createplayer` ConCommand registration block.
- `0x10173750` — the panel-mode dispatch that selects the corresponding host treatment.
- `0x101740e0` — `CharEditPanel` close/hide; a non-zero mode runs `v_unpause`, then
  `teleport_player firetrans`.

The mode field is the int at `CharEditPanel +0x274`: 0 quest log, 1 character creation, 2 team
selection. The local byte gating `0x101740e0`'s tail is not yet identified.

## The script API workspace (roadmap 9.7)

The script→engine surface is recovered with two scripts and written up in
`docs/vtmb/script_api.md`. The six `PyMethodDef` tables in `vampire.dll`:

| Table | Entries | What |
|---|---|---|
| `1058f7a8` | 11 | module `vampire` globals |
| `1058f868` | 24 | Character (player + NPC) |
| `1058f698` | 13 | `Entity` base methods (12 distinct — `GetCenter` twice) |
| `1058f778` | 2 | `Entity.__getattr__` / `__setattr__` |
| `1058f5d0` | 2 | `G` — `ClearAll`, `keys` |
| `1058f620` | 2 | the `IRestore` buffer adapter — `read`, `readline` |

```powershell
research/tooling/ghidra/driver/run.ps1 -Program vampire.dll -Script DumpPyMethods `
  -ScriptArgs "table=1058f868 depth=1 out=…/py_character.txt"
```

## The choreo workspace (roadmap RE19)

`logic_choreographed_scene` / `CSceneEntity` / `CChoreoEvent`, all in `vampire.dll`. Every
address is tabulated in `docs/vtmb/choreographed_scenes.md` → "Provenance"; the entry points to
re-derive from are the datamap builder `FUN_10080420` (records `0x105481c4`, 34) and the
dispatch `FUN_10082ee0` with its jump table at `0x10083500`. Two workflow notes from that
pass:

- **A `switch`'s jump table beats the decompiler.** `DispatchStartEvent`'s C output merges
  three enum cases into one arm and silently omits a fourth; reading the 19 dwords at
  `0x10083500` shows `CAMERASHOT` pointing at the *unknown-type* branch — the decompile
  never says so.
- **Reading a vtable slot from the PE is faster than decompiling it.** A `.text` slot is
  usually a 5-byte `E9` thunk; following it (and scanning the whole `.text` for thunks that
  target a given body) identifies which class overrides which slot — that is how
  `CInstancedSceneEntity` (vftable `0x1044f584`) was found to override only the playback
  think `+0x218`.

**Datamaps need both readers.** `DumpDatamap` reads the image and sees only the statically
initialized leading records; everything the per-class builder assigns at static-init reads back as
zero, so `CBaseCombatCharacter` reports `INPUTS (0)` for a class carrying 25. Decompile the
builder, then replay it:

```powershell
research/tooling/ghidra/driver/run.ps1 -Program vampire.dll -Script DumpFuncs `
  -ScriptArgs "funcs=1031a600 depth=0 out=…/bcc_builder.txt"
uv run elysium research parse_datamap_builder …/bcc_builder.txt --recs 10616694 --count 305
```

The builder's tail names both arguments — `_DAT_<map+4> = <count>; _DAT_<map> = &DAT_<recs>;`

**When the builder is split across static-init fragments, read the PE instead.** MSVC emits some
per-class datamap builders as several small static-init functions, and the decompiler then returns
only the fragment you asked for — `CBaseToggle`'s builder decompiled to 3 of its 26 dynamically
assigned records. Reconstructing from the image beats both readers: walk the sections, scan for
`typedescription_t` arrays at stride `0x2C`, then merge in every `mov [abs], imm` in `.text` whose
target lands inside a record slot. That recovers builder-assigned records without a decompiler pass
and without spending a headless run. Reading a datamap, a record array or a vtable slot straight out
of the PE is generally faster than decompiling it — see also the `clc_*` handler table and the
`CInstancedSceneEntity` vtable note above.

**Parallel sessions need one project copy each.** The lock warning above is not just about pacing:
two agents working at once will collide. Copy the analyzed project directory per workstream
(`-ProjDir <copy> -ProjName vtmb`) — a full copy of the eight-program `vtmb` project is ~535 MB and
costs no re-analysis..

**Finding a builder or a handler:** grep an input name. The string table carries both the external
name and the `Input<Name>` internal name, and the referencing function is the builder. Handlers
open by pushing a profiler marker `C<Class>::Input<Name>`, so `-Script DumpGrep -ScriptArgs
"str=::Input"` maps handler names to addresses in one run.

Recovered datamaps: `CBaseCombatCharacter` `1061664c` (builder `FUN_1031a600`, recs `10616694`,
305) · `CAI_BaseNPC` `105c9814` (`FUN_1027a820`, recs `105c9874`, 102) · the player class
`10580edc` (`FUN_1015af10`, recs `10580f24`, 157).
