# The script → engine action surface

What VtMB's scripts *call*, what each name *does*, and which system has to back it.

`docs/vtmb/python_bridge.md` owns the **mechanism** — how `vampire.dll` binds Python to the engine, and
why `Entity.__getattr__` being a datamap walk means there is no fixed method API to port. This
doc is the **inventory**: one row per name the shipped content reaches for, with its
signature, its owning binding table or datamap, its call count across the whole corpus, and the
runtime system that must answer it.

**Confidence.** Names, arities, argument *types*, owning class, doc strings and handler
addresses are read directly out of `vampire.dll` and are exact. Per-handler **semantics** are
recovered where stated and marked pending otherwise — the handler address is recorded in every
row, so recovering one is a single `DumpFuncs` run, done by the system task that consumes it.

## How to regenerate

Two halves, both reproducible from the user's own install.

**Demand** — `uv run elysium research script_api_survey --json $ELYSIUM_EXPORT_ROOT/_mcp/script_api.json`. Reads
only `$ELYSIUM_EXPORT_ROOT/` (the gitignored mirror): 36 level scripts, 147 `.dlg` (50,393 rows, columns 4 and 5),
and the exported maps' output field-6 payloads. It folds module-level aliases — every level
script opens with `Find = __main__.FindEntityByName`, and 1,911 calls hide behind that one line —
and cross-checks each name against `ElysiumScriptNatives.cpp`, the CPython host's `PyMethodDef`
tables, and every `D.Input(TEXT("…"))` in the class registry, so a row says both what the engine
offers and what the runtime currently answers.

**Supply** — the Ghidra workspace (`$ELYSIUM_WORK_ROOT/research/ghidra/`, local-only):

```powershell
# a PyMethodDef table: names, doc strings, thunk -> body, decompiled bodies
research/tooling/ghidra/driver/run.ps1 -Program vampire.dll -Script DumpPyMethods `
  -ScriptArgs "table=1058f868 depth=1 out=…/py_character.txt"

# a class datamap: the builder, then replay its assignments
research/tooling/ghidra/driver/run.ps1 -Program vampire.dll -Script DumpFuncs `
  -ScriptArgs "funcs=1031a600 depth=0 out=…/bcc_builder.txt"
uv run elysium research parse_datamap_builder …/bcc_builder.txt --recs 10616694 --count 305
```

### The **Args** column is stated by the image, not inferred

Every bound method begins by calling `PyArg_ParseTuple(args, "<format>", …)`, and that format
string **is** the Python-level argument list. It was unreadable for a long time for a mechanical
reason: Ghidra imports a DLL's functions with no signature, so the second parameter was untyped
and the operand stayed `&DAT_10590cb8`; and every one of these formats is shorter than the string
analyzer's five-character minimum, so nothing ever made them strings either.

Applying CPython **2.1.2**'s own declared prototypes to the imports fixes both — `PyArg_ParseTuple`
declares `(PyObject *, char *, ...)`, and with the parameter typed the operands resolve:

```
uv run elysium research pyapi                          # prototypes out of Include/*.h
uv run elysium research corpus pyapi --apply           # apply them, and make the formats strings
```

Two consequences worth knowing when reading a body:

- **A second `PyArg_ParseTuple` after a `PyErr_Clear()` is an optional-argument form**, not a
  retry. `StartBarter` tries `"Oiii"` and falls back to `"Oii"`; `SetExpression` tries
  `"Osfffff"` then `"Os"`. The longer format is the full arity and the shorter one is what a
  script may omit.
- **A format is not a doc string, and it wins.** Six `ml_doc` values in these tables are
  copy-paste errors from a neighbouring method (listed below), and an argument list read from
  the prose inherits the error. `SetExpression` carries `React`'s doc; its format says
  `(char, str, …)` where `React`'s says `(char, obj, int, …)`.

### Reading a datamap takes both techniques

`DumpDatamap` reads the image; `parse_datamap_builder.py` replays the builder. Neither alone is
complete, because VtMB's datamaps are **half static**: the leading records ship initialized in
`.data`, and everything the per-class builder assigns at static-init reads back as **zero** from
the un-run image. `CBaseCombatCharacter` is the worked case — an image read reports
`INPUTS (0)` for a class carrying 25, because every input record is builder-written. The builder
tail names both halves of what the image lacks:

```c
_DAT_10616650 = 0x131;              // datamap_t.numFields = 305
_DAT_1061664c = &DAT_10616694;      // datamap_t.dataDesc  = the record array
return &DAT_1061664c;               // the datamap_t itself
```

Feed that base and count to the parser. Names come free — Ghidra renders a string pointer as
`s_<content>_<address>`, so the symbol carries the literal.

**Finding a class's builder:** grep for one of its input names. The string table carries both the
external name and the `Input<Name>` internal name, and the referencing function is the builder.
Handler bodies are found the same way — every input handler opens by pushing a profiler marker
named `C<Class>::Input<Name>`, so a `str=::Input` sweep maps handler names to function addresses
in one run.

## The demand, in one table

16,860 executable call sites over 676 distinct called names. Grouped by the system that has to
answer them (the rows below are selected demand slices, not a partition of the total):

| System | Calls | Leading names | Owning task |
|---|---|---|---|
| NPC disposition / reactions | **2,862** | `SetDisposition` 2510, `SetRelationship` 334 | 9.9 |
| Inventory & items | **853** | `HasItem` 327, `RemoveItem` 182, `GiveItem` 126, `StartBarter` 108 | 9.8 |
| Random dialogue gates | **589** | `OneOfSet` 589 | 9.7 (solved, below) |
| Sheet: XP / humanity / masquerade | **290** | `AwardExperience` 77, `HumanityAdd` 69, `CalcFeat` 53 | 9.4 |
| Economy | **250** | `CurrentMoney` 86, `MoneyAdd` 83, `MoneyRemove` 80 | 9.10 |
| Sequences & conversation camera | **224** | `SetCamera` 115, `BeginSequence` 72 | 8.5, 11.7 |
| AI schedules | 76 | `FleeAndDie`, `SetupPatrolType`, `FollowPatrolPath` | named patrol slice: 8.5; remainder: 13.5 |
| Scripted line / whisper audio | 48 | `PlayDialogFile` 39, receiver-qualified `Whisper` 9 | AUD2, AUD3 |

By binding kind: 21 Character methods (4,982 calls), 9 module globals (3,712), 62 registered
entity inputs (2,278), 10 `Entity` base methods (650), 455 script-defined names (4,155), 40
standard-library names (393), one `G`-method name (4), and 78 names (686 calls) that resolve to
nothing the runtime knows.

Two names in the tables are called by **no** shipped script: `React` and `SquadSeesPlayer` — API
the content never used.

### A `vamputil` helper can shadow an engine input name

`vamputil.py` defines both `Whisper(soundfile)` and `FrenzyTrigger(char)` — and those are also
`CBaseCombatCharacter` / player datamap **input** names. Both spellings are live in the corpus,
and the receiver decides which one runs: a bare `Whisper("Crying")` calls the script helper
(25 sites), while `pc.Whisper("Crying")` goes through `__getattr__` to the datamap input
(9 sites); `FrenzyTrigger` splits 1 bare / 6 receiver-qualified. So a name's binding is not a
property of the name — it is a property of the call site, and a port that resolves either
spelling to a single implementation changes behaviour. The helpers are not pass-throughs either:
`Whisper(s)` forwards to `pc.Whisper(s)` (so the bare spelling always addresses the *player*,
whatever the surrounding receiver), and `FrenzyTrigger(char)` hands the input a `1` where the 5
`.dlg` sites spell `pc.FrenzyTrigger()` and hand it nothing. Counts in the tables below are the
**receiver-qualified** half for these two.

In this rebuild the split is structural: neither name may enter the shared native table
(`ElysiumScriptNatives.cpp`, which carries the rule as a comment and
`Elysium.Substrate.OneOfSet` as its regression), so the bare spelling resolves in `__main__`
against the real `vamputil.py` the CPython host imports, while the qualified spelling reaches the
datamap input through the ordinary class-chain walk.

## The console escape hatch

`ccmd.<name>` sends a script call out through the client console, so console commands that level
scripts invoke are part of the action surface even though they are not Python methods. The opening
path establishes these commands directly from the binaries:

| Name | Registered by | Contract |
|---|---|---|
| `teleport_player <targetname>` / `<x> <y> <z>` | `vampire.dll` | moves the player to a named entity or coordinate; a missing name prints `Could not find entity named %s` |
| `v_setpause` | `client.dll` | takes the client-side modal pause used by the character wizard |
| `v_unpause` | `client.dll` | releases that pause; `CharEditPanel` close executes it before the genesis teleport |
| `vskip_intro` | `vampire.dll` | skips the current intro scene; distinct from the wizard footer's `vchar_skip_intro` ConVar |

`vskip_intro` is not the character wizard's ordinary skip switch. The Unofficial Patch reaches it
only from `unhidePlus()` for clans 9–11; the reader of `vchar_skip_intro` remains unrecovered.

## Module globals — table `0x1058f7a8`, 11 entries

Doc strings are verbatim from `ml_doc`; they are the contract the scripts rely on.

| Name | Body | Args | Calls | Contract |
|---|---|---|---|---|
| `FindEntityByName` | `10196970` | `"s"` | **1,911** | *"Find a single entity by its targetname field. Returns None if not found. It is an error if multiple entities have the same name."* |
| `FindPlayer` | `10196940` | none | 531 | *"Find the first player entity, or NULL if there is not one spawned"* |
| `OneOfSet` | `10196c50` | `"ii"` | 589 | **no doc string** — the only global without one. Solved below. |
| `ScheduleTask` | `10196ea0` | `"fs"` | 245 | *"Sets up a task callback."* |
| `FindEntitiesByName` | `10196d10` | `"s"` | 163 | *"Returns a list of entities matching the name."* |
| `ChangeMap` | `10196a40` | `"fss"` | 76 | *"Changes to the map and sets the player at the specified landmark"* |
| `FindEntitiesByClass` | `10196de0` | `"s"` | 70 | *"Returns a list of entities matching the class."* |
| `CreateEntityNoSpawn` | `10197050` | `"sOO"` | 64 | *"Creates an entity, but does not call it's spawn function"* |
| `CallEntitySpawn` | `101970f0` | `"O"` | 64 | *"Dispatches the entitie's spawn function"* |
| `IsPCMalk` | `10196bb0` | none | — | *"Returns 1 if the player is Malkavian, otherwise returns 0."* |
| `SquadSeesPlayer` | `10196f30` | `"s"` | 0 | *"Returns 1 if NPCs in the squad can see the player."* |

`Args` is the `PyArg_ParseTuple` format each body states; **none** means the body never parses,
which is how a no-argument global says so. Note `ChangeMap` takes a **float first**
(`"fss"` — a delay, then map and landmark), and `ScheduleTask` likewise (`"fs"` — delay, then
the callback name).

All eleven are `METH_VARARGS`. `IsPCMalk` compares the player's clan against a lazily-initialized
`"Player_Malkavian"` lookup and returns `None` — not `0` — when no player is spawned.

### `OneOfSet(which, count)` — solved

589 dialogue gates ride on this and nothing in the script corpus defines it. The body
(`10196c50`) is:

```c
PyArg_ParseTuple(args, "ii", &which, &count);
roll = (**(code **)(*DAT_1070b22c + 0x1e0))();     // engine counter, vtable slot +0x1e0
return PyInt_FromLong( (roll % count) == (which - 1) );
```

So it is a **1-based one-of-N selector**: `OneOfSet(2, 4)` is true on exactly one of four
outcomes. It is called from `.dlg` only — never from a level script — and always in **sets**: N
sibling rows carrying the same choice text, row *i* gated on `OneOfSet(i, N)`, so the set presents
as one line and the roll picks which destination it goes to. The whole corpus is 589 calls in
sets of 2, 4, 6, 7 (75 sets, the bulk) and 8, and the gate is routinely ANDed with a second
condition (`OneOfSet(1,6) and pc.CurrentMoney() >= 5`), which closes the selected row without
opening another.

**The set shape is what constrains the roll.** Exactly one row passes only if every gate in a set
reads *the same* roll, so a per-call draw is ruled out by the content: over a 7-row set it would
show no row 34% of the time and two or more 40% of the time. `DAT_1070b22c`'s vtable slot `+0x1e0`
must therefore hand back a value that is stable for at least the burst in which a turn's gates are
evaluated.

**The roll is the host frame counter.** `DAT_1070b22c` is the engine-server interface —
`vampire.dll` acquires it as `VEngineServer014`, which `engine.dll` registers with
`InterfaceReg` and implements as `CVEngineServer` (124 slots). Slot `0x1e0 / 4 = 120` is
`CVEngineServer::vfunc120` at `engine.dll 0x2010acf0`, whose whole body is:

```c
undefined4 CVEngineServer::vfunc120(void) { return DAT_20b42980; }
```

`DAT_20b42980` has exactly one writer in the module, `_Host_RunFrame` (`0x2008e450`, identified
by its own VProf scopes `_Host_RunFrame_Input` / `_Server` / `_Client` / `_Sound`), and what it
does there is `DAT_20b42980 = DAT_20b42980 + 1` — **one increment per host frame**.

So the value holds for exactly one frame, which is the unit a turn's whole choice list is
gathered in, and a repeated visit to the same set re-picks on the next frame. The set-shape
argument above predicted a value stable across the burst; the binary says the burst is the
frame.

The runtime implements the selector verbatim and draws **one roll per engine frame**
(`ElysiumScriptNatives::OneOfSetRoll`), the frame being the unit a turn's whole choice list is
gathered in (`FElysiumDlgConversation::EnterNpcLine` runs the gates in one synchronous burst) —
which is what retail does, now verified rather than reasoned.
`elysium.script.oneofset <roll>` pins it; `-1` restores the live draw.
Tested by `Elysium.Substrate.OneOfSet` (roadmap 9.7d).

## Character methods — table `0x1058f868`, 24 entries

Every method takes the character as its first parsed argument and resolves it to a
`CBaseEntity*`; three component pointers on that object carry the whole surface:

- **`+0x98`** — the dialogue/disposition component (`SetDisposition`, expression, gesture).
- **`+0x9c`** — the *combat character* (inventory, money, stats, feats). Its inventory owns the
  ordinary item handles and the carried keyring used by `HasItem`'s fallback.
- **`+0xa8`** — the player extension used by player-only item grant and the player-facing
  drop/barter services; it is not a second item container.

A method whose receiver lacks the component it needs raises `AttributeError` with a
method-specific message (`"SetDisposition needs to be called on …"`, `"invalid combat character
in CurrentMoney"`), rather than returning a falsy value.

| Name | Body | Args | Calls | Backing today |
|---|---|---|---|---|
| `SetDisposition` | `10197e50` | `(char, name:str, level:int)` | **2,510** | real disposition presentation (level, stance transition, face, gaze/blink); independent of combat/reaction score |
| `SetQuest` | `10199800` | `(char, quest:str, state:int)` | 732 | real (map + catalogue + awards + journal) |
| `GetQuestState` | `101987b0` | `(char, quest:str)` | 377 | real (quest map) |
| `HasItem` | `10198640` | `(char, item:str)` | 327 | real (ordinary slots, then keyring) |
| `IsMale` | `10199990` | `(char)` | 207 | real (sheet) |
| `RemoveItem` | `10199240` | `(char, item:str)` | 182 | real (stack decrement / final-entity destroy / keyring) |
| `GiveItem` | `10199100` | `(char, item:str)` | 126 | real (player-only grant; a failed grant logs) |
| `SetCamera` | `10198070` | `(char, shotfile:str)` | 115 | real (the 11.7 shot channel) |
| `StartBarter` | `101993c0` | `(char, int, int[, int])` — `"Oiii"`, else `"Oii"` | 108 | stub |
| `CurrentMoney` | `101998c0` | `(char)` — `"O"` | 86 | real (the `money` field) |
| `SeductiveFeed` | `10198150` | `(char, obj)` — `"OO"` | 54 | stub |
| `CalcFeat` | `10198cc0` | `(char, feat:str)` | 53 | real (the feat rating over the sheet) |
| `HasWeaponEquipped` | `101984c0` | `(char, item:str)` | 27 | real (exact compare vs the active weapon) |
| `AmmoCount` | `101989b0` | `(char, item:str)` | 19 | real (stack count / loaded magazine) |
| `GiveAmmo` | `10198b30` | `(char, item:str, count:int)` | 19 | real (stack add / reserve pool) |
| `BumpStat` | `10199a70` | `(char, stat:str, times:int)` | 19 | real (dots onto the base) |
| `WorldMap` | `10199520` | `(char, int)` — `"Oi"` | 12 | stub |
| `IsFollowerOf` | `101988c0` | `(char, obj)` — `"OO"` | 8 | stub |
| `SewerMap` | `10199690` | `(char, int)` — `"Oi"` | 4 | stub |
| `SetGesture` | `10197f60` | `(char, sequence:str)` | 2 | real exact-label lookup; a miss is a silent no-op |
| `GetMasqueradeLevel` | `10199ce0` | `(char)` | 2 | real (the masquerade counter) |
| `DialogDiscipline` | `10198310` | `(char, obj, str)` — `"OOs"` | — | the rating, no blood spent (the power is P13) |
| `SetExpression` | `10197ce0` | `(char, expr:str[, f, f, f, f, f])` — `"Osfffff"`, else `"Os"` | — | stub |
| `React` | `10197b00` | `(char, obj, int[, str])` — `"OOis"`, else `"OOi"` | 0 | stub |

The retail item ownership, keyring, stack, active-weapon, ammo, container and barter behavior
behind these signatures is recovered in `docs/vtmb/inventory.md`. The **Backing today** column
continues to report the current Unreal source; closing the reverse-engineering contract does not
turn those stubs into an implementation.

Useful doc strings: `SetCamera` — *"Sets the entity to use the named shot file as their cinematic
camera mode"* (so its argument keys `vdata/camerashots/`); `DialogDiscipline` — *"Uses a
discipline in dialog, doesn't deduct blood points"*; `GetQuestState` — *"Returns the state of the
player's quest, or 0 if this is an NPC.."*.

**`SetQuest` ignores its receiver.** The thunk fetches entity index **1** and hands the call to
`CVPlayer::SetQuest`, so a quest lands on the player however the call was written; a bad argument
list raises `AttributeError("bad args to SetQuest.")`. Its second argument is the completion
state's **1-based ordinal in file order**, not the authored `"ID"`, and the call does far more than
store a number — it resolves the state, pays `AwardMoney` then `AwardXP` then `Event`, and writes
the journal row, all of it skipped when the state did not actually change. The whole walk:
`docs/vtmb/game_runtime.md` → "Quests".

**`SetGesture` is an exact sequence route, and both shipped calls miss.** `FUN_10197f60` resolves
the character's component at entity `+0x98`, calls `LookupSequence`, and only on a non-negative
answer reads the duration and starts the gesture. A negative lookup returns `None` without a
warning or fallback. The two dialogue calls are Tourette lines 901 and 911, naming
`malk_female_idle` and `SM_Huddle`; neither label exists anywhere in Tourette's complete
include-model vocabulary, so both are inert in retail. This is distinct from
`scripted_sequence`, whose missing-label path warns and plays sequence 0.

Two of the sheet methods are decompiled in full; the semantics live in `docs/vtmb/game_runtime.md` §3, and
what a *caller* needs is:

- **`CalcFeat(char, feat)` returns an int — the feat rating, not a roll.** It is
  `Feats::FeatValue` (`101e56e0`) clamped to the feat's `MaxValue`, so the implicit `>=` a
  `dlgexpr` skill-check applies compares a rating. A bad feat name raises
  `AttributeError("invalid feat name -- %s")`; a non-character raises
  `"invalid combat character"`.

  **A divergence, marked.** The runtime resolves a name the feat table does not own **as a trait**
  and returns its current value, where VtMB raises. The reason is our own dlgexpr normalizer: it
  rewrites every `"<Name> <int>"` check into `pc.CalcFeat("<Name>") >= <int>`, and the `.dlg` corpus
  checks `Humanity` 272 times, `Dominate` 102 and `Thaumaturgy` 55 — all traits, which VtMB resolves
  in `CDialogDependency::TestSimple` off `GetCurrent` without going through `CalcFeat` at all
  (`docs/vtmb/game_runtime.md` §3). Reading the trait *is* the faithful answer for those checks; routing them
  through this one name is the divergence. A name that is neither feat nor trait still reads 0
  rather than raising, because a raise would abort the whole conversation line; error-to-false is
  the posture the rest of the scripting surface takes. The same dependency's **sex gate** (an
  `M_`/`F_` prefix) is handled by the normalizer, not here — it emits `pc.IsMale()` beside the call.
- **`BumpStat(char, stat, times)` takes three arguments** — the third is a **repeat count**, and
  the body loops it, adding one dot per pass. It writes the **base** (`CVStatList_t::IncBase`), and
  carries a ceiling of its own: each pass is skipped unless `GetBase(stat) < 5`, hardcoded and
  independent of the stat's `Max`. A count below 1 does nothing, so it **cannot decrement**; a
  count of 0 or a null name raises `"invalid args in BumpStat"`. One client notification fires
  after the loop, not per dot. (Its `ml_doc` is `DialogDiscipline`'s — one of the six copy-paste
  errors below — so none of this could be read off the doc string.)

**Six doc strings are copy-paste errors** — a build fingerprint, and a trap for anyone reading
them as spec: `CurrentMoney` carries `HasItem`'s text, `HasWeaponEquipped` carries `RemoveItem`'s,
`IsFollowerOf` carries `GetQuestState`'s, `BumpStat` carries `DialogDiscipline`'s, `SetExpression`
carries `React`'s, and (in the `Entity` table) `GetModelName` carries `SetModel`'s.

## `Entity` base methods — table `0x1058f698`, 13 records / 12 names

`__init__` *"takes a single PyCObject with a pointer to the entity"*, then `GetOrigin`,
`GetAngles`, `GetCenter`, `GetModelName`, `GetAngleVectors`, **`GetCenter` again** (record 6 is a
byte-identical duplicate of record 3 — same `ml_meth`, same doc), `SetOrigin`, `SetAngles`,
`SetModel`, `SetName`, `GetName`, `IsAlive`. There is **no `SetModelName`**: `SetModel` is the
only model writer. All twelve are backed for real by the CPython host.

`__getattr__` (`10195510`) / `__setattr__` (`10195a10`) sit in their own two-entry table
(`0x1058f778`), documented as *"gets attributes of C++ object by their keyvalue name"* and
*"checks name against members in game datatable"* — the datamap dispatch `docs/vtmb/python_bridge.md`
describes.

**`SetModel` changes animation ownership; it is not a sequence request.** The Python body at
`0x101977a0` parses the entity and model path, resolves the entity, verifies that the engine model
lookup returns an index greater than one, interns the path and calls virtual `+0x1a4`. On an
animating entity that reaches `CBaseAnimating::SetModel` at `0x10095030`, which applies the new
studio model and rebuilds model-dependent bone/controller state when the model index changes. The
Python route does not call `LookupSequence`, `SetIdealActivity`, or `ResetSequenceInfo`; whatever
action is requested next resolves against the new model/include graph. Every return path yields
`None`, including an unresolved entity/model.

The exported executable corpus contains **356** `SetModel` calls: 158 literal paths and 198
expressions/variables. The map entities add three `MorphModel` ownership fields and no authored
`SetModel` or `Transform` I/O wire. Across those 161 literal ownership demands, 54 name a model in
the current character/animated-prop manifest; 107 name static, weapon, viewmodel, null or other
models outside that manifest. That latter count is a classification boundary, not 107 missing
character animations. The reproducible call ledger is
`uv run elysium research action_animation_survey`.

## The datamap input surface

These are the names the survey could not resolve, and they are **not methods** — they are entity
inputs, reached by the same `__getattr__` datamap walk that serves a Hammer I/O wire. The
argument type is the record's `fieldType` (VtMB's shifted enum), which is the marshalling
contract a port needs.

### `CBaseCombatCharacter` — datamap `0x1061664c`, builder `FUN_1031a600`, 305 records, 25 inputs

| Input | Type | Calls | Handler |
|---|---|---|---|
| `MoneyAdd` | INTEGER | 83 | `LAB_1000b4a6` → `FUN_10340c90` |
| `MoneyRemove` | INTEGER | 80 | `LAB_1000fcef` |
| `WillTalk` | INTEGER | 79 | `LAB_1000c130` |
| `HumanityAdd` | INTEGER | 69 | `LAB_1001348f` |
| `ChangeMasqueradeLevel` | INTEGER | 44 | `LAB_10005e1b` |
| `Bloodloss` | INTEGER | 18 | `LAB_100128aa` |
| `FrenzyTrigger` | VOID | 6 | `LAB_10004665` |
| `Bloodgain` | INTEGER | 4 | `LAB_1000671c` |
| `ClearActiveDisciplines` | VOID | 3 | `LAB_1000cdba` |
| `FrenzyCheck` | INTEGER | 1 | `LAB_10002ae0` |
| `HungerCheck` | INTEGER | 0 | `LAB_10002ae0` — **the same handler as `FrenzyCheck`**; two external names, one behaviour |
| `Inventory_Remove` | CLASSPTR | 0 | `LAB_1000b370` |
| `BarterBegin` / `BarterEnd` | VOID | 0 | `LAB_100043b8` / `LAB_10004390` |
| `BloodHeal` | INTEGER | 0 | `LAB_100100b9` (internal name `BloodHealIn`) |
| `FrenzyUpdate` | INTEGER | 0 | `LAB_100079dc` |
| `PlayFloat` | VOID | 0 | `LAB_1000b898` |
| `SetHeadAsCameraTarget` / `SetBodyAsCameraTarget` | VOID | 0 | `LAB_10004cb4` / `LAB_100044cb` |
| `FadeHeadAsCameraTarget` / `FadeBodyAsCameraTarget` | FLOAT | 0 | `LAB_1000e917` / `LAB_1001018b` |
| `LookAtEntityEye` / `Center` / `Origin` | STRING | 0 | `LAB_10014a6f` / `LAB_10012d9b` / `LAB_1000c46e` |
| `LookAtEntityDefault` | VOID | 0 | `LAB_10009f52` |

Its outputs include `OnSellWeapon` and `OnBarterClose`, plus the save-only custom fields
`m_tEffectList`, `m_disciplineVisuals` and `m_Relationship`.

**Worked semantics — `InputMoneyAdd` (`FUN_10340c90`):**

```c
if (inputdata.value.fieldType == 4 && inputdata.value.int != 0)   // 4 = FIELD_INTEGER (VtMB)
    CBaseCombatCharacter::MoneyAdd(this, inputdata.value.int);    // FUN_10340e50
```

A zero-valued or wrong-typed input is a silent no-op. The `fieldType == 4` test is VtMB's own
shifted `fieldtype_t` appearing at the input-dispatch layer, not just in attribute marshalling.

### `CAI_BaseNPC` — datamap `0x105c9814`, builder `FUN_1027a820`, 102 records

One scripted input, `SetRelationship` (**STRING**, 334 calls, `LAB_100073d8`) — its saved
entity/class table and writer are present; senses, enemy assignment and combat schedule consumers
are not. Its 16 outputs are the NPC event surface:
`OnDamaged`, `OnDeath`, `OnHalfHealth`, `OnFoundEnemy`, `OnLostEnemyLOS`, `OnLostEnemy`,
`OnFoundPlayer`, `OnLostPlayerLOS`, `OnLostPlayer`, `OnHearWorld`, `OnHearPlayer`, `OnHearCombat`,
`OnGrappleBegin`, `OnGrappleEnd`, **`OnFedUponBegin`**, **`OnFedUponEnd`** — the last pair being
what roadmap B6's feed interaction fires. Keyfields include `squadname` and `hintgroup`.

### The player class — datamap `0x10580edc`, builder `FUN_1015af10`, 157 records, 10 inputs

| Input | Type | Calls | Handler |
|---|---|---|---|
| `GiveItem` | **STRING** | 126 | `LAB_10008760` |
| `AwardExperience` | **STRING** | 77 | `LAB_10006807` → `CVPlayer::InputAwardExperience` `FUN_1015f100` |
| `Whisper` | STRING | 9 | `LAB_100118a6` |
| `SetCriminalLevel` | INTEGER | 3 | `LAB_100063bb` |
| `RemoveCamera` | VOID | 3 | `LAB_10001c12` — real since 11.7: clears the map's one scripted camera |
| `PlayHUDParticle` | STRING | 1 | `LAB_1000ff6a` |
| `StopHUDParticle` | FLOAT | 1 | `FUN_1015f330` |
| `SetInvestigateLevel` | INTEGER | 0 | `LAB_10007748` |
| `SetSupernaturalLevel` | INTEGER | 0 | `LAB_10007fb3` |
| `Holster` | VOID | 0 | `LAB_100138ef` |

The dynamic record array begins at `0x10580f24`. Reconstructing all 157 records from
`FUN_1015af10` yields these **10 externally named input records** plus one superficially similar
`VOID` record: `CBasePlayerPlayerDeathThink`, no external name, flag `0x20`, callback
`FUN_101668b0`. That record registers a think function; it is not reachable through AcceptInput.
The earlier count of 11 inputs conflated the think callback with the ten records carrying input
flag `0x8`.

Two consequences for the port:

- **`GiveItem` exists twice** — as a Character *method* (`10199100`) and as a player datamap
  *input* taking a string. A script reaching `pc.GiveItem(...)` resolves the method first
  (`__getattr__` tries the class method table before the datamap walk), so the input is the I/O
  wire's path and the method is the script's. Both must exist and they need not share an
  implementation.
- **`AwardExperience` takes a STRING, not an amount.** It names an entry the engine looks up
  (`vdata/system/Experience_Table.txt`), so 9.4 cannot model it as an integer add. The handler
  (`FUN_1015f100`) takes the variant's string when `fieldType == 2` and otherwise stringifies it,
  then calls `CVPlayer::AwardExperience` (`1015f630`): the key is refused if already in the
  `m_ExpList` give-once ledger, looked up (a miss awards *and appends* nothing), given the
  `Experience_Modifier` bonus above 2 XP, and handed to `AddExperience` (`1015f8b0`) — which
  divides by 100 and **keeps the remainder**. Full walk: `docs/vtmb/game_runtime.md` §3 → "XP & leveling".

## `G` and the save adapter

`G`'s method table (`0x1058f5d0`) holds exactly `ClearAll` (`1019b7b0`) and `keys` (`1019b7e0`) —
no `has_key`, which is why `G.has_key` falls past `Py_FindMethod` into the flag dict and reads
integer `0`. The corpus writes **1,226 distinct `G` flags across 3,782 assignments**.

The restore adapter is binding/save mechanism, not an action name; its recovered contract is
`docs/vtmb/python_bridge.md` → "Reproduction."

Implementation priority and status for this action inventory: `docs/project/roadmap.md`.
