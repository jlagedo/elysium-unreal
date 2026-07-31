# Ghidra RE workspace (VtMB menu / GameUI)

Reverse-engineers the parts of the VtMB menu that are **not** in any data file —
the main-menu item geometry (positions, spacing, slide/fade animation) and the
in-game pause item set, compiled into `GameUI.dll` (`CBasePanel`, `CGameMenuButton`).
See `docs/project/rebuild-strategy.md` → *Menu — full-fidelity spec (M0)*, section E.

## What's here

| Path | Tracked | What |
|---|---|---|
| `DumpMenu.java`  | ✅ | recon: dumps RTTI classes, menu-related strings + xrefs, decompiles seed functions |
| `DumpGrep.java`  | ✅ | parameterized recon: strings/classes/function names by regex → decompile the matches |
| `DumpFuncs.java` | ✅ | targeted decompiler: follows seed functions (+callees) and vftables → C pseudocode |
| `DumpAsm.java`   | ✅ | raw disassembly of a function, or a flat run at any address (disassembles on demand) |
| `DumpXrefs.java` | ✅ | every reference to an address + the function containing each referencing site |
| `DumpConst.java` | ✅ | the dword at an address as hex/int/float (the decompiler renders these as `_DAT_`) |
| `DumpConVars.java` | ✅ | the ConVar/ConCommand registration table: object address ↔ name ↔ default, for one ctor |
| `MakeFuncs.java` | ✅ | promotes disassembled-but-unowned code into functions (CALL targets + post-padding starts) |
| `DumpPyMethods.java` | ✅ | a CPython `PyMethodDef` table: name, `ml_doc`, thunk→body, then every body decompiled |
| `parse_datamap_builder.py` | ✅ | reconstructs a `datamap_t` from its **decompiled builder** (the half an image read misses) |
| `run.ps1`        | ✅ | headless runner (import + post-script) |
| `project/`       | ❌ gitignored | the analyzed Ghidra project DB (derived from the user's own binary) |
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

The builder's tail names both arguments — `_DAT_<map+4> = <count>; _DAT_<map> = &DAT_<recs>;`.

**Finding a builder or a handler:** grep an input name. The string table carries both the external
name and the `Input<Name>` internal name, and the referencing function is the builder. Handlers
open by pushing a profiler marker `C<Class>::Input<Name>`, so `-Script DumpGrep -ScriptArgs
"str=::Input"` maps handler names to addresses in one run.

Recovered datamaps: `CBaseCombatCharacter` `1061664c` (builder `FUN_1031a600`, recs `10616694`,
305) · `CAI_BaseNPC` `105c9814` (`FUN_1027a820`, recs `105c9874`, 102) · the player class
`10580edc` (`FUN_1015af10`, recs `10580f24`, 157).
