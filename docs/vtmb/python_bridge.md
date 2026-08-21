# VtMB game logic — the Python 2.1 bridge

Authoritative map of how VtMB embeds Python and binds it to the engine: what runs the
scripts, where the scripts live, and how a script call reaches a C++ object.

**Every claim here is verified against the user's own install** — PE import/export
tables, a Python 2.1 marshal reader run over the shipped `.pyc`, entity data read via
`pipeline/src/elysium_pipeline/formats/bsp.py`/`pipeline/src/elysium_pipeline/formats/vpk.py`, and Ghidra decompilation of `vampire.dll`. Addresses are
for the retail binaries (`vampire.dll` imagebase `0x10000000`, `engine.dll`
`0x20000000`, `vampire_python21.dll` `0x1e100000`).

The scripting is **100% readable data** — plain-text Python, plain-text `.dlg`, BSP
keyvalues. Nothing is compiled. The *systems the scripts call* are 100% compiled, and
live in `vampire.dll`.

## The three binaries

| Binary | Python symbols | Role |
|---|---|---|
| `Bin/vampire_python21.dll` | **exports 653** | The VM. CPython **2.1.2**, renamed, plus five symbols. |
| `Bin/engine.dll` | imports 30, **5 unique** | Boots and owns the VM. Knows nothing about entities. |
| `Vampire/dlls/vampire.dll` | imports 69, **44 unique** | **Owns the entire script API.** |

The DLL states its own version: the string `2.1.2` is in its data, and `Py_GetVersion`
returns it. That pins it to a single released source tree, which is what makes the
additions decidable — every export is looked for in the whole of `Python-2.1.2`
(`Include/`, `Python/`, `Objects/`, `Modules/`, `Parser/`, `PC/`), and **exactly five appear
nowhere in it**. A namespace test does not find them: four of the five begin with `Py`, and
`Py_SetGameInterface` looks like stock API until the source says otherwise.

| Export | RVA | Status |
|---|---|---|
| `Py_SetGameInterface` | `0x02bed0` | **Dead.** The body is one byte: `C3` (`RET`), then 15× `90` padding. `engine.dll` calls it; it discards its argument. |
| `PyRun_ConsoleString` | `0x053160` | Real — the dev-console eval path. |
| `Py_FlushConsoleOutput` | `0x02bee0` | Real. Called from five sites in `vampire.dll`, including `FUN_100ce990`, the `"__main__.%s"` field-6 dispatcher — so level-script output flushes through it. |
| `PyParser_ParseConsoleString` | — | The parser half of the console path. |
| `PyParser_SimpleParseConsoleString` | — | Likewise. Neither is called from `vampire.dll` or `engine.dll`; both exist for `PyRun_ConsoleString`. |

Nothing else diverges. The remaining 648 exports are stock, the 32 non-`Py*` ones being
built-in module inits (`initmath`, `initcPickle`, `initnt`, `initthread`, …). **The
interpreter itself carries no VtMB semantics**: the divergence is a console I/O path and one
dead stub, and everything a script can observe is bound in `vampire.dll`.

### Which build it is, and why every offset depends on it

Three of CPython 2.1's compile-time switches change **struct layout**, so reading any bridge
body means knowing which way each was set. Each is settled against the shipped DLL, not assumed:

| Switch | State | Evidence | Consequence |
|---|---|---|---|
| `Py_TRACE_REFS` | **off** | `#define`d only inside `#ifdef Py_DEBUG`, and a trace-refs build must export `_Py_NewReference`, `_Py_ForgetReference`, `_Py_Dealloc`. The DLL exports none. | The object header is the two-word form. **`PyObject` is 8 bytes, not 16** — with it on, every member offset in every object shifts by 8. |
| `COUNT_ALLOCS` | **off** | never `#define`d, and a counting build would export its counters. | `PyTypeObject` ends at `tp_weaklistoffset` and is **108 bytes**; `tp_alloc`, `tp_free`, `tp_maxalloc`, `tp_next` are not in the object. |
| `CACHE_HASH`, `INTERN_STRINGS` | **on** | `#define`d unconditionally at the top of `stringobject.h`. | `PyStringObject` carries `ob_shash` and `ob_sinterned` and is **21 bytes**. |

The layouts that follow, as `ApplyPythonApi` builds them into the Ghidra project:

| Struct | Size | Struct | Size |
|---|---:|---|---:|
| `PyObject` | 8 | `PyClassObject` | 32 |
| `PyVarObject` | 12 | `PyInstanceObject` | 20 |
| `PyTypeObject` | 108 | `PyMethodObject` | 24 |
| **`PyMethodDef`** | **16** | `PyStringObject` | 21 |
| `PyCFunctionObject` | 16 | `PyIntObject` | 12 |
| `PyTupleObject` | 16 | `PyFloatObject` | 16 |
| `PyListObject` | 16 | `PyFunctionObject` | 40 |
| `PyFrameObject` | 332 | `PyTryBlock` | 12 |

`PyMethodDef` at 16 bytes is the cross-check: it was hand-decoded from the binary long before
the source was on hand, and the two agree.

**2.7's headers would have been wrong here.** `PyTypeObject` has 50 members in 2.7 against 2.1's
27, because new-style classes landed in 2.2; `PyClassObject` gained `cl_weakreflist`. Only the
2.1.2 tree describes this DLL.

`engine.dll`'s 5 unique symbols are `Py_Initialize`, `Py_Finalize`, `Py_SetProgramName`,
`Py_SetGameInterface`, `PyType_Type`. It sets the interpreter search path to
`\vampire\python` and can run a string. **The name `Py_SetGameInterface` is a decoy** —
no interface is passed anywhere. All binding happens in `vampire.dll`.

## Where the Python lives — five surfaces, two languages

| Surface | Location | Volume | Language |
|---|---|---|---|
| Level scripts | `Vampire/python/**/*.py` (loose) | 27 files, 8,954 lines, **690 functions** | Python 2.1 |
| Dialogue | `dlg/*.dlg` inside `pack*.vpk` | 138 files, 49,359 rows → **8,355 conditions + 2,988 actions** | `dlgexpr` (not Python) |
| Entity outputs | `maps/*.bsp` ENTITIES, output **field 6** | **6,956** calls across the engine-loaded maps (1,591 retail) | Python expression |
| Conditional entities | `maps/*.bsp`, `logic_pythoncheck` | **51** | Python expression |
| **Console / cfg** | `cfg/*.cfg` aliases ↔ `__main__.ccmd` | the Basic/Plus switch + the movement aliases | console commands ↔ Python |
| *(compiled duplicates)* | `python/*.pyc` inside the VPKs | 24 | **dead — never loaded** |

**The console surface is bidirectional.** Scripts run console commands by *touching an attribute* on
the console object — `c = __main__.ccmd; c.patchtype = ""` executes the alias `patchtype` — and a
command the console cannot resolve **falls through to Python**. So a `.cfg` alias can name a Python
function and a Python function can trigger a `.cfg` alias.

**Reading an attribute executes it too, not only assigning one** *(inferred — see below)*. Field 6 is
wrapped as `__main__.%s` and evaluated, so genesis's `ccmd.createplayer` is a bare attribute **get**
with no assignment. `createplayer` is a real `client.dll` ConCommand (`docs/vtmb/game_runtime.md` →
"`createplayer` opens a panel") and that field-6 expression is its **only** invocation anywhere in the
shipped content — so a get must execute, or chargen could never open in the retail game. Across every
exported map, field 6 names `ccmd` exactly twice: `ccmd.createplayer` (genesis, once) and
`ccmd.wc_create` (9 maps, `logic_auto` `OnMapLoad`, delay 1.5, times 1); both read consistently under
get-executes.

*Confidence:* behavioural inference, not a decompile. `ccmd` is not a defined string in `vampire.dll`
or `client.dll` and `engine.dll` does not import into the Ghidra project, so the type object's
`tp_getattro` was not read directly. **What would verify it:** locating the `vampire` module's
`ccmd` type object (the module is built before `PyRun_SimpleString("from vampire import *")` in
`vampire.dll FUN_1019a490`) and decompiling its getattr slot.

The Unofficial Patch's entire Basic/Plus switch rides on this: its installer writes one of two
`cfg/user.cfg` files differing only in `alias patchtype "setBasic()"` vs `"setPlus()"`, and the one
shared script tree asks the console which install it is running under. Consequently **nothing in any
`.py`, `.ents`, `.dlg` or `.bsp` ever names `setPlus`/`setBasic`** — the sole reference in the whole
install is that `.cfg` line, so both functions read as dead code to any search of the script and map
trees. Ignition is `logic_auto.OnMapLoad -> unhidePlus()`, wired on **107 of 108 maps**, which
`ScheduleTask`s the `c.patchtype = ""` assignment. Port task: roadmap **9.3b** (+ **PL5d** for the
cfg copy).

**Elysium profile divergence — owner call:** the rebuild always runs the Unofficial Patch's Plus
profile. The imported patch-first maps, scripts, dialogue and data already establish that the patch
is active; `Patch_Plus` distinguishes its Basic and Plus profiles rather than patched and unpatched
content. The cfg mirror remains a verbatim record of the source install, but `FElysiumConsole`
replaces only its personal `patchtype` selector with `setPlus()` after parsing. `unhidePlus()` and
the patch's `setPlus()` body remain unchanged and own the profile's state and entity mutations.

The Plus initializer also exposes an engine contract which is otherwise easy to miss:
`setPlus() -> IsIdling()` indexes `FindEntitiesByClass("viewmodel")[3]` for a Tremere before it
tests `Patch_Plus`. The original runtime therefore has at least four live, script-addressable
`viewmodel` entities when map initialization runs. Elysium creates the same four ordinary substrate
entities with the player; first-person rendering remains owned by the viewmodel programme.

`sp_tutorial_1` makes the profile timing explicit **[data/native, RE47]**. The UP replacement BSP
first creates, keyvalues, spawns and activates its direct NPC population. A `logic_auto` then runs
`unhidePlus()`, which schedules the ordinary-vampire `patchtype` console assignment one game second
later; the installed alias finally enters `setPlus()`. `setPlus()` mutates already-live entities by
name (`plus_*` unhidden, `basic_*` hidden). It does not load the map's NPCs, and the opening Jack
entity matches neither wildcard. Its `IsIdling()` call initializes Patch Plus's **player** idle
monitor and proximity checks; it does not issue an NPC idle, facing, movement or dialogue command.
The separate core auto sets `Jack.WillTalk(0)` before the porch trigger later reenables talking.

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

Six such tables carry the whole bound surface: the module `vampire`'s global functions, the
`Entity.__getattr__`/`__setattr__` pair, the `Entity` base methods, the Character methods,
`G` (`ClearAll`, `keys` — no `has_key`, so `G.has_key` falls past `Py_FindMethod` into the
flag dict and reads integer `0`), and a two-entry file-like table (`read`, `readline`) that
is the `IRestore` buffer adapter (see below). **54 table entries total; the shipped scripts
call 109 methods** — the other ~55 are unbound, see the next section. The full per-name
inventory — every name, arity, doc string, call count, the `GetCenter` duplicate, and the
copy-paste `ml_doc` errors — lives in `docs/vtmb/script_api.md`, not repeated here.

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
- Step 4 runs **before** step 5 and the walk stops at the first name match, so an input name
  shadows any same-named value read: the expression yields a bound callable, not a number, and
  comparing it to an integer is a type comparison that cannot fail the way the author expects.
  `chooseSire()` (`vamputil.py`) reads `getattr(pc, "BloodHeal") != -1` to pick between two
  Malkavian sire models. `BloodHeal` is a `CBaseCombatCharacter` **input**
  (`docs/vtmb/script_api.md`, internal name `BloodHealIn`), not the Blood Healing discipline —
  that slot's datamap names are `blood_healing` / `base_blood_healing`, from `stats.txt`
  InternalName `Blood_Healing` (the patch's display `Name` is `Bloodheal`, a third spelling that
  addresses nothing). The name therefore resolves at step 4, the comparison is always true, and
  the branch is unconditional. A read of the discipline rating has to spell it
  `pc.base_blood_healing`.

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

## Datamaps are built at runtime — the base `CBaseEntity` contract

VtMB has **no static `DEFINE_FIELD` arrays**; each class's `datamap_t` is populated at init by a
per-class **builder** that writes 44-byte `typedescription_t` records into `.data`. The
`CBaseEntity` builder is **`FUN_100a22f0`**; every record carries the internal name (`m_spawnflags`),
the external/Hammer name (`spawnflags`), `fieldType`, `fieldOffset`, the `flags` byte (keyable
bit, see the write path above), and — for inputs — the `inputFunc`. Reading a datamap out of the binary therefore
means decompiling these builders, not walking a static table.

The base `CBaseEntity` contract every entity inherits (from `FUN_100a22f0`):

- **Keyfields** — `angles, model, target, targetname, spawnflags, health, max_health, flags,
  velocity, avelocity, basevelocity, gravity, friction, ltime, waterlevel, watertype, soundgroup,
  usescript, npc_transparent, blocks_traces, dmg_filter_name, use_filter_name`.
- **Inputs** — `Kill, ScriptHide, ScriptUnhide, Use, SetParent, ClearParent, Alpha, Color,
  SetSoundOverrideEnt, SetFakeSilence`.
- **Outputs** — `OnUseBegin, OnUseEnd`.

`usescript`, `soundgroup`, `npc_transparent`, `blocks_traces`, `SetFakeSilence`,
`SetSoundOverrideEnt`, and `ScriptHide`/`ScriptUnhide` are **VtMB additions** to the base entity
(not stock Source), so the port registers them once on the base class. The ScriptHide save-state
fields (`m_bScriptHidden`, `m_ScriptSavedSolid`, `m_ScriptSavedMoveType`, `m_ScriptSavedSolidFlags`,
`m_fScriptSavedEffects`, `m_pfnScriptSavedThink`) are in the same base map — the persisted form of
the whole-entity OFF switch described in `docs/vtmb/entity_io.md`.

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

**An input-only record describes no member, so it is keyed by nothing.** A
`DEFINE_INPUTFUNC` record carries `externalName` (+0x14) and `inputFunc` (+0x1C) but leaves
`fieldName` (+4) **null** and `fieldOffset` (+8) **zero** — it names a handler, not a field.
Any reader that keys a class's records by offset, or that skips a record with no field name,
therefore discards the entity's entire input surface while still reporting a complete
structure. The offsets are real: `InputScriptHide`'s record at `0x10553d24` reads back all
zeroes from the image and its `inputFunc` is written by the builder as a single
`mov [0x10553d40], 0x1001488f`.

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

**The level script's own top-level names end up in `__main__` too.** The field-6 dispatch wraps
the payload in `__main__.%s` (`0x1055e370`), so a payload's *leading* name is resolved as an
attribute of `__main__` — `journalPickup()` runs as `__main__.journalPickup()`, and that only
resolves if the level script's `def`s are attributes of `__main__`. Two independent checks agree:
`tutorial.py`'s `__main__.Level = __name__` only carries information if `__name__` is the script's
own module name (so it is imported as a module, then merged — not exec'd into `__main__`), and
`hw_609_1` fires a bare **`FindPlayer().ClearActiveDisciplines()`** while its level script
(`hollywood.py`, unlike `tutorial.py`) never aliases `FindPlayer` at module level — only
`__main__` can answer that name. A host that evaluates payloads in the level module's namespace
instead resolves the script's own functions but not the engine globals the script did not alias.

## The five call paths

| Trigger | Mechanism |
|---|---|
| Level-script callback | `PyRun_String` / `PyObject_CallFunction`. **`vampire.dll`** holds the format string **`__main__.%s`** (`0x1055e370`, in the `CEventQueue` dispatch region) — output field 6's `journalPickup()` becomes `__main__.journalPickup()`. (`engine.dll` owns only the VM boot.) |
| Entity output field 6 | Source's output format is extended from 5 comma fields to **7**; field 6 is a Python call string. 6,956 of 24,081 engine-loaded outputs (29%) carry one — 6,851 fire *only* Python with no I/O target, 105 do both (retail: 1,591 of 16,125, 10%; 1,500 / 91). |
| `logic_pythoncheck` | `python_script` keyvalue is an expression → `PyRun_String(…, Py_eval_input)` → the integer-only condition gate below → standard `OnTrue`/`OnFalse` I/O. |
| Dialogue | `.dlg` field 4 (condition, eval) and field 5 (action, exec). |
| Quest completion state | a `CompletionState`'s **`"Event"`** key in `vdata/system/quests_*.txt` — *"script data, such as a flag assignment or a function call, that will be passed to the script interpreter"*. `CVPlayer::SetQuest` runs it with **`PyRun_ConsoleString(src, Py_file_input, __main__.__dict__, __main__.__dict__)`** — so a statement, not an expression, in the same namespace field 6 resolves — traced as `RUNNING PYTHON AT TIME %f: %s` and `PyErr_Print`ed on failure. It fires **after** that state's `AwardMoney` and `AwardXP`, and only when the state actually changed (`docs/vtmb/game_runtime.md` → "Quests"). **No shipped row authors one.** |

The current 23-map export survey finds 209 field-5 identifiers absent from `sp_tutorial_1`, but no
new Python call path. They are level-callback and `G`-key vocabulary carried by the same entity
output record, not new bridge functions, a Python tick or a parallel scheduler. The exhaustive
names/examples and their manifest are in `docs/vtmb/exported-map-event-surface.md` and its generated
survey report.

### The Python condition gate — a non-zero integer, not truthiness

A VtMB Python **condition/dependency** gate is true **only** when the evaluated result is a Python
**integer** with a non-zero value. The engine runs `PyRun_String(expr, Py_eval_input, __main__, __main__)`
with the result pre-seeded to `0`, then tests `result->ob_type == PyInt_Type` and takes
`PyInt_AsLong`. A non-integer result — a non-empty string, a list, even the float `1.0` — or an
error leaves the seed at `0` and reads **false**. This is **not** `PyObject_IsTrue` / general
Python truthiness, under which a non-empty string or a non-zero float would be true.

One rule, **four surfaces**, all routing through the same helper
`CDialogDependency::CallPyDialogFunction` (`FUN_100ea2d0`, invoked with `Py_eval_input`) or its
byte-identical twin, the `logic_pythoncheck` evaluator `FUN_10135290`:

- `logic_pythoncheck`'s `Test` expression;
- the computer-terminal `dependency` (`FUN_1021bec0` → `FUN_100ea2d0`; see `docs/vtmb/computer-terminals.md`);
- the `game_sign` / `prop_sign` dependency (`FUN_101d2850`);
- the `.dlg` col-4 line/choice condition (`CDialogDependency::TestPython` `FUN_100e9ff0` → `FUN_100ea2d0`).

A caller that selects among gated blocks takes the first block whose dependency returns a non-zero
integer.

Two neighbouring Python paths are **not** this gate and must not be conflated with it:

- the `.dlg` **action** (col 5) runs as statements (`Py_file_input`, mode `0x100`) for side effects
  only; its return value is **discarded**, with no `ob_type` test at all.
- the `usescript` line **selector** calls the same helper under `Py_eval_input` but uses the
  returned integer **directly as the line id to start on**, not as a boolean.

The truth test is the exact `ob_type == PyInt_Type` **identity** check, not `PyInt_Check` — so a
Python `long` fails it exactly as a string or float does; only a genuine `PyInt` contributes. A
Python 2.7 `bool` (`ob_type` is `PyBool_Type`, not `PyInt_Type`) fails the identity test too, so
the reproduction must map all four surfaces through one predicate that resolves this identically.

### Synchronous calls versus queued Python

The bridge has three distinct event boundaries:

- Calling a reflected entity input from Python is **synchronous**. The PyMethodDef table entry
  `entity_input_function` (`0x1058f658`, thunk `0x10006429`, body `FUN_101962a0`) marshals the
  argument and calls the entity's `AcceptInput` virtual at `+0x1d8` immediately, with null
  activator and caller. The input body completes before Python resumes. Outputs fired by that body
  still enter the ordinary event queue.
- Assigning a reflected entity field or a `G` key is a direct datamap/data-manager write. It does
  not synthesize an input event unless the datamap member is explicitly an input function.
- Output field 5 and `ScheduleTask` are queued. For a combined output event, service delivers the
  named entity input(s) first and then evaluates the field-5 `__main__.%s` call. Consequently the
  Python observes all synchronous mutations performed by those receivers, while their newly
  emitted zero-delay outputs wait behind the equal-time cohort already queued.

`ScheduleTask` is closed, not an inferred parallel scheduler. Its PyMethodDef body
`FUN_10196ea0` parses `(delay, source)`, creates a Python string, and calls `FUN_100ce0d0`, which
adds a Python-typed `CEventQueue` entry due at `curtime + delay`. Service calls `FUN_100ce8a0` to
execute the stored source against the **live** `__main__` dictionary. The pending source therefore
shares ordering, pause/scaling and save/restore with delayed entity I/O; it does not capture a
module-local namespace or a state snapshot.

`logic_pythoncheck` is the opposite boundary: its `Test` input evaluates the expression
synchronously, converts only a Python integer result to truth, then queues ordinary `OnTrue` or
`OnFalse` output actions. An exception or non-integer prints and selects false; it does not cancel
earlier work in the queue service pass.

**Nothing in the server bounds recursion on the synchronous path.** A script→input→script cycle is
possible whenever an input body itself re-enters Python — `logic_pythoncheck.Test` (`FUN_10135290`,
`PyRun_String(..., Py_eval_input)`) and the `prop_hacking` runscript are the reachable examples — and
no participant on that path counts depth:

- `CBaseEntity::AcceptInput` is `FUN_100abc90` (vtable slot 118 = `+0x1d8`, identical in every
  entity vftable checked; trace string `0x1055714c`). Its body is a profiler scope, one entry gate, a datamap walk
  over the class chain, the `m_pfnInputActivator` / caller EHANDLE writes (`+0x10c` / `+0x110`),
  developer `DevMsg` reporting, an optional debug-overlay record when `+0x224` carries `0x10`, the
  variant-type conversion, and then the record's `inputFunc` call. It has **no depth counter, no
  reentrancy flag and no budget**. The single entry gate is the hidden-state test
  `FUN_100b5190` = `byte [this+0xf4]` (see "Hidden state" in `docs/vtmb/entity_io.md`): a hidden
  entity returns `true` for every input whose name does not begin with `ScriptUnhide`. The byte
  written at `+0x1b1` after a successful dispatch is set, never read at entry.
- `entity_input_function`'s body `FUN_101962a0` carries none either. It validates the CObject, parses
  the argument per the datamap field type, builds the `variant_t`, calls `vt+0x1d8` once, and returns
  `None`. Neither `FUN_10135290` nor the field-5 caller `FUN_100ce990` holds a guard flag.

The only bound is the embedded interpreter's own. Retail embeds **CPython 2.1.2**
(`Bin/vampire_python21.dll`, version string at file offset `0x89d20`), whose `recursion_limit` global
lives at `0x1e1808d8` (image base `0x1e100000`; `Py_GetRecursionLimit` at `0x1e10f340` is `MOV EAX,
[0x1e1808d8]; RET`) and is initialized to **1000**. `vampire.dll` imports 69 symbols from that DLL and
**neither `Py_SetRecursionLimit` nor `Py_GetRecursionLimit` is among them**, and no retail or patch
script calls `sys.setrecursionlimit`, so the default stands for the whole session. Each cycle level
adds at least one Python frame, so `tstate->recursion_depth` accumulates monotonically while the
outer levels sit blocked inside `AcceptInput`.

At exhaustion the interpreter raises `RuntimeError: maximum recursion depth exceeded` (string at file
offset `0x80cf0`) out of the innermost `PyRun_String`. That lands on the ordinary error policy below:
the C++ boundary calls `PyErr_Print()` and continues, so a `logic_pythoncheck` at the wall simply
evaluates **false** and the stack unwinds normally — the limit converts a runaway cycle into a printed
traceback plus error-to-false at the innermost level, not into an abort. It does not by itself
terminate a cycle whose script re-enters after receiving that false.

Whether 1000 nested levels are actually reachable before the native stack fails is **not settled by
static evidence**: `Vampire.exe` reserves a 1 MB main-thread stack (PE optional header), and each
cycle level costs one interpreter frame plus the whole `entity_input_function` → `AcceptInput` →
input-body → `PyRun_String` C++ path, which is not measurable from the image alone. Treat "Python
raises first" and "the C stack fails first" as both open; what *is* proven is that the server itself
contributes no limit.

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

`G` is engine-owned (a C type **`PyDataManager`** whose method table holds exactly two entries,
`ClearAll` `0x1019b7b0` and `keys` `0x1019b7e0` — see the note on `0x1058f5d0` above), injected
into `__main__`, and **never constructed in Python**. It is one flat, int-valued namespace:
**345 distinct flags** referenced by retail scripts (549 with the patch), **~900** by dialogue.

**208 of the 345 flags the scripts read are never assigned in Python** — dialogue writes
them. Level scripts and dialogue are not separable systems.

Shapes are overwhelmingly small ints (`G.x = G.x + 1`); the only exceptions across the
whole retail corpus are 3 lists, 1 dict (`G.morgue`, only `[name]=1` / `has_key`), and 3
strings.

**Default-on-miss = `0`, confirmed.** `G` is backed by two engine dict globals — the flag
dict `0x1072b370` and the `morgue` dict `0x1072b374`. Its `tp_getattr` (`0x1019b3d0`) resolves
a name in order: `InitMode` → an int field on the object; `morgue` → the morgue dict;
`Py_FindMethod` against the method table `0x1058f5d0` → a bound method; then
`PyDict_GetItemString(flagdict, name)` → the stored value; **else `PyErr_Clear()` then
`PyInt_FromLong(0)`** — so `G.Story_State` on an unset flag returns integer `0`, never raises.
`tp_setattr` (`0x1019b570`) mirrors it: `InitMode` sets the int, `morgue` and the method names
are read-only, everything else is `PyDict_SetItemString` (a `None`/NULL value deletes the key).

**`G[k]` is `G.k` — subscripting is the attribute path.** `PyDataManager`'s type object
(`0x1058fa08`; `tp_name` `0x1058fccc`, `tp_basicsize` 12 = the header plus the `InitMode` int)
carries a **`tp_as_mapping`** at `0x1058f9f8`, and both accessors are thin adapters over the
attribute slots:

```c
// mp_subscript 0x1019b4a0
if (key->ob_type == &PyString_Type)          // 0x109f37d0
    return tp_getattr(self, PyString_AsString(key));   // literal tail JMP to 0x1019b3d0
return PyInt_FromLong(0);                    // a non-string key reads as integer 0

// mp_ass_subscript 0x1019b720
if (key->ob_type == &PyString_Type)
    return tp_setattr(self, PyString_AsString(key), value);   // CALL 0x1019b570
// non-string key: tail-jumps PyDict_SetItem(self, key, value) — the MANAGER object in the dict
// slot, i.e. a latent bug. Unreachable: every G key in the corpus is a string.
```

So subscripting inherits everything the attribute path has, default-on-miss `0` included, and
`mp_length` (`0x1019b790`) reports the flag count. This is load-bearing rather than decorative:
`tutorial.py`'s `saveState()` — the **first** thing `DialogPostProcess()` calls — is
`for k in G.keys(): G_tut[k] = G[k]`, and `state()` writes back with `G[k] = ...`. Four scripts
subscript `G` (`tutorial`, `temple`, `zvtool_file`, `zvtool_pc`).

`G` is the save unit: `vampire.dll` carries `CPython_SaveRestoreBlockHandler` and
`CPyObjStrSaveRestoreDataOps`, pickles the flag + morgue dicts (`cPickle`), and the game ships
`pickle.py` + `copy_reg.py` for it. The handler's vftable is `0x10476ac8`: slot 0 `GetBlockName`
returns `"Python"`, slot 2 `Save` is `0x1019adc0`, slot 7 `Restore` is `0x1019b130`; the other six
slots are empty stubs. `Save` imports `cPickle`, takes its `dump`, and calls
`cPickle.dump(dict, file)` twice — flag dict first, morgue second — against a file-like adapter
over the `ISave` buffer, framing each as `byte tag; int len; pickle[len]`. On-disk layout:
`docs/vtmb/savegame_format.md`.

Two console commands operate on the pair: `gclearall` (`CC_GClearAll` → `G.ClearAll()`, "Resets
all state flags" — `PyDict_New`s both dicts, `0x10199e20`) and `gcleardialog` ("Resets all dialog
state flags"). The cvar `show_python_variable_changes` logs every assignment
(`"Setting Variable: G.%s = %s"` / `"Clearing Variable: G.%s"`), and `PyDataManager_Log`
(`0x1019b800`) dumps both dicts under the headings `Global Variables:` and `Morgue Entries:`.

**`G.morgue` is the dead-character registry**, keyed by NPC targetname → `1`. The shipped
`vamputil.py` is the whole public API over it — `IsDead(charname)` is
`__main__.G.morgue.has_key(charname)` and `MarkAsDead(charname)` is `__main__.G.morgue[charname] = 1`
— and story scripts gate content on it (`IsDead("Milligan")`, `IsDead("Pisha")`,
`IsDead("Heather")`). Because insertion is a script/dialogue call rather than an engine death
hook, an entry does not imply the entity is mechanically dead in the map's saved state.

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

`vamputil.py` (46 functions: `IsClan`, `IsDead`, `RandomLine`, `Whisper`, …) is exec'd
into `__main__` and is **not** engine API. Two of its helpers — `Whisper` and `FrenzyTrigger` —
**share a name with an engine datamap input**, and the receiver decides which runs: bare
`Whisper(...)` is the script helper, `pc.Whisper(...)` is the input (`docs/vtmb/script_api.md`).

### `.dlg` conditions/actions — `dlgexpr`, not Python

Field 4 may join an **engine skill-check** to a Python expression with `&`. The operator is
an implicit `>=` in 1,348 of 1,353 cases; the halves can swap order; skill-checks compose
with `and`/`or` and parens; 830 conditions are skill-only with no Python at all.

A check's `ident` is not always a feat: the corpus checks stats (`Humanity` 272) and disciplines
(`Dominate` 102, `Thaumaturgy` 55) through the same syntax, and an `M_`/`F_` prefix on it is the
engine dependency's **sex gate** rather than a name (`docs/vtmb/game_runtime.md` §3 → "Feats").

Across 49,359 rows: zero imports, lambdas, comprehensions, exceptions, control flow,
subscripts, slices, or container literals. Literals are `int` (12,278) and `str` (4,682)
plus **one float game-wide**. Actions are only assignment (2,762) and call (1,156),
separated by `;` (and `&` in 7 places).

```ebnf
condition  = skillcheck , [ ("&"|"|") , expr ] | expr , [ ("&"|"|") , skillcheck ] | expr ;
skillcheck = [ "(" ] , [ sexgate ] , ident , [ relop ] , int , [ ")" ] | skillcheck , ("and"|"or") , skillcheck ;
sexgate    = "M_" | "F_" ;   (* the dependency's required-gender field, not part of the name *)
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

## The script file layer — three path spellings and a write guard

The scripts read and write the install tree directly, through stock `open` and the `nt` module.
Five of the 41 mirrored files touch the filesystem; `fileutil.py` is the game's own abstraction
over it (`exists`/`isDir`/`isFile`/`list`/`listdir`/`listfiles`/`mkdir`/`readlines`/`writetofile`/
`appendtofile`/`copyfile`/`removefile`), and callers bypass it freely.

Paths are spelled **three** ways, and only the first goes through a function an embedder can
redirect:

| | Shape | Example |
|---|---|---|
| A | `nt.getcwd() + "\" + moddir + "\<tree>\..."` | `vamputil.FixKeyBindings` reads `cfg\config.cfg` (`vamputil.py:2352`) |
| B | moddir-relative, no `getcwd` | `open(moddir + "/vdata/hackterminals/haven_pc.txt")` (`vamputil.py:588`) |
| C | bare relative, no moddir either | `open("zvtool_g_dump.txt", "w")` (`zvtool_file.py:64`) |

B and C hand a relative path straight to the OS, which resolves it against the **process** cwd. In
the real game all three land in the same place because the process is the game and its cwd is the
install folder. The style list is not closed — C exists precisely because nothing enforces a
convention.

`sys.moddir` defaults to `"Vampire"` (`fileutil.py:8-11`; `zvtool_file.py:23-26` defaults it
lowercase), and mod support is the reason it is a variable at all.

**`fileutil` guards writes by string match.** Every write path — `mkdir`, `writetofile`,
`appendtofile`, both ends of `copyfile`, `removefile` — refuses unless the path contains
`"\" + moddir + "\"` (`fileutil.py:81,102,113,127,158`), the stated intent being "to help mitigate
possibility of damaging non-game related files". So the moddir value is load-bearing for writes,
not just for reads.

**The scripts write, and not only scratch files:**

- `vamputil.py:588-596` — read-modify-write of `vdata/hackterminals/haven_pc.txt`, replacing line 5
  with the PC's name so the haven computer's email client addresses the player.
- `vamputil.py:889-946+` — the Unofficial Patch's hunter mode `copyfile`s `- hunter` variants over
  shipped assets: `scripts/kb_act.lst`, a `sound/interface/` WAV, `vdata/signs/`+`vdata/system/`+
  `vdata/items/` tables, and `materials/` `.tth`/`.ttz`/`.vmt` pairs.
- `zvtool_file.py:200-246` — opens the map's own `.bsp` `"rb+"`, seeks, truncates and appends a
  modified entity lump (a dev tool, but it is in the shipped tree).

**Existence is also game logic.** `fileutil.isFile()` on a `sound/character/dlg/**/*.lip` gates
which dialogue line plays (`vamputil.py:1039,1052,1059`; `fusyndicate.py:240`) — a missing file is
a branch, not an error, so a rebuild that cannot answer the probe silently changes behaviour.

The Unreal answer is a filesystem namespace scoped to the interpreter rather than a redirected
`getcwd` — reads served from the content mirror, writes into a `Saved/` overlay. Why the process
cwd is not available, and what the overlay buys, is in the design comment
on `Source/ElysiumUE/Private/Scripting/ElysiumScriptFS.h`.

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

**IronPython facts:** IronPython **2.7 is Python 2.7** and
would accept VtMB's 2.1 syntax nearly as-is, but is dormant and .NET-Framework-era.
IronPython **3** is maintained and targets **.NET 8/10** but is Python 3.4
and needs a 2to3 pass; it maps `int` to `BigInteger`, and the DLR's runtime codegen rules
out AOT/trimming. Neither touches the ~11,300 `dlgexpr` snippets, which need their own
parser regardless.

## Reproduction

No dependency beyond the repo's own readers plus Ghidra (`$ELYSIUM_WORK_ROOT/research/ghidra/`, see its README).

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

The file-like table (`0x1058f620`) is the **`IRestore` buffer adapter** — the read side of the
same pair `Save` pickles through. `read` (`0x1019bba0`) bounds its request against the restore
buffer's remaining span and raises `IOError` carrying `"py_obj->irestore->ReadData read …"`;
`readline` is `0x1019bc80`. It is the file object `cPickle.load` reads `G` back through.

## The action inventory

Which names the shipped content actually calls, how often, what each one's signature and owning
class are, and which system has to back it: **`docs/vtmb/script_api.md`**. It carries the whole recovered
surface — the six method tables with their `ml_doc` contracts, the `CBaseCombatCharacter` /
`CAI_BaseNPC` / player datamap inputs, and the demand ranking that orders the build. This doc
stays the mechanism; that one is the inventory.

**Recovering a datamap takes two complementary reads.** An image read (`DumpDatamap`) sees only
the statically-initialized leading records; everything the per-class builder assigns at
static-init reads back as zero, so `CBaseCombatCharacter` reports `INPUTS (0)` for a class
carrying 25. The builder's decompiled assignments are the other half
(`research/tooling/ghidra/driver/parse_datamap_builder.py` replays them). A class's builder is found by grepping
any one of its input names — the string table carries both the external name and the
`Input<Name>` internal name — and each input's handler is found the same way, since every handler
opens by pushing a profiler marker named `C<Class>::Input<Name>`.
