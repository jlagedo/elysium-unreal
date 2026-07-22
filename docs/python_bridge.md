# VtMB game logic — the Python 2.1 bridge

Authoritative map of how VtMB embeds Python and binds it to the engine: what runs the
scripts, where the scripts live, and how a script call reaches a C++ object.

**Every claim here is verified against the user's own install** — PE import/export
tables, a Python 2.1 marshal reader run over the shipped `.pyc`, entity data read via
`tools/bsp.py`/`tools/vpk.py`, and Ghidra decompilation of `vampire.dll`. Addresses are
for the retail binaries (`vampire.dll` imagebase `0x10000000`, `engine.dll`
`0x20000000`, `vampire_python21.dll` `0x1e100000`).

The scripting is **100% readable data** — plain-text Python, plain-text `.dlg`, BSP
keyvalues. Nothing is compiled. The *systems the scripts call* are 100% compiled, and
live in `vampire.dll`.

## The three binaries

| Binary | Python symbols | Role |
|---|---|---|
| `Bin/vampire_python21.dll` | **exports 653** | The VM. Stock CPython 2.1. |
| `Bin/engine.dll` | imports 30, **5 unique** | Boots and owns the VM. Knows nothing about entities. |
| `Vampire/dlls/vampire.dll` | imports 69, **44 unique** | **Owns the entire script API.** |

`vampire_python21.dll` is unmodified CPython 2.1 — its 32 non-`Py*` exports are all
stock built-in module inits (`initmath`, `initcPickle`, `initnt`, `initthread`, …).
Troika added exactly **three** symbols:

| Export | RVA | Status |
|---|---|---|
| `Py_SetGameInterface` | `0x02bed0` | **Dead.** The body is one byte: `C3` (`RET`), then 15× `90` padding. `engine.dll` calls it; it discards its argument. |
| `PyRun_ConsoleString` | `0x053160` | Real — the dev-console eval path. |
| `Py_FlushConsoleOutput` | `0x02bee0` | Real. |

`engine.dll`'s 5 unique symbols are `Py_Initialize`, `Py_Finalize`, `Py_SetProgramName`,
`Py_SetGameInterface`, `PyType_Type`. It sets the interpreter search path to
`\vampire\python` and can run a string. **The name `Py_SetGameInterface` is a decoy** —
no interface is passed anywhere. All binding happens in `vampire.dll`.

## Where the Python lives — four surfaces, two languages

| Surface | Location | Volume | Language |
|---|---|---|---|
| Level scripts | `Vampire/python/**/*.py` (loose) | 27 files, 8,954 lines, **690 functions** | Python 2.1 |
| Dialogue | `dlg/*.dlg` inside `pack*.vpk` | 138 files, 49,359 rows → **8,355 conditions + 2,988 actions** | `dlgexpr` (not Python) |
| Entity outputs | `maps/*.bsp` ENTITIES, output **field 6** | **1,621** calls across 101 maps | Python expression |
| Conditional entities | `maps/*.bsp`, `logic_pythoncheck` | **51** | Python expression |
| *(compiled duplicates)* | `python/*.pyc` inside the VPKs | 24 | **dead — never loaded** |

The Unofficial Patch shadows all of it (loose search paths resolve before the VPKs, per
`## Asset resolution` in CLAUDE.md): **21 of the 26 shared scripts differ**, plus 9
patch-only `.py` and 6 retail-only; it shadows all 138 `.dlg` and adds 9.

### The loose `.py` is the source; the VPK `.pyc` is a fossil

Verified by unmarshalling all 24 `.pyc` (magic word **60202** = Python 2.1, confirming
the VM version from the bytecode itself) and comparing against the loose tree:

- **21 of 22** comparable scripts have an **exact** top-level `def`/`class` name-set match.
- **633 functions checked; 594 (93.8%) sit at byte-identical line numbers.**
- Every discrepancy is a **later official patch to the `.py` without recompiling the `.pyc`**:
  `vamputil.py` is bimodal (21 functions at delta 0, 25 at delta **+10** — one 10-line
  insertion); `warrens.py` gains `hw_warrens_5_patch` and `patchMitnick`. **Zero functions
  exist in the bytecode but not the source.**
- `giovanni.py` defines `cutscene()` **twice** (lines 118 and 488); the second shadows the
  first. Latent bug, not drift.

CPython 2.1 predates `zipimport` (2.3) and cannot read a VPK, so the interpreter only ever
sees the real filesystem. **The loose tree is what executes.**

The `.pyc` carry Troika's build path — `J:/Remaster/Vampire_v409_041008_LOCS/…` (v4.09,
dated 2004-10-08). Their only remaining value is provenance and as a cross-check oracle.

## Binding — module, classes, method tables

`vampire.dll` registers one C extension module named **`vampire`** via `Py_InitModule4`
(module init at `0x10199f70`; the call at `0x10199fbe` pushes `0x3f2` = **1010** =
`PYTHON_API_VERSION` for Python 2.1). It then builds **old-style classes** with
`PyClass_New`.

`PyMethodDef` is the stock 16-byte record (`ml_name`, `ml_meth`, `ml_flags`, `ml_doc`),
NULL-terminated. Every `ml_meth` points into an **MSVC incremental-link thunk table** at
`0x1000xxxx` — each entry is a `JMP` to the real body. Follow the jump.

| Table VA | Entries | What |
|---|---|---|
| `0x1058f7a8` | 11 | module `vampire` — global functions |
| `0x1058f778` | 2 | `Entity.__getattr__`, `Entity.__setattr__` |
| `0x1058f698` | 13 | `Entity` base methods (`__init__`, `GetOrigin`/`SetOrigin`, `GetAngles`/`SetAngles`, `GetCenter`, `GetAngleVectors`, `Get`/`SetModelName`, `SetModel`, `Get`/`SetName`, `IsAlive`) |
| `0x1058f868` | 24 | Character (player + NPC) |
| `0x1058f5d0` | 2 | `G` — `ClearAll`, `keys` |
| `0x1058f620` | 2 | file-like — `read`, `readline` |

**Total: 54 table entries. Scripts call 109 methods.** The other ~55 are not bound
anywhere — see the next section.

**Module `vampire` globals (11):** `FindPlayer`, `FindEntityByName`, `FindEntitiesByName`,
`FindEntitiesByClass`, `ScheduleTask`, `SquadSeesPlayer`, `CreateEntityNoSpawn`,
`CallEntitySpawn`, `ChangeMap`, `OneOfSet`, `IsPCMalk`. (`SquadSeesPlayer` is called by no
shipped script — the tables include API the content never used.)

**Character (24):** `React`, `SetExpression`, `SetDisposition`, `SetGesture`, `HasItem`,
`GiveItem`, `RemoveItem`, `AmmoCount`, `GiveAmmo`, `HasWeaponEquipped`, `StartBarter`,
`WorldMap`, `SewerMap`, `SetQuest`, `CurrentMoney`, `IsMale`, `SeductiveFeed`, `SetCamera`,
`CalcFeat`, `DialogDiscipline`, `BumpStat`, `GetMasqueradeLevel`, `GetQuestState`,
`IsFollowerOf`.

Several `ml_doc` strings are copy-paste errors (`CurrentMoney` carries `HasItem`'s text;
`IsFollowerOf` carries `GetQuestState`'s) — useful as a build fingerprint.

## Dispatch — `Entity.__getattr__` is a datamap lookup

`__getattr__` thunk `0x1000a443` → body **`0x10195510`**. `__setattr__` thunk `0x10013c3c`.

An **entity is a C++ pointer boxed in a `PyCObject`** tagged with the descriptor string
`_entity_ptr_`, held on an old-style class instance. `__getattr__` does:

1. `PyArg_ParseTuple` the (self, name) pair; bad args → `AttributeError`.
2. Unwrap `_entity_ptr_` → `CBaseEntity*`. Null → `AttributeError` ("game entity has been
   deleted"). This is why `if(ent):` guards work.
3. **Look the name up in the entity's Source datamap** (helper `FUN_10195940`).
4. If the found field carries an **`inputFunc`** (`typedescription_t + 0x1C`, tested as
   `field[7]`): box `(entity, field)` in a fresh `PyCObject` and return
   `PyCFunction_New` over the generic `entity_input_function` def (`PTR` at `0x1058f658`)
   — i.e. **manufacture a bound callable on the spot**.
5. Otherwise **marshal the field's value** by `fieldType` (see the enum below).

The lookup (`FUN_10195940`) is a straight Source datadesc traversal:

```c
for (map = entity->GetDataDescMap();     // virtual call, vtable +0x148
     map != NULL;
     map = map->baseMap)                 // +0xC — walks UP the class chain
{
    for (i = 0; i < map->dataNumFields; i++) {          // +0x4
        field = map->dataDesc + 0x2c * i;               // +0x0, stride 44
        if (strcmp(field->externalName, name) == 0) {   // +0x14
            *out_field = field;
            *out_addr  = (char *)entity + field->fieldOffset[0];   // +0x8
            return 1;
        }
    }
}
return 0;
```

**Consequences.**

- A Python attribute name and a Hammer keyvalue/input name are **the same namespace**.
  `ScriptHide`, `Trigger`, `Kill`, `Enable`, `Unlock`, `BeginSequence` are not bespoke
  Python methods — they are **entity inputs**, resolved by the same name lookup that
  entity I/O uses. That is why they appear in no method table.
- Data attributes (`pc.clan`, `npc.times_talked`, `monitor.skin`) are **datamap fields**,
  read/written directly at `entity + fieldOffset`.
- Script-set names that miss the datamap (`ent.beam1`, `ent.track`, `ent.numBeams`) fall
  through to the old-style instance `__dict__`, so entities behave as property bags.
- `cop.Kill` written **without parentheses** (present in retail) manufactures a callable
  and discards it — the input never fires.

## The write path — `Entity.__setattr__`

Thunk `0x10013c3c` → body **`0x10195a10`**, the mirror of `__getattr__`. It runs the **same
datamap walk** (`FUN_10195940`) and then:

1. `_entity_ptr_` is **read-only** — assigning it raises `AttributeError` ("%s is read only").
2. A name that **misses the datamap falls through to the instance `__dict__`**
   (`PyDict_SetItemString`) — entities are property bags on the write side too, symmetric with
   the read fall-through.
3. A datamap field is writable only if its **`flags` byte (`+0x12`) has bit `0x8` set**; a
   non-keyable field raises the same read-only error.
4. The value is **marshalled into `entity + fieldOffset` by `fieldType`**, using the same shifted
   enum: `1`/`15` float (`FLOAT`/`TIME`), `2`/`16`/`17` string (interned through the engine string
   pool), `3`/`14` 3-vector, `4` integer, `5` boolean (`PyObject_IsTrue`), `8` `COLOR32` (4-tuple
   of bytes), `11`/`12`/`13` entity-handle variants; any other code raises
   `TypeError` ("%s.%s has unhandled type %d").

So `pc.clan = 3` and the I/O wire that writes a keyfield are one path, exactly as the read side
unifies attribute reads with input dispatch.

## Divergence — VtMB's `fieldtype_t` is not modern Source's

The marshalling switch keys off `fieldtype_t`. **Modern `datamap.h` codes do not apply.**
VtMB's enum lacks `FIELD_QUATERNION` (modern index 4) and `FIELD_TICK` (modern 17), both
added to Source after VtMB forked, so **every code from 4 upward is shifted**. All nine
observed switch cases fit the reduced enum exactly:

| Code | VtMB | Modern `datamap.h` | Marshals to |
|---|---|---|---|
| 1 | `FIELD_FLOAT` | `FIELD_FLOAT` | `PyFloat` |
| 2 | `FIELD_STRING` | `FIELD_STRING` | `PyString` |
| 3 | `FIELD_VECTOR` | `FIELD_VECTOR` | 3-tuple of `PyFloat` |
| 4 | `FIELD_INTEGER` | `FIELD_QUATERNION` | `PyInt` |
| 5 | `FIELD_BOOLEAN` | `FIELD_INTEGER` | `_Py_TrueStruct` / `_Py_ZeroStruct` |
| 14 | `FIELD_POSITION_VECTOR` | `FIELD_EDICT` | 3-tuple of `PyFloat` |
| 15 | `FIELD_TIME` | `FIELD_POSITION_VECTOR` | `PyFloat` |
| 16 | `FIELD_MODELNAME` | `FIELD_TIME` | `PyString` |
| 17 | `FIELD_SOUNDNAME` | `FIELD_TICK` | `PyString` |

Full VtMB order: `VOID, FLOAT, STRING, VECTOR, INTEGER, BOOLEAN, SHORT, CHARACTER,
COLOR32, EMBEDDED, CUSTOM, CLASSPTR, EHANDLE, EDICT, POSITION_VECTOR, TIME, MODELNAME,
SOUNDNAME, INPUT, FUNCTION, …`

**`typedescription_t` is 44 bytes (`0x2c`) in VtMB, 52 in modern Source** — it lacks the
trailing `override_count` / `fieldTolerance`. The offsets that matter are unchanged:
`fieldType` +0, `fieldName` +4, `fieldOffset[2]` +8, `fieldSize` +0x10, `flags` +0x12,
`externalName` **+0x14**, `pSaveRestoreOps` +0x18, `inputFunc` **+0x1C**, `td` +0x20,
`fieldSizeInBytes` +0x24.

**`datamap_t`**: `dataDesc` +0, `dataNumFields` +4, `dataClassName` +8, `baseMap` **+0xC**.
`GetDataDescMap()` is the virtual at **vtable +0x148**.

Same class of trap as the 104-byte `dface_t` and the 176-byte `DDispInfo`: reading VtMB
with a modern header silently corrupts every typed field.

## `__main__` is the bus

There is no `import vampire` in any script. Everything is star-imported into `__main__`
and scripts reach back into it (`import __main__`, `from __main__ import G`,
`from __main__ import *` in 14 files). `__main__` is bidirectional — scripts write to it
(`__main__.Level = __name__`, and `oceanhouse.py` stashes entity refs as
`__main__.HW`/`LS`/`Shake_HC`/…).

That stashing exists because **`ScheduleTask(delay, "<python source>")` defers a source
string** (41 calls) that is evaluated later against `__main__`. A live `__main__`
dictionary plus a runtime evaluator are therefore mandatory — a pure ahead-of-time
translation cannot serve this.

Module-level code in every level script is only aliasing plus a `print`; there are no
import-time side effects to emulate.

## The four call paths

| Trigger | Mechanism |
|---|---|
| Level-script callback | `PyRun_String` / `PyObject_CallFunction`. **`vampire.dll`** holds the format string **`__main__.%s`** (`0x1055e370`, in the `CEventQueue` dispatch region) — output field 6's `journalPickup()` becomes `__main__.journalPickup()`. (`engine.dll` owns only the VM boot.) |
| Entity output field 6 | Source's output format is extended from 5 comma fields to **7**; field 6 is a Python call string. 1,621 of 16,214 outputs (10%) carry one; 1,528 fire *only* Python with no I/O target; 93 do both. |
| `logic_pythoncheck` | `python_script` keyvalue is an expression → `PyRun_String` → `PyObject_IsTrue` → standard `OnTrue`/`OnFalse` I/O. |
| Dialogue | `.dlg` field 4 (condition, eval) and field 5 (action, exec). |

`worldspawn` carries a `levelscript` keyvalue (92 of 101 maps) naming the hub module —
`"levelscript" "chinatown"` loads `python/chinatown/chinatown.py`. Several maps share one
module.

**Error handling — error-to-false, confirmed.** The `logic_pythoncheck` evaluator
(`FUN_10135290`) runs `PyRun_String(python_script, Py_eval_input, __main__, __main__)` with the
result **seeded to `0`**; on a raise it calls `PyErr_Print()` (dumps the traceback to console) and
returns `local_4 != 0` = **false**, and a non-`int` result is false as well. The exec/call paths
swallow the same way — `FUN_100ce8a0` (exec a source string; `ScheduleTask` / level callbacks) and
`FUN_100ce990` (the `"__main__.%s"` field-6 call, format string `0x1055e370`) both `PyErr_Print()`
and continue, never aborting the map. The 89 malformed `.dlg` snippets (smart quotes, unbalanced
parens, legacy `<>`, `=` in a condition) therefore raise, print, and evaluate **false**, silently
hiding those lines in retail — the same engine and the same `PyRun_String` eval, so the port
reproduces error-to-false rather than repairing the snippets. The Unofficial Patch fixes most.

## `G` — the global flag bag

`G` is engine-owned (a C type with `ClearAll` and `keys`), injected into `__main__`, and
**never constructed in Python**. It is one flat, int-valued namespace: **345 distinct
flags** referenced by retail scripts (549 with the patch), **~900** by dialogue.

**208 of the 345 flags the scripts read are never assigned in Python** — dialogue writes
them. Level scripts and dialogue are not separable systems.

Shapes are overwhelmingly small ints (`G.x = G.x + 1`); the only exceptions across the
whole retail corpus are 3 lists, 1 dict (`G.morgue`, only `[name]=1` / `has_key`), and 3
strings. Scripts never initialize `G.Story_State`, so **`G` must return a default (`0`)
for unset attributes rather than raise** — assumed, not yet confirmed in Ghidra, and
load-bearing for story branching.

`G` is the save unit: `vampire.dll` carries `CPython_SaveRestoreBlockHandler` and
`CPyObjStrSaveRestoreDataOps`, and the game ships `pickle.py` + `copy_reg.py` for it.

## The two languages

### Level scripts — a small Python 2.1 subset

27 files, 8,954 lines, 690 functions. Uses `if`/`elif`/`else` (936/242/143), `for` (132),
`while` (23), `and`/`or`/`not`, lists, `%` string formatting (67 — mostly building entity
names, `Find("cop_car_%i" % i)`).

**Zero** `try`/`except`/`raise`/`lambda`/`yield`/`global`/`exec`, zero list comprehensions,
**3 classes** (all trivial: `LaserRoomBeams`, `BladeMover` in `fusyndicate.py`,
`MuseumBeams` in `museum.py`). Imports only `__main__`, `random`, `time`, `string.atoi`.

Python **2**, not 3: 285 `print` statements, `has_key`. 3 files mix tabs and spaces
(`e3.py`, `oceanhouse.py`, patch `vamputil.py`).

`vamputil.py` (46 functions: `IsClan`, `IsDead`, `RandomLine`, `OneOfSet`, …) is exec'd
into `__main__` and is **not** engine API.

### `.dlg` conditions/actions — `dlgexpr`, not Python

Field 4 may join an **engine skill-check** to a Python expression with `&`. The operator is
an implicit `>=` in 1,348 of 1,353 cases; the halves can swap order; skill-checks compose
with `and`/`or` and parens; 830 conditions are skill-only with no Python at all.

Across 49,359 rows: zero imports, lambdas, comprehensions, exceptions, control flow,
subscripts, slices, or container literals. Literals are `int` (12,278) and `str` (4,682)
plus **one float game-wide**. Actions are only assignment (2,762) and call (1,156),
separated by `;` (and `&` in 7 places).

```ebnf
condition  = skillcheck , [ ("&"|"|") , expr ] | expr , [ ("&"|"|") , skillcheck ] | expr ;
skillcheck = [ "(" ] , ident , [ relop ] , int , [ ")" ] | skillcheck , ("and"|"or") , skillcheck ;
action     = stmt , { (";"|"&") , stmt } ;
stmt       = target , "=" , expr | expr ;
target     = ident , "." , ident | ident ;
expr       = orexpr ;
orexpr     = andexpr , { "or" , andexpr } ;
andexpr    = notexpr , { "and" , notexpr } ;
notexpr    = [ "not" ] , cmp ;
cmp        = arith , { relop , arith } ;
relop      = "==" | "!=" | "<" | "<=" | ">" | ">=" ;
arith      = term , { ("+"|"-") , term } ;
term       = [ "-" ] , atom ;
atom       = int | string | postfix | "(" , expr , ")" ;
postfix    = ident , { "." , ident | "(" , [ args ] , ")" } ;
args       = expr , { "," , expr } ;
```

Skills seen: `Humanity` (432), `Persuasion`, `Seduction`, `Intimidate`, `Dominate`,
`Dementation`, `Haggle`, `F_Seduction`/`M_Seduction`, `Appearance`, `Firearms`, `Wits`,
`Intelligence`, `Research`, `Perception`, `Brawl`, `Trip_Name` — case is inconsistent.

## Implications for the rebuild

**There is no 109-method API to port.** The binding is a reflection layer over the
datadesc system. Register entity **inputs** and **keyfields** once (an `[EntityInput]` /
`[EntityKeyfield]` attribute feeding a name→delegate map in `Entities/`), and one lookup
serves all four triggers: the script call, output field 6, `logic_pythoncheck`, and the
plain Hammer I/O wire. A delegate replaces `entity + fieldOffset` cleanly — no `PyCObject`,
no raw offsets.

The genuinely bespoke surface is **11 globals, 24 Character methods, `G`, and the
field-type marshalling rules** (using VtMB's enum). Everything else rides the Source I/O
bus.

**Runtime choice is late and reversible** — an interpreter, IronPython, and a transpiler
all bind to the same API. Build the corpus and the bridge first.

**Scripts stay data; transforms belong in the pipeline.** Bring-your-own-game means a
hand-fixed fork of Troika's source only ever matches one install, and 21 of 26 shared
scripts already differ between the retail and patch trees. Any adaptation must be a
deterministic transform re-run over the user's own files.

**IronPython facts** (should the question return): IronPython **2.7 is Python 2.7** and
would accept VtMB's 2.1 syntax nearly as-is, but is dormant and .NET-Framework-era.
IronPython **3** is maintained and targets **.NET 8/10** (Godot's TFM) but is Python 3.4
and needs a 2to3 pass; it maps `int` to `BigInteger`, and the DLR's runtime codegen rules
out AOT/trimming. Neither touches the ~11,300 `dlgexpr` snippets, which need their own
parser regardless.

## Reproduction

No dependency beyond the repo's own readers plus Ghidra (`tools/ghidra/`, see its README).

- **PE import/export**: parse the tables directly (no `pefile` in this environment) to get
  the per-DLL symbol split and IAT slot addresses to xref.
- **`.pyc`**: Python 3's `marshal` **cannot** read 2.1 data. A ~100-line 2.1 marshal reader
  (types `0 N . i I f x l s t R u ( [ { c`; code object = `argcount, nlocals, stacksize,
  flags` as `short`, then `code, consts, names, varnames, freevars, cellvars, filename,
  name`, `firstlineno` as `short`, `lnotab`) walks every code object. Naive string-grepping
  a `.pyc` **does not work** — marshal type codes glue onto adjacent strings and fabricate
  identifiers (`apartmentLeave` + the next value's `s` tag reads as `apartmentLeaves`).
- **Method tables**: scan `.rdata`/`.data` on 4-byte alignment for 16-byte records whose
  name pointer resolves to an identifier in `.rdata` and whose `ml_meth` lands in `.text`.
- **Ghidra**: the `PyMethodDef` function pointers are thunks — decompile the `JMP` target,
  not the `0x1000xxxx` stub, or the script reports "no function at".

Useful IAT slots in `vampire.dll`: `Py_InitModule4` `0x109f370c`, `Py_FindMethod`
`0x109f3724`, `PyCFunction_New` `0x109f3788`, `PyRun_String` `0x109f3758`,
`PyObject_IsTrue` `0x109f3770`, `PyErr_Clear` `0x109f3768`.

## Open

- **`G` default-on-miss** — assumed `0`; not yet confirmed in Ghidra. Silently changes story
  branching. Seeds: the G console commands `FUN_1019aad0` (`gclearall`) / `FUN_1019ab40`
  (`CC_GClearAll`) reach the G global object — decompile its type's `tp_getattr` and confirm the
  miss path returns `PyInt_FromLong(0)`.
- **The `read`/`readline` table** (`0x1058f620`) — an unidentified file-like type.
