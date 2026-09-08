# VtMB game runtime — main loop, state, and the opening flow

How *Vampire: The Masquerade – Bloodlines* actually runs: the frame loop, the
three-layer split (engine / game DLL / Python), what state the game keeps and where
it lives, the character sheet and RPG rules, the New Game → chargen → trial → tutorial
sequence, and the dialogue/subtitle system.

This is the **integrating overview** — the mechanics it references live in the sibling
docs; this one ties them together and adds the loop, the state model, the RPG data, and
the opening sequence:

- `docs/vtmb/python_bridge.md` — the CPython 2.1 embedding, the datamap reflection API, the
  five call paths, `G`.
- `docs/vtmb/entity_io.md` — the Source I/O bus (7-field outputs, `ScriptHide`/`Unhide`, the
  usable set, `use_icon`).
- `docs/vtmb/level_transitions.md` — the map chain and the three spawn mechanisms.
- `docs/vtmb/dice-system.md` — the World-of-Darkness d10 resolution.
- `docs/vtmb/audio_pipeline.md` — the sound engine (Miles mixer, MS-ADPCM/MP3 codecs),
  the bespoke SoundScheme ambience/music system, DSP rooms.
- `docs/vtmb/animation_and_movers.md` — skeletal `.mdl` v2531 animation (bones, the RLE
  keyframe tracks, skinning) and brush movers (doors/buttons/spinners).

Evidence is tagged where it matters: **[VtMB]** = read from the user's own DLLs
(strings/symbols/addresses); **[SDK]** = Source SDK 2013 reference; **[data]** =
plain-text game data; **[script]** = a level `.py`; **[inferred]** = reasoned, not yet
decompiled; **[live]** = observed in an interactive retail run/capture. Retail image bases:
`engine.dll` `0x20000000`, `vampire.dll` `0x10000000`,
`client.dll` `0x10000000`, `vampire_python21.dll` `0x1e100000`.

## 1. Three layers and the main loop

VtMB is **stock early-Source** (a pre-tick 2003-era branch). Three layers, one rule:
**the engine is plumbing, `vampire.dll` is the whole game, Python is data-driven story
logic only.**

| Layer | Binary | Owns |
|---|---|---|
| Engine | `Bin/engine.dll` | host loop, render, sound, physics-lib load, save-file I/O, the CPython VM boot |
| Game (server) DLL | `Vampire/dlls/vampire.dll` | entities, think, entity I/O, movement, AI, RPG rules, dialogue eval, camera, and the **Python dispatch** |
| Client DLL | `Vampire/cl_dlls/client.dll` | HUD, `use_icon` table, view/input feel |
| Menu/UI | `client.dll` main/pause/HUD; `GameUI.dll` dialogs | two shipped UI stacks; full ownership: `docs/vtmb/vtmb-ui.md` |
| Script VM | `Bin/vampire_python21.dll` | stock CPython 2.1 — runs the story scripts |

### The host frame

`engine.dll` owns the classic Source `_Host_RunFrame` pipeline (all present as literal
profile-scope strings **[VtMB]**): `CEngine::Frame` → `_Host_RunFrame` `0x2008e450`, with
fixed sub-stages **Input → Server → Client → Sound → Render**. The **Server** stage runs
`SV_Frame` `0x200f62b0` → the game DLL's per-frame entry.

The engine queries `vampire.dll` by versioned interface string **[VtMB]**:
`ServerGameDLL002`, `ServerGameClients002`, `ServerGameEnts001` (matching classes
`CServerGameDLL`/`CServerGameClients`/`CServerGameEnts` exported with RTTI). One loader
`0x200faef0` caches all three: `serverGameDLL` `0x21300c88`, `serverGameEnts` `0x21300c8c`,
`serverGameClients` `0x21300c90`. The per-frame server entry is
**`IServerGameDLL::GameFrame(bool simulating)`** **[SDK eiface]** =
`CServerGameDLL::GameFrame` **`0x1011abc0`** **[VtMB]**, reached through `serverGameDLL`
vtable **`+0x14`** (slot 5). (`0x10571fc0` is that function's profile-scope *string*, not
its code.) `simulating` is false when paused / no player — the port's "advance world state
this frame or not."

### The server stage in order — usercmds run *before* the think pass

**Player movement is not part of `GameFrame`.** It runs earlier in the same server stage,
while the engine drains the client's move message. The chain is verified end to end
**[VtMB]**:

| # | Step | Address |
|---|---|---|
| 1 | `_Host_RunFrame` opens the **Server** profile scope | `0x2008e450` |
| 2 | `CL_Move` — builds and sends this frame's `CUserCmd` | `0x20027aa0` |
| 3 | `SV_Frame` | `0x200f62b0` |
| 4 | `SV_ReadPackets` — per-client packet loop | `0x200f2b10` |
| 5 | `SV_ExecuteClientMessage` — `clc_*` dispatch | `0x200f9cf0` |
| 6 | the `clc_move` handler | `0x200f9990` |
| 7 | `serverGameClients->ProcessUsercmds` (vtable `+0x20`), call site `0x200f9b2d` | `0x1011c8c0` |
| 8 | `CBasePlayer::ProcessUsercmds` — player vtable `+0x734` | `0x1016aaf0` |
| 9 | `PlayerRunCommand` per command — player vtable `+0x738` | `0x10351090` |
| 10 | `CPlayerMove::RunCommand` on `g_PlayerMove` `0x10939f00` | `0x101874a0` |
| 11 | *back in `SV_Frame`:* the game-frame driver | `0x200f7e40` |
| 12 | `serverGameDLL->GameFrame(simulating)` — thinks, then the queue | `0x1011abc0` |
| 13 | `SV_SendClientMessages` | `0x200f3c30` |

Step 2 is gated on the server being active (`0x212b0754`): when it is — singleplayer and
listen — `CL_Move` runs **inside** the Server scope immediately before `SV_Frame`, so the
command created this frame is consumed this frame; a pure client instead sends it in the
Client scope. `clc_move` is resolved through the message table at `0x201ac834`, records of
`{const char *name; void (*func)(); int type;}`, stride 12. Step 7's signature is Source's
own — `(edict_t*, bf_read*, numcmds, totalcmds, dropped_packets, ignore, paused)`, seven
pushed args returning `float` — and its two failure paths are the
`ProcessUsercmds: Incorrect reading frame` / `Overflowed reading usercmd data` messages.

`CBasePlayer::ProcessUsercmds` replays the dropped commands and then runs each new one
**synchronously**, in place: there is no pending-command queue and nothing defers to the
think pass. It closes by draining the entity delete list (`0x100f6ce0`).

`CPlayerMove::RunCommand` `0x101874a0` is stock Source, in this order **[VtMB]**:
`StartCommand` `0x10185b10` → `gpGlobals->frametime`/`curtime` **set from the command** →
`UpdateButtonState` `0x1016b090` → `CheckMovingGround` `0x10185f90` → **`RunPreThink`**
`0x10186f00` (which calls `CHL2_Player::PreThink` `0x10350830`) → **`RunThink`** `0x101871f0`
**[inferred — position + signature]** → `SetupMove` `0x10186120` → `CGameMovement`'s move
(`g_pGameMovement` `0x10627580`, vtable `+4`) → `FinishMove` `0x10186c10` →
`moveHelper->ProcessImpacts()` → **`RunPostThink`** `0x10187280` → `FinishCommand`
`0x10185d70`.

Two consequences a port inherits. The player's **own** think runs inside `RunCommand`, not
in the think pass. And `frametime`/`curtime` are rebound to the command's timing for the
duration of the move, then restored — so a move is simulated on the command's clock, not
the frame's.

**So the pawn is moved against last frame's mover positions.** Movers are `MOVETYPE_PUSH`
and their think runs later, in `Physics_RunThinkFunctions`; a mover that moves into the
player pushes him from its own side rather than the player being swept against the mover's
new pose.

### The `GameFrame` body

`CServerGameDLL::GameFrame` `0x1011abc0` is thirteen calls long, in this order **[VtMB]**:

| # | Call | Address |
|---|---|---|
| 1 | `IGameSystem` pre-entity-think pass **[inferred]** | `0x1042c6c0` |
| 2 | game-rules frame start **[inferred]** | `0x10352f00` |
| 3 | **`Physics_RunThinkFunctions(simulating)`** | `0x1003bdd0` |
| 4 | `IGameSystem` post-entity-think pass **[inferred]** | `0x1042c6e0` |
| 5 | a singleton `0x1072c474` → vtable `+4` | `0x101bdb50` |
| 6 | **`CEventQueue::ServiceEvents`** | `0x100cfac0` |
| 7 | two per-frame sweeps **[inferred]** | `0x10119980`, `0x1027ee90` |
| 8 | resolve the player, `dynamic_cast`, call vtable `+0x420` | inline |
| 9 | drain the entity delete list **[inferred]** | `0x100f6ce0` |
| 10 | `UpdateAllClientData` — loop 1..`maxClients` **[inferred]** | `0x1018be60` |

Steps 3 and 6 are RE2's think-first finding, now placed in the full body. `simulating`
reaches only step 3: false simulates **players only** (indices 1..`maxClients`), true walks
the whole entity list, both through `CBaseEntity::PhysicsSimulate` `0x1003bad0`.

### The entity think loop

Inside `GameFrame`, stock Source think dispatch, entirely in `vampire.dll` **[VtMB]**:
`Physics_BuildThinkLists` → `Physics_RunThinkFunctions` `0x1053b588` → per-entity
`CBaseEntity::PhysicsSimulate` → `PhysicsRunThink`/`PhysicsDispatchThink`. Thinks are
scheduled by the datamap field `m_flNextThink` `0x105548e4`; an entity re-thinks when
`curtime >= m_flNextThink`. `SetNextThink` `0x105571d8`, `RegisterThinkContext`
`0x10557194` (multi-context thinks, rare). A `think_limit` cvar and
`%s(%s) thinking for %.02f ms!!!` / `Dormant entity %s is thinking!!` are the vanilla
Source think guards.

### Time model — variable timestep, `curtime` clock

There is **no fixed tick** — `engine.dll` has *no* `interval_per_tick` / `sv_tickrate`
/ `TICK_INTERVAL` cvar **[VtMB]**; only `fps_max`, `host_framerate`, `host_timescale`,
and (networking-only) `sv_maxupdaterate`. This pins VtMB to the pre-tick branch: the
server `GameFrame` runs **once per rendered host frame** with a variable
`gpGlobals->frametime`. `gpGlobals->curtime` (absolute seconds) is the single clock:
think times, delayed-I/O fire times, and `ScheduleTask` deadlines are all absolute
`curtime` values; `host_timescale`/`host_framerate` scale how fast it advances (slow-mo).

> The SDK 2013 `globalvars_base.h` has `tickcount`/`interval_per_tick` — those are the
> **later** branch. Do not assume VtMB has them. (Same trap class as the 104-byte
> `dface_t`.)

### How Python is driven — event-driven, one event queue

**Python is never per-frame-ticked.** There is no `frame()` callback. Scripts run only
when something fires them, through the paths in `docs/vtmb/python_bridge.md` (output
field 6, `logic_pythoncheck`, dialogue field 4/5, a quest `CompletionState`'s `Event`,
`ScheduleTask`).

The servicing mechanism is **Source's `CEventQueue`, extended to carry Python** **[VtMB]**.
`vampire.dll` has the vanilla event queue (`debug_dump_eventqueue`,
`EventQueue Dump: (CurTime: %f) …`) — the same structure that services **delayed
entity I/O** (every output's `delay` field). VtMB added Python entry types to it:

- `RUNNING PYTHON AT TIME %f:` `0x1055e338` — a Python entry firing.
- `__main__` `0x1055e360` and the wrap format **`__main__.%s`** `0x1055e370` — a call
  string `journalPickup()` becomes `__main__.journalPickup()`.
- `Invalid python entry in the event queue.` — the type-check on service.
- `ERROR: Failed to match Discipline Event in the EventQueue!` — RPG disciplines also
  schedule timed events here.

`ScheduleTask(delay, "<source>")` is confirmed end to end: PyMethodDef body `FUN_10196ea0`
parses the two arguments and calls queue adapter `FUN_100ce0d0`; service executes the stored string
through `FUN_100ce8a0` against live `__main__`. **Delayed I/O, `ScheduleTask`, and timed disciplines
all converge on this one time-ordered queue.**

> The `__main__.%s` format string lives in **`vampire.dll`** (`0x1055e370`, inside the
> event-queue region) **[VtMB]**, where field-6 and `ScheduleTask` dispatch belong.
> `engine.dll` owns only the VM boot (`Py_Initialize`, search path `\vampire\python`);
> game-logic dispatch is server-DLL-owned.

### Queue service order, recursion and starvation

Queue insertion is sorted by absolute `fireTime` and stable for ties: the insertion walk continues
while an existing deadline is `<=` the new deadline. Service then drains the due head repeatedly.
For one event it performs, in order:

1. resolve its target-name expression and call `AcceptInput` on **all** matching entities in global
   entity-list order;
2. execute its field-5 Python call, if present;
3. call a direct `EHANDLE` target, if present and still valid;
4. remove the serviced event and continue at the head.

Missing targets/inputs and Python errors are non-fatal. Stale activator/caller handles resolve null,
so name delivery and Python still run; an invalid direct target is skipped. A handler's newly queued
zero-delay event is placed after the equal-time cohort already pending. Thus due `A, B`, where `A`
queues `C`, resolves `A, B, C`: same-frame recursive drain, but breadth-first FIFO rather than
depth-first recursion. The reverse ordering of repeated output rows happens earlier, when output
actions are parsed and fired; `docs/vtmb/entity_io.md` owns that rule.

Entity death is not a general queue rollback. A named event already posted by a trigger still
resolves after that trigger dies, with null caller if the handle is stale. A targeted cancellation
path exists for the special typed discipline-event lookup, but this pass found no blanket retail
"cancel everything by dead caller" rule for ordinary output actions.

Retail has **no normal per-frame event budget**. Its debug pause/single-step state can deliberately
stop service after a selected number, but gameplay service otherwise runs until the head is in the
future. A zero-delay cycle can therefore monopolize or hang the server frame, starving rendering,
later world work, and every delayed event. A future event cannot age into eligibility during that
drain because `curtime` does not advance inside it. `logic_relay`'s ordinary refire lock and
`trigger_multiple`'s wait suppress common accidental loops, but fast-retrigger relays and arbitrary
entity cycles remain capable of this failure.

The queue is serviced even when `GameFrame(simulating)` is false; that flag limits the earlier
entity-think walk, not step 6. Deadlines still use game `curtime`: host time scaling changes their
real-time duration, and a global pause that stops `curtime` freezes them. Enqueue also guards a
backward clock jump (`CEventQueue::AddEvent`, `vampire.dll FUN_100ce210`): if current `curtime` is
below the last observed enqueue time, the new deadline is shifted forward by
`(lastCurtime - curtime) + 0.005` rather than being inserted spuriously in the past — distinct from
`logic_relay`'s separate 0.001 re-fire lockout. Pending entries themselves are save/restore state.

**The Python "tick point" for a port:** drain this single time-ordered event queue inside the server
step, evaluating any script/I/O entry whose time is at or before current game time. Everything else
Python-side is call-triggered and needs no separate scheduler.

### Responsibility matrix

| Subsystem | Owner | Evidence |
|---|---|---|
| Host loop / frame pacing / timing cvars | engine.dll | `_Host_RunFrame*`, `CEngine::Frame`, `fps_max`/`host_timescale` **[VtMB]** |
| Rendering (world/models/lightmaps/decals) | engine.dll (+ client.dll HUD/FX) | `CRender::FrameBegin/End`, `VModelInfo` **[VtMB]** |
| Physics / collision | `VPHYSICS.DLL` (loaded by engine); vampire.dll owns movetypes/think-driven physics | `VPhysicsCollision007` **[VtMB-engine]**; `CBaseEntity::PhysicsSimulate`, `CPhysicsHook` **[VtMB-vampire]** |
| Player movement | vampire.dll (`CGameMovement`) | `sv_stepsize`, `speed_runbase` **[VtMB]**; ported — `docs/vtmb/source_movement.md` |
| Sound | engine.dll (`S_*`/`SNDDMA_*`), triggered by server | `_Host_RunFrame_Sound` **[VtMB]**; `ambient_generic`/`PlaySound` **[data]** |
| Entity spawning & datamap | vampire.dll | `CBaseEntity::Spawn`, datamap build `FUN_100a22f0`, `GetDataDescMap` vtable+0x148 **[VtMB/doc]** |
| Entity I/O dispatch (delays) | vampire.dll `CEventQueue` | `EventQueue`, output field 3 **[VtMB/doc]** |
| Think scheduling | vampire.dll | `SetNextThink`/`m_flNextThink`/`Physics_RunThinkFunctions` **[VtMB]** |
| AI / NPC / nav | vampire.dll | `npc_VHumanCombatant` 28-input surface **[doc]** |
| RPG rules (dice/combat/disciplines/feats) | vampire.dll (compiled), parameterised by data | `Failed to match Discipline Event` **[VtMB]**; `CalcFeat`/`BumpStat` **[doc]**; `docs/vtmb/dice-system.md` |
| Quests | vampire.dll state + Python logic | `SetQuest`/`GetQuestState` **[doc]** |
| Dialogue | vampire.dll eval, `.dlg` data, Python expr half | `CDialog::GetStartingLine` `0x100e0b10`, `CDialogDependency::TestPython` `0x100e9ff0` **[VtMB]** |
| Camera (cutscene/track) | vampire.dll entities with thinks | `CCameraTrackTrackThink`, `RestoreCameraToPlayerControl` **[VtMB/doc]** |
| Story flags `G`, deferred tasks | vampire.dll-owned, Python-facing | `m_pPythonObject`, `ScheduleTask` **[VtMB/doc]** |
| Story/level scripts | Python (loose `.py`) | `worldspawn.levelscript` **[doc]** |

## 2. State: what the game tracks, and where it lives

Runtime state sits in **three surfaces**, each persisted by a different save block (§save):

| Surface | Shape | Holds | Persisted by |
|---|---|---|---|
| **Character-sheet fields** | datamap fields on the player `Character` entity | the numeric sheet: attributes, abilities, disciplines, blood, health, humanity, XP, money, clan, gender | per-field entity save (`FTYPEDESC_SAVE`) |
| **Quests** | string→int map on the engine | quest progress (`SetQuest`/`GetQuestState`), 113/101 call sites | entity save |
| **`G`** | flat, mostly-int flag-bag, Python-facing | narrative/world state (~900 flags across scripts+dialogue) and cached/derived values | pickled as one blob (`CPyObjStrSaveRestoreDataOps`) |

Rule of thumb: **numbers live on the player entity; story lives in `G`; progress lives
in quests.** `G` holds *booleans and small counters*, not the sheet. 208 of the 345 `G`
flags level scripts *read* are written only by dialogue (`docs/vtmb/python_bridge.md`) — so
scripts and dialogue are one coupled system.

### Save / load — block-structured

Stock Source save-restore: the **file** is written by `engine.dll` (`CSaveRestore`,
`SAVE/*.sav`, `Quick Save`/`Autosave`, `svc_restore` **[VtMB]**), its **content** is a
set of `ISaveRestoreBlockHandler` blocks each contributed by `vampire.dll` **[VtMB]**:

- **Entities** — each live entity's datamap `FTYPEDESC_SAVE` fields (the numeric sheet
  rides here, on the player entity).
- **Physics** — `CPhysSaveRestoreBlockHandler`.
- **Event queue** — `CEQ_SaveRestoreBlockHandler` `0x1055e5d4`: pending delayed I/O
  *and* pending Python/`ScheduleTask` events survive a save.
- **Think contexts** — `CThinkContextsSaveDataOps`.
- **Python `G`** — `CPyObjStrSaveRestoreDataOps` `0x1055e608`: `G` marshalled/pickled to
  a string (the game ships `pickle.py` + `copy_reg.py`).

**Port implication:** the save is not monolithic. A faithful port serialises (a) each
entity's registered save-fields, (b) the pending event/timer queue, (c) per-entity think
contexts, (d) the `G` namespace as its own dynamic keyed blob (it can hold the corpus's
3 lists / 1 dict / 3 strings, so not a fixed struct).

## 3. The character sheet & RPG data model

The rules ship as plain-text KeyValues under `vdata/system/` **[data]** (patch copy is
the truth; `stats.txt` == `stats - vampire.txt`; the `* - hunter.txt` set is the cut
Hunter mode — ignore).

### The sheet — `stats.txt`

Root `StatData` → four `Stat` containers. Each `Stat` has `InternalName` (the engine
key — **`BumpStat`/`getattr` are case-insensitive**), `Min`/`Max`/`Default`, an optional
`NameMapping` (→ a display table in `strings.txt`), and a `Costs { New / Raise }` buy
formula.

- **Attributes** (9; Min 0/Max 10, **MinSell 1/MaxBuy 5**, Default 1;
  `New 1`, `Raise = Current_Rating * 4`):
  `Strength, Dexterity, Stamina` · `Charisma, Manipulation, Appearance` · `Perception,
  Intelligence, Wits`. Physical/Social/Mental is an ordering enum (`Attrib_Order`), not
  nested blocks. Charisma/Manipulation/Appearance carry `"Disabled" "1"` (exposed but
  flagged; Appearance still feeds Seduction/Nosferatu).
- **Abilities** (12; Min 0/Max 5, Default 0; `New 3`, `Raise = Current_Rating * 3`):
  Talents `Brawl, Dodge, Intimidation, Subterfuge` · Skills `Firearms, Melee, Security,
  Stealth` · Knowledges `Computer, Finance, Investigation, Academics` ("Scholarship").
  Talents/Skills/Knowledges is `Ability_Order_Lookups`, not nested. Several disabled.
- **Disciplines** (**Min −1**/Max 5 — `Blood_Healing` alone caps at 1 — **Default −1**, no
  `MinSell`/`MaxBuy` of their own; `New 10`, `Raise = Current_Rating * 5`): `Animalism,
  Auspex, Blood_Healing, Celerity, Corpus_Vampirus, Dementation, Dominate, Fortitude,
  Obfuscate, Potence, Presence, Protean, Thaumaturgy` (+ Hunter-mode ones). Each has
  `*_Targeting*` sub-stats. A discipline a clan **cannot** learn reads **`-1`** (sentinel);
  owned-but-unraised reads `0`. The sentinel is what keeps it off the sheet — "Buying a
  dot" below. Selection, targeting, blood payment, active effects and teardown are owned by
  `docs/vtmb/disciplines.md`.
- **Active_Disciplines** — parallel `Active_*` stats holding the currently-toggled level;
  drive per-frame effects (e.g. `Active_Obfuscate` gates stealth-attack bonuses).
- **Derived / bookkeeping** (flat `Stat`s, `Raise 10000` = priced out of reach — **not** the
  engine's `30000` cannot-buy verdict, which no shipped stat carries): `Clan`
  (`NameFunc ClanNameFunc`), `Gender` (0=F,1=M), `BloodPool` (0–15, Def 10;
  `BloodPoolEmptyFunc` on 0), `BloodPool_Max`, `Health`/`Max_Health` (Def 100),
  `Health_Aggravated_Dmg`, `Generation` (Def 0; drives three lookup tables),
  `Armor_Rating`, `Level` (1–3), `Humanity` (0–10, Def 7, `Raise = Current_Rating * 2`),
  `Masquerade` (0–5 **violation counter**, Def 0, 5 = game over), `Experience` (Def 0),
  `Soak_Pool`, `Frenzy_Check_Mod`, `Starting_Equipment`, `AutoLevel_Template`, …

**Divergences from tabletop VtM to record:** `Willpower` is present but fully commented
out; there are **no Virtues** (Conscience/Self-Control/Courage) and **no Backgrounds**
category anywhere. Money is its own field (`MoneyAdd`/`CurrentMoney`), not a `Stat`.

The four containers are **flat lists, and the engine addresses a trait by `(container, index)`
where index is the block's position in file order** — including the leading `*_Order` block,
which occupies index **0**. So `Attributes` is not the nine attributes: it is a **35-slot**
list running `Attrib_Order`(0), `Strength`(1) … `Wits`(9), then every derived/bookkeeping stat
through `Experience`(34). `Abilities` is **13** (`Ability_Order` at 0, `Brawl`(1) … `Academics`(12));
`Disciplines` and `Active_Disciplines` are **17** each and carry no order slot, so `Animalism` is 0.
The last four discipline slots — `Shield_of_Faith`(13), `Divine_Vision`(14), `Holy_Light`(15),
`Mind_Shield`(16) — are the **Numina** powers, and they are authored in `stats.txt` proper (which
is byte-identical to `stats - vampire.txt`), not only in the cut Hunter variant. The **save array
is 13** (`docs/vtmb/savegame_format.md`), so the file's slot space is wider than the sheet's.

### How a trait is addressed — `CVStatList_t` / `CVStatRef` **[VtMB]**

Every `CBaseCombatCharacter` carries the container array at `+0x13BC` (count) / `+0x13C0`
(`CVStatList_t*`), each list tagged with its category at `+0x10`: **0** Attributes, **1**
Abilities, **2** Disciplines, **3** Active_Disciplines. **4** is a fifth category that exists
only in the reference/effect namespace, not as a list: **Feats**.

Each list holds two parallel int arrays, and **both are datamap-exposed and saved** — the
current value under the bare name, the base under a `base_` prefix:

| Array | Offset | Slots | Datamap sample |
|---|---|---|---|
| `m_iVAttributesBase` | `+0x10F0` | 35 | `base_attrib_order`(0), `base_stamina`(3), `base_vmax_health`(17), `base_experience`(34) |
| `m_iVAttributesCurrent` | `+0x117C` | 35 | `attrib_order`, `stamina`, `vmax_health`, `experience` |
| `m_iVAbilitiesBase` / `Current` | `+0x1210` / `+0x1244` | 13 | `base_ability_order`(0), `base_brawl`(1) |
| `m_iVDisciplinesBase` / `Current` | `+0x1280` / `+0x12B4` | 13 | `base_animalism`(0) |
| `m_iVActiveDisciplinesBase` / `Current` | `+0x1310` / `+0x1354` | 13 | `base_active_animalism`(0), `active_animalism`(0) |

Five datamap external names are known to diverge from the `stats.txt` `InternalName`: `intimidate`
↔ `Intimidation`, `computers` ↔ `Computer`, `base_gender_` carries a trailing underscore, and
`Max_Health`(17) reads `vmax_health` — a `v` prefix that keeps the trait clear of
`CBaseEntity::m_iMaxHealth`, which is a separate keyfield on the same entity. `HealthBuffer`(25)
reads `health_buffer`; its base spelling is `base_health_buffer`. **Inferred, not
read:** `Health`(15) has the identical collision with `m_iHealth` and its datamap name was not
sampled; the runtime takes `vhealth` from the slot-17 pattern, and a later decompile pass should
confirm or correct it.

A Hammer keyvalue / `__getattr__` walk resolves the *datamap* name; `BumpStat` and the
`feats.txt` `Base%d` keys resolve the *`stats.txt`* name — two spellings, one slot.

Accessors (`CVStatList_t`, named by their own scope-trace markers):

| Method | Address | Semantics |
|---|---|---|
| `GetBase(i)` | `10200CA0` | the raw base array value, unmodified |
| `GetCurrent(i)` | `102012D0` | `base` → trait effects → clamp to effective max → clamp up to min |
| `IncBase(i)` | `10200D60` | `+1`, refused unless the stat's `IncPredependency` holds **and** `base < effective max` |
| `AddBase(i, d)` | `10200FC0` | `+d` **written raw** — gated only by `IncPredependency`, which a **negative `d` bypasses** |

**`AddBase` does not clamp and never reads the max** (decompiled): the gate is the predependency
alone, and the base is written as `base + d` whatever that comes to. So a `HumanityAdd 99` at 7
leaves the **base at 106** while every reader sees the clamped current 10, and the next `-1` walks
back to 105, not 9 — the overflow is banked in the base. Only `IncBase` tests the ceiling, because
its `+1` is a dot being bought rather than a counter being moved.

The change callback each fires differs with it: `IncBase` passes the old and new **base**,
`AddBase` passes `GetCurrent` sampled before and after the write. That is how `Health`'s
`Max "Max_Health"` and the `IncPredependency` expressions (`"Health < Max_Health"`) stay live.

**`CVStatRef` is the one trait-name resolver** (16 bytes; `10204570`) shared by `BumpStat`,
`traiteffects000.txt`, the `feats.txt` `Base%d`/`Automatic%d` keys and the stat `Min`/`Max`
cross-references. It strips a trailing ` / N` or ` * N`, then resolves the name against the four
stat containers, the feat table (category 4), an item table, the `TraitFxStrs` `Fx_*` enum, and
finally the hardcoded `"ObfuscateCanInc"` (category 9, a function-backed trait). `CVStatRef::Apply(v)`
(`10204C20`) is the arithmetic: the multiplier at `+0xC` is `1` for identity, **positive N = `× N`,
negative N = `÷ |N|`** (integer). That is what makes `"Base0" "Armor_Rating / 2"` work.

### Health is a damage counter, not hit points **[VtMB]**

**Nothing derives health from Stamina.** `Max_Health` is an ordinary stat — Attributes index
**17** (`vmax_health`), `Min 1 / Max 99999 / Default 100`, `Raise 10000` (unbuyable) — and no
formula key exists in `stats.txt` at all. The player gets the `Default`, a flat **100** for the
whole game: `clandoc000.txt`'s four `Max_Health` lines are all commented out, neither
`levelingtemplate_000.txt` nor `histories000.txt` mentions it, and **no** trait effect in
`traiteffects000.txt` targets `Max_Health` or `Health` (85 distinct traits are targeted; neither
is among them). Stamina's actual job is soak — it is `Base1` of `Soak_vs_Bashing` and
`Soak_vs_Bashing_Kindred`, `Base2` of `Soak_vs_Lethal_Falling_Kindred`, and the target of 20
trait effects.

**`Health` (index 15) counts damage taken**, not health remaining: `Default 0`, `Min 0`,
`Max "Max_Health"`. `Health_Aggravated_Dmg` (16) is the second damage pool with the same bounds,
and `HealthBuffer` (25, Max 32000) the over-cap pool. `CBaseCombatCharacter::HealthToPercent`
(`1032FE60`) projects the pair onto Source's engine-space health:

```c
(( GetCurrent(Attributes, 17) - GetCurrent(Attributes, 15) ) * m_iMaxHealth)
    / GetCurrent(Attributes, 17)
```

with `CBaseEntity::m_iMaxHealth` at `+0x208` and `m_iHealth` at `+0x210`.

**An NPC's health track is authored, not derived.** Each `npctemplate*.txt` `ClanData.Attributes`
block can set `Max_Health` literally, in the same flat container as `Strength`…`Wits`; an absent
key follows `ParentTemplateName` and ultimately the `stats.txt` default 100. Across the 150
patch-first declarations, 114 are explicit, 23 inherit a literal and 13 default. Effective pools
span 1 (`Scurrying`, inherited by `Rat`) through 1400 (`MingXiao`); ordinary examples include
`NPCGeneric` 22, `WarehouseThug` 60 and `OfficerGeneric` 100, while `Bach` is 440,
`SheriffMan` 570, `Gargoyle` 800, `Tutorial_Jack` 819, `ManBat` 880 and `Hengeyokai` 968.
`research npc_health_census` reproduces the complete resolution without committing game data.

`BloodPool` is a different Attributes resource. Feeding, vampire healing and Disciplines may
consume or change it, but the ordinary lethal comparison does not substitute it for health:
death is selected when accumulated `Health >= Max_Health` after the alive damage transaction.

### Feats — the derived-roll layer (`feats.txt`)

How dialogue, lockables, feeding and combat consume a feat rating — including which paths roll
and which only compare — is consolidated in `docs/vtmb/skills-and-checks.md`.

`.dlg` skill-checks and verb rolls are **Feats**, not raw stats. A Feat sums a **variable-length
`Base%d` list** and is read via `pc.CalcFeat("<feat>")` (case-insensitive), each naming a
`"Normal"` dice weighting table (where `docs/vtmb/dice-system.md`'s d10 resolver plugs in):

| Feat | = Attribute + Ability | Feat | = Attribute + Ability |
|---|---|---|---|
| Lockpicking (`Intrusion`) | Dexterity + Security | `Persuasion` | Charisma + Academics |
| `Sneaking` | Dexterity + Stealth | `Seduction` | Appearance + Subterfuge |
| `Hacking` | Wits + Computer | `Intimidate` | Intelligence + Intimidation |
| `Inspection` | Perception + Investigation | Unarmed (`Close_Combat_Brawl`) | Brawl + Strength |
| `Research` | Academics + Intelligence | Melee (`Close_Combat_Melee`) | Melee + Strength |
| `Haggle` | Finance + Manipulation | Ranged (`Ranged_Combat`) | Firearms + Perception |

So a `.dlg` gate "Persuasion 7" means `CalcFeat("Persuasion") >= 7`.

**A dialogue check carries a sex gate** **[VtMB]**. `CDialogDependency::TestSimple` (`100E9760`)
opens by rejecting the line when a per-dependency field at `+0x224` disagrees with
`CBaseCombatCharacter::IsMale` (`10336920`): the field holds **1 = male-only**, **0 = female-only**,
and anything else leaves the check ungated. In the data that field comes from an `M_`/`F_` prefix on
the check name — `M_Persuasion 3`, `F_Seduction 8` — which is a *gate*, not part of the trait name:
`F_Seduction` is not a feat, a stat or an item, and `cal.dlg` authors the same beat twice, once per
sex, with different lines. 36 conditions across the corpus use it (`F_Seduction` 22, `M_Seduction`
5, `M_Persuasion`/`F_Persuasion` 4 each). The evaluator also proves the check space is wider than
the feat table: the same function reads trait values through `GetCurrent` for the discipline and
stat checks (`Dominate` 102 sites, `Humanity` 272) that never enter `Feats::FeatValue` at all.

The two-name table above is the **common** shape, not the schema. The shipped file holds **23
feats** and the base list is counted by probing `Base0`, `Base1`, … until a key is absent, so it
is genuinely variable: `Soak_vs_Bashing` has three bases (`Armor_Rating` + `Stamina` +
`Soak_Pool`), `Soak_vs_Aggravated` one, `Soak_vs_Lethal_Falling` uses `"Armor_Rating / 2"`, and
`Damage` and `Frenzy_Feat` have **none** — they are pure-code feats. Ids are file order:
`Intrusion`(0), `Sneaking`(1), `Hacking`(2), `Inspection`(3), `Research`(4), `Haggle`(5),
`Intimidate`(6), `Persuasion`(7), `Seduction`(8), `Close_Combat_Brawl`(9),
`Close_Combat_Melee`(10), `Ranged_Combat`(11), `Defensive_Maneuvers`(12), the eight
`Soak_vs_*`(13–20), `Damage`(21), `Frenzy_Feat`(22).

#### What a feat evaluates to — `Feats::FeatValue` **[VtMB]**

`CalcFeat` (`10198CC0`) matches the name case-insensitively against the feat table, then returns
`PyInt_FromLong(Feats::FeatValue(feat, character))` (`101E56E0`) — **a plain int, the feat
*rating*. It rolls no dice.** An unknown name dumps `Feat %d = %s` for ids 0–12 to the console and
raises `AttributeError("invalid feat name -- %s")`; a non-character raises
`"invalid combat character"`.

```
cap = feat.MaxValue                      // KeyValues "MaxValue", default 10
r   = 0
for each base ref in feat.bases:         // +0x1C count / +0x20 array, stride 0x10
    if ref.category == 0:                        // an Attributes-container trait
        v = GetCurrent(attributes, ref.index)
        v = ref.Apply(v)                         // the "/ 2" or "* N" modifier
        if v < 1 and ref.index < 10: v = 1       // floor of 1 — exactly the nine attributes
        r += v
    elif ref.category == 1:                      // an Abilities-container trait
        r += ref.Apply(GetCurrent(abilities, ref.index))
    // any other category (a "None" base) contributes nothing
if feat.id == 1:                                 // Sneaking
    r += CBaseCombatCharacter::GetStealth…()     // 1032FAB0
if feat.id in (9, 10, 11):                       // Brawl / Melee / Ranged
    r += GetPresence…() (10323210) - GetShakyHa…() (103232B0)
r = ApplyTraitEffects(m_tEffectList, category 4 /* feats */, feat.id, r)
return clamp(r, 0, cap)                          // negative → 0
```

So the "pool" is the **sum of the base list, each entry read as the current (effect-modified,
min/max-clamped) trait value through its own `/`-or-`*` modifier** — and the sum is itself run
through a second, feat-level trait-effect pass before clamping. The nine attributes floor at 1;
abilities and the derived stats do not.

The Sneaking-only modifier is the balanced `trigger_stealth_mod` aggregate; its clamp and detection
consumer are documented in [stealth.md](stealth.md).

The `PCWeighting` / `NPCWeighting` keys are resolved **at load** into an index into the global
DiceRolls table array (`101D9780`, stride `0x19C`, name at `+4`, **0 on miss**), stored at feat
`+0x34` / `+0x38`. `dicerolls.txt` ships three — `Normal`(0), `Heavy`(1), `Light`(2) — and all 23
feats name `Normal` for both, so the miss-fallback is `Normal` too. **The rating `CalcFeat`
returns is the pool size handed to that table**; the resolver is `recovered/dice-system.md`'s.

Two more loader-key families sit beside the bases and are *not* summed into the rating.
`Automatic%d` names automatic-success traits: `Close_Combat_Brawl` and
`Close_Combat_Melee` both set `"Automatic0" "Automatic_Str_Successes"`. `Display2nd%d` is
also authored on those two feats and on all eight soak feats, pointing at the corresponding
automatic Strength or soak trait. The common damage resolver reads the current automatic traits
separately; the exact presentation consumer of `Display2nd%d` is not yet recovered.

**Open residue.** For feat ids 9–20 (the four combat feats and the eight soak feats)
`Feats::FeatValue` consults five global object pointers — `0x1073A104` Brawl, `0x10739BDC` Melee,
`0x10739CB4` Ranged, `0x10739C6C` Defensive_Maneuvers, `0x10739C24` all eight `Soak_vs_*` — as
`if (!obj->vtable[1]() && obj->+0x2C >= 1) rating = obj->+0x2C`. The shape is Source's `ConVar`
(`m_nValue` at `+0x2C`, `IsCommand()` at vtable slot 1), i.e. a dev override; each pointer is
**null in the shipped image with zero writes anywhere in the disassembly** (three read-only
references each), so the owner is unidentified. Next step: `MakeFuncs` over `vampire.dll`, then
re-xref. No `vdata` key configures it.

### The 7 clans (`clandoc000.txt`)

`Player_Brujah/Gangrel/Malkavian/Nosferatu/Toreador/Tremere/Ventrue`. All share
`BloodPool 8`, `Generation 10`, every attribute 1 / ability 0 at base; they differ by
discipline trio, attribute/ability priority order, and gift/bane:

| Clan | Disciplines | Advantage | Bane |
|---|---|---|---|
| Brujah | Celerity, Potence, Presence | +1 Unarmed; Presence in dialog | −2 all frenzy checks |
| Gangrel | Animalism, Fortitude, Protean | +5 Str/Wits/Sta in frenzy | −1 all frenzy checks |
| Malkavian | Auspex, Dementation, Obfuscate | Insight; +2 Inspection | Madness (unique dialog) |
| Nosferatu | Animalism, Obfuscate, Potence | extra rat blood | Seduction max 0; visage → Masquerade |
| Toreador | Auspex, Celerity, Presence | Humanity gains ×2 & cheaper; Presence in dialog | Humanity losses ×2 |
| Tremere | Auspex, Dominate, Thaumaturgy | Thaumaturgy; Dominate in dialog | no Physical attr above 4 |
| Ventrue | Dominate, Fortitude, Presence | Dominate & Presence in dialog | no rat blood; low-life feeding → vomit |

**One engine index, one display enum.** The engine index is `clandoc000.txt`'s own template
order — `0 Clear`, `1 Human`, `2 Player_Brujah` … `8 Player_Ventrue`, `9 mp-condotierre`,
`10 mp-inquisitor`, `11 mp-mercenary` — which is what `pc.clan` reports **[script]** and
what `rules_tables.txt`'s clan-keyed `Subpool_*` tables are indexed by (their per-row
comments name exactly those clans) **[data]**. `ClanNameFunc` (`strings.txt`, `1 Brujah …
7 Ventrue`) is a **display** enum and nothing else.

Banes and gifts are named as `ClanEffect`/`FrenzyEffect` trait-effects
(`traiteffects000.txt`) and enforced by the generic trait-effect layer, not by per-clan code
— see "Trait effects" below.

### Chargen — a personality quiz (`charcreatewizard.txt`)

Not a manual point-buy: the wizard asks `Popup` questions whose answers increment
abstract Traits (`Combat, Non-Combat, Social, Stealth, Intellect, Unarmed, Armed,
Ranged`), then scores each clan against the tallies to **auto-suggest a clan**. The whole
wizard lives in **`client.dll`**, not `vampire.dll` **[VtMB]** — the wizard's own logic
around `0x10141400–0x10145000`, the point-spend panel around `0x1017c000–0x10182500`.

**Clan scoring.** `Clan_Tables.ClanNode` gives each `Player_*` template its own
`Primary`/`Secondary`/`Tertiary` trait (a rank may repeat — Gangrel has two `Primary`s);
`ConnectionScores` is a **3×3 payoff matrix**, `Selection` *n* (the player's 1st/2nd/3rd
ordered trait) × the clan's rank for that trait:

| | clan Primary | clan Secondary | clan Tertiary |
|---|---|---|---|
| player's 1st | 3 | 1 | 0 |
| player's 2nd | 2 | 3 | 1 |
| player's 3rd | 1 | 2 | 3 |

A clan's score is the sum over the player's three selections; the loader is `FUN_10144840`
(reads `Clan_Tables` → `ClanNode`, then `ConnectionScores` → `Selection`, 3 × 3) **[VtMB]**.

**The sheet baseline is bought, not written.** `[CharGenWiz]: Spending Player Stat Points`
(`FUN_10144be0`) runs two console commands: **`giftxp 9000`**, then
**`vautolvl <template> 1`** with the clan's `CharGen_AutoLevel_Template`
(`Brujah_CharGen`, …) — so the starting spread in `levelingtemplate_000.txt` is applied by
the ordinary auto-level buyer against a deliberately huge XP grant, on top of the clan
template's flat `Attributes`/`Abilities`/`Disciplines` block **[VtMB]**. Only then does the
point-spend panel open.

**A clan's `Attributes` block carries symbolic values, not only ratings** **[data]**. Six of its
keys are names rather than numbers — `Attrib_Order` (`"Physical_Mental_Social"`) and
`Ability_Order` (`"Talents_Skills_Knowledges"`), which resolve through the stat's `NameMapping`
group in `strings_internal.txt` and are what select the tier split above; `Starting_Equipment` /
`Excluded_Equipment`; and `CharGen_AutoLevel_Template` / `Default_AutoLevel_Template`, which name
`levelingtemplate_000.txt` rows. The last two are **not** `stats.txt` slots at all — they live in
the `Attributes` block but have no trait behind them, so a reader that stores the block as
name → int loses them entirely and reads both orders as ordering 0.

**Two marked divergences in our chargen** — both are recorded beside the faithful
behaviour rather than silently taken:

- **Route 3 of the entry popup is omitted.** `Help_Popup0`'s third action — the Unofficial Patch's
  "replay some Bloodlines missions as a human hunter from the Society of Leopold" — leads to
  `Hunter_Selection`, whose `CharTemplate`s are the **multiplayer clans 9–11** (`mp-mercenary`,
  `mp-condotierre`, `mp-inquisitor`). The sheet's 2..8 clan encoding, the clan sigil set and the
  player-body lookup all stop short of them, so the route is filtered out of the popup rather than
  left to open a path that dead-ends. Reversible: the data is parsed and the filter is one name.
- **`AUTO-SPEND POINTS` is drawn disabled.** Its handler is not recovered, and the obvious
  candidate is not one: the clan's `<Clan>_CharGen` leveling template **is** the baseline and has
  already run by the time the pools exist, so no authored spend order remains to follow. A
  priority-order fill would be an invented rule, which the governing direction refuses by default.
  The button keeps its shape so the screen does not misreport what retail offers.

**The point pools** (`FUN_1017d930`, one pass at panel construction) **[VtMB]**. Seven
counters — Physical, Social, Mental, Talents, Skills, Knowledges, Disciplines — each built
as *clan term + tier term*:

1. **Clan term:** `rules_tables.txt`'s `Subpool_<name>` table indexed by the **clan
   template index** (§ "The 7 clans"). Shipped values are **0 for all six attribute/ability pools**
   in both retail and the patch — the file says so itself ("These aren't really used
   anymore (but *ARE* referenced in code)") — and **1 for `Subpool_Disciplines`** on every
   clan.
2. **Tier term:** for each tier *t* ∈ {0 primary, 1 secondary, 2 tertiary}, the category is
   `Attribute_Order_Lookups[Attrib_Order·3 + t]` (resp. `Ability_Order_Lookups`), and that
   category's pool gains `Subpool_Attribute_Primary_Secondary_Tertiary[t]` = **2 / 1 / 0**
   (resp. `Subpool_Ability_…[t]` = **3 / 2 / 1**). The **`_Kine` variants** (both **1 / 1 /
   1**) are used instead when the character template's `Kindred` key is 0 — the non-vampire
   templates. **These four tables live in `stats.txt`**, as nested `Table` blocks on the
   `Attrib_Order` / `Ability_Order` stats — only the clan term's `Subpool_<name>` tables are rows
   of `rules_tables.txt`. The two halves of a pool come from two different files.

So a shipped playable character gets **2/1/0 attribute dots** by category priority,
**3/2/1 ability dots**, and **1 discipline dot** — on top of the auto-levelled baseline.
The panel shows each as `"<category>: <n>"` (`FUN_1017f470`).

**A pool point buys a dot outright — the `Costs` model below is the experience path, not this
one.** The shipped numbers admit no other reading: an attribute raise is priced
`Current_Rating * 4`, so the 1 → 2 step alone costs 4 and a 2-point Physical pool could not buy a
single dot; a discipline raise is `Current_Rating * 5` against a pool of exactly 1. The pools are
denominated in dots, and the panel's `REMAINING POINTS` counts dots. *Confidence: inferred from
the shipped tables rather than decompiled; the spend handler in `client.dll`'s
`0x1017c000–0x10182500` panel block would confirm it directly.*

### Buying a dot — the cost model **[VtMB]**

`stats.txt`'s `Costs { New / Raise }` parses into a `CVStatCost_t` = two 20-byte
`CVStatSubCost_t` (New at `+0`, Raise at `+0x14`); `CVStatSubCost_t::Parse`
(`client.dll FUN_10159650`, `vampire.dll FUN_101fb980`) recognises three forms:

| Data form | Type code | Buy(*r*) | Sell(*r*) |
|---|---|---|---|
| `"Current_Rating * N"` | 9999 | `N·r` | `N·(r−1)` |
| `"Table: a, b, c, …"` | 9998 | `values[r]` | `values[r−1]` |
| a bare integer *k* | *k* | *k* | *k* (0 when *k* = 10000) |

**`Current_Rating` is the rating *before* the purchase**, and it is the stat's **base**
value (`CVStatList_t::GetBase`), not the buffed current one — settled by the buy/sell
symmetry: `Sell(r) ≡ Buy(r−1)`, so a dot always refunds exactly what it cost. Attributes at
`×4` therefore run 4, 8, 12, 16 XP for 1→2…4→5; abilities `×3` → 3, 6, 9, 12; disciplines
`×5` → 5, 10, 15, 20; Humanity `×2`. If only one of `New`/`Raise` is given, `CVStatCost_t::Load`
copies it into the other.

The gate around it (`FUN_10160e80`, called from `FUN_10161170`) is a `{kind, index}` stat
reference and returns **30000 = cannot buy** when the stat index is `-1`, when the kind is
outside `0 attributes / 1 abilities / 2 disciplines`, or when `current >= MaxBuy`.
Otherwise: **kinds 1 and 2 use `New` when `current <= 0`** and `Raise` above it; **kind 0
(attributes) never uses `New`** — which is why the attribute `New "1"` is annotated in the
data as a Nosferatu sell-back hack, reachable only through the sell path. `GetCostToSell`
additionally refuses (returns 0) below rating 2 unless called on the `New` tier.
`CVStatInfo_t::MinSell`/`MaxBuy` are `max(Min, MinSell)` / `min(Max, MaxBuy)`, and
`CVStatInfo_t::PostLoad` **defaults an absent `MinSell`/`MaxBuy` to `Min`/`Max`** — so
attributes clamp to buy ≤ 5 / sell ≥ 1 from their own keys, while disciplines inherit
`-1 … 5`.

**The discipline `-1` sentinel is a list filter, not a price.** A sheet section that sets
its by-value flag admits a stat into the displayed rows only when its current value is in
**`[0, 6)`** (`FUN_1017f870`, which also drops anything whose `CVStatInfo_t` carries
`HiddenInCharEditor` — a key **no shipped stat sets**). A discipline the clan cannot learn
sits at its `Default` of `-1`, so it never gets a row and can neither be selected nor
bought; the cost layer itself would happily quote it the `New` price, because its
`current <= 0` test does not distinguish `-1` from `0`.

The commented-out **`Raise_Clan_Discipline` / `Raise_Other_Discipline`** pair in `stats.txt`
was **never implemented**: neither string exists anywhere in `client.dll` or `vampire.dll`, and
`CVStatCost_t::Load` reads exactly two keys under `Costs` — `New`, then `Raise`. A per-clan
discipline price is expressed
instead as a **trait effect carrying its own `Costs` block** (below) — which is how the
Unofficial Patch reinstates the distinction in data.

### XP & leveling

- **Spend** — `Costs.Raise` per dot: attribute `×4`, ability `×3`, discipline `×5`, humanity
  `×2`, against the **pre-purchase base** rating (above).
- **`experience_table.txt`** is the **quest-reward → XP** map, not a cost table.
  `ExperienceTable::PreCache` (`10222300`) → `LoadFile` (`10222740`) reads it line by line: `>`
  starts a comment, lines under 3 characters are skipped, and `ParseLine` (`102228F0`) splits on
  `|` into exactly three fields — `Q_trimspace` on the key and the description, `atoi` on the
  value. A row is 12 bytes, `{char* key, char* desc, int value}`, and **the value is stored raw**;
  190 rows ship, keys distinct, longest 12 characters. The section headers'
  `> Total Experience Value:` comments agree with `sum(floor(v/100))` on only 36 of 68 sections —
  they document the base path (excluding mutually-exclusive and bonus rows) and three are simply
  stale, so they are advisory, not an invariant.
- **`pc.AwardExperience("<Key>")` / the player datamap input** (STRING, handler `LAB_10006807` →
  `CVPlayer::InputAwardExperience` `1015F100` → `AwardExperience` `1015F630`) **[VtMB]**:
  1. **Give-once is a ledger, not an encoding.** It walks `m_ExpList` (`+0x1D7C` memory /
     `+0x1D88` count, stride `0x34`) and `Q_strnicmp`s each stored key against the incoming one
     for `strlen(key)` characters; a hit returns immediately. **Every** key is give-once,
     unconditionally — the trailing `01` has nothing to do with it. (The prefix compare is latent
     breakage: a key that is a strict prefix of an already-awarded key would read as already
     given. The shipped table has **zero** such pairs.)
  2. A key the table does not hold awards nothing **and appends nothing**, so it retries on every
     fire.
  3. `value = row.value`, and **if `value > 299`** (≥ 3 XP) then
     `value += GetCurrent(Attributes, 29 /* Experience_Modifier */)`, floored at 100. This is the
     file's own "*the value without extra experience points*"; `Experience_Modifier` is a real
     trait-effect target.
  4. The key is appended to `m_ExpList` (`Q_strncpy` 48 bytes into the 52-byte slot; the trailing
     dword is not written by this path).
- **`CVPlayer::AddExperience(float, bool notify)`** (`1015F8B0`) is where the `/100` lives, **and
  it carries the remainder** — so `floor(value/100)` is confirmed, but the leftover hundredths
  persist across awards instead of being discarded:
  ```
  m_flLifetimeExp (+0x19F8) += v            // raw, never divided
  acc = (m_flExpRemainder (+0x19FC) += v)
  if (acc < 100.0f) return
  whole = (int)(acc * 0.01f)                // __ftol truncates
  m_flExpRemainder = acc - whole * 100.0f   // the sub-100 residue is KEPT
  AddBase(Attributes, 34 /* Experience */, whole)
  if (notify && whole > 0) NotifyExperience(this)
  ```
  Every real row is `N01`, so each award banks +0.01 XP of residue and one bonus point falls out
  per 100 awards. (The four non-`N01` values — `0`, `7`, `50`, `51` — are the file's own
  "Junk for testing" block.) `NotifyExperience` (`101CEBC0`) is the client-side notification,
  gated on `player->+0x1E00 == 0`, fired once before the award and again after when `whole > 0`.
- **`levelingtemplate_000.txt`** — ordered auto-level templates (NPCs + CharGen builds);
  the engine "buys up" listed traits in order as points allow. Driven by the `vautolvl`
  console command; a template's `LevelGroup` is selected by a `Dependency` expression over
  the sheet (`"Attrib_Order == Physical_Mental_Social"`) **[data]**.

### Quests — the catalogue, `SetQuest`, and the awards **[VtMB]**

The class is **`QuestJournal`** (its own scope-trace strings name it). The catalogue is a process
global loaded from the five `quests_*.txt` files; the *player* carries only the journal rows.

**The catalogue records.** A quest is 0x68 bytes: `+0` `Title`, `+4` `DisplayName`, `+8` the id of
the file it came from, `+0xc` `iOrder`, `+0x10` the current state (**-1 = unassigned**), and
`+0x14` an `int stateIdx[20]` prefilled with -1 — so **a quest is capped at 20 completion states**.
A `CompletionState` is 0x18 bytes: `+0` ID, `+4` `Description`, `+8` `Type`, `+0xc` `AwardMoney`,
`+0x10` `AwardXP`, `+0x14` `Event`. `QuestJournal::AddQuest` (`102213C0`) and
`AddCompletionState` (`10221660`) `Q_trimspace` every string they read, `Title` included.

- **`Type` is resolved by substring**, not by equality — `Q_stristr` in order: `incomplete` → 1,
  `success` → 2, `failure` → 3, `botch` → 4, anything else → 1. The key's default when absent is
  the literal `"incomplete"`. **`botch` is a fourth type no shipped row authors.**
- **The authored `"ID"` is never read.** `AddCompletionState` zeroes the record's ID slot and
  parses only `AwardMoney` / `AwardXP` / `Description` / `Event` / `Type`. The states are appended
  in file order, and `SetQuest`'s second argument indexes that array **1-based**: state `N`
  addresses `stateIdx[N-1]`. All 435 shipped rows happen to author `ID` equal to their file
  position, so the two readings coincide on retail data — but the mechanism is the ordinal.

**`pc.SetQuest(title, state)`** — Python thunk `10199800`, setter `CVPlayer::SetQuest` `1017CC20`.
The thunk parses `(self, title:str, state:int)` and raises `AttributeError("bad args to
SetQuest.")` on a mismatch, but **ignores the receiver**: it fetches entity index **1**, so the
quest always lands on the player no matter what the call was written against.

1. Look the title up in the catalogue. **An unknown title does nothing at all** — no state stored,
   no journal row, no award. Likewise a `state` whose `stateIdx[N-1]` is -1.
2. **`iOrder` is assigned once, on first assignment** (the quest's current state is still -1): a
   walk over every loaded quest takes `max(iOrder) + 1`, so the first quest assigned in a run
   gets **1**.
3. The catalogue record's current state is overwritten unconditionally.
4. **The re-fire gate** (`10182420`) reads the *journal row*, not the catalogue:
   - no row yet → proceed (a first assignment always awards);
   - a row at the **same** state → **stop**; a repeat `SetQuest` awards nothing and re-runs nothing;
   - a row at a **different** state → proceed, **including a move backwards** — unless the row's
     current state is `Type` **botch**, which raises `Error("Attempting to set botched quest %s to
     state: %d")` and stops. With no shipped row authoring `botch`, that branch is unreachable on
     retail data.
5. The awards then run **in this order**, all on the player: **`AwardMoney`**
   (`CBaseCombatCharacter::MoneyAdd` `10340E50` — a raw `+=` on `+0x13D8`, no floor at zero), then
   **`AwardXP`** (`AwardExperience` above, so the `m_ExpList` ledger is the second give-once
   guard), then **`Event`** — `PyRun_ConsoleString` against `__main__`'s dict with
   `Py_file_input`, traced as `RUNNING PYTHON AT TIME %f: %s` and `PyErr_Print`ed on failure. Each
   is skipped when zero/empty.
6. The journal row is written last (`10182260`), and on a `success` state the client notification
   fires a second time.

**The journal row** is `ASSIGNED_QUEST`, stride **0x40** at `player+0x1D68` with the count at
`+0x1D74`: `szTitle[0x30]`, `+0x30` `idxQuestTable`, `+0x34` `idxState`, `+0x38` `iOrder`, and
**`+0x3c` a byte flag set to 1 on every write** — the unread marker, a field
`docs/vtmb/savegame_format.md`'s record does not list. Two things the name `idxQuestTable` hides: the value
stored is the **flat quest index across all five loaded files**, not the index of the file; and the
row is matched by **`Q_strnicmp(title, 48)` — case-insensitively** — then **replaced in place**, so
a quest never holds two rows.

**Nothing in the image ever reads `+0x3c`.** The engine sets it and the panel that would clear it is
compiled in and undecompiled, so VtMB's own rule for when a quest stops being "new" is unrecovered.

**The quest log's selected hub tab is player state, not panel state**: `m_iCurrQuestLogArea`, an
integer datamap field on the player at **`+0x1dac`** (read out of the datamap builder in
`vampire.dll`). It persists with the character, so a save restores the tab the player was last on.

**`quests_main.txt` authors no quests.** The loader reads five tables, but the fifth ships with every
quest commented out — the file is the format's own documentation template. All 79 shipped quests
belong to the four hub tables, which is why the log needs only four tabs.

### Trait effects — how clan banes and histories are enforced **[VtMB]**

Clan gifts/banes and History backgrounds are **not** special-cased in code. A clan names
`ClanEffect` / `FrenzyEffect` (`clandoc000.txt`), a History names `Effect` plus a
`CritterScope` filter (`Kindred(Gangrel)`, `ALL()`) (`histories000.txt`); each resolves to a
`TraitEffectGroup` in `traiteffects000.txt` (RTTI `CVTraitEffectGroup_t`, present in both
`client.dll` and `vampire.dll`), and every `TraitEffect` in the group is one of three things:

- **A modifier on a stat** — `"Trait"` + `"Modifier"`. The operator vocabulary is *data*:
  `traiteffect.txt`'s `ModifierNames` is the enum the parser indexes —
  `0 +` (and `-`), `1 *`, `2 /`, `3 Max`, `4 Min`, `5 %`, `6 Value`, `7 Cost`,
  `8 BloodCost`, `9 Damage`, `10 Duration`. The parser (`FUN_101567e0`) matches names 1–10
  by prefix, skips the name and any spaces, then reads a signed integer — or, for
  `6 Value`, a **named** value (`"Value Knowledges_Talents_Skills"`,
  `"Value Clawed_Form"`). No name match ⇒ operator `0` and a signed `atoi`, which is what
  `"+1"` / `"-2"` take. An optional `DisplayOverride` key defaults to `-1`.
- **A cost override** — a `Costs { … }` block with no `"Modifier"`; the loader sets operator
  **7 (`Cost`)** and parses it as an ordinary `CVStatCost_t`, so `"Raise" "Table: 6, 8, 12,
  18, 24"` replaces that stat's price for that character.
- **A code-side effect flag** — `"Trait"` naming an entry of `strings_internal.txt`'s
  **`TraitFxStrs`** (18 entries: `Fx_Translucency`, `Fx_Supernatural_Level`,
  `Fx_Humanity_Mods_Doubled`, `FX_Increased_Rat_Feed`, `Fx_Cannot_Rat_Feed`,
  `Fx_Feed_Bonus_Opp_Gender`, `Fx_No_Resist_Feeding`, …), which engine code reads where the
  behaviour lives. The file's own comment names the header it mirrors,
  `game_shared\v_stat_effects_shared.h`.

The `"Trait"` name itself goes through the one resolver, `CVStatRef` — so besides a stat or
an `Fx_*` flag it can also name a feat, an item or a ConVar; see "How a trait is addressed"
above.

That covers every bane in the clan table: Tremere's physical cap is `Strength/Dexterity/
Stamina` `"Max 4"`, Nosferatu's is `Appearance`/`Subterfuge` `"Max 0"` plus
`FX_Increased_Rat_Feed +1`, Toreador's Humanity doubling is `Fx_Humanity_Mods_Doubled +1`
beside a `Humanity` `Costs.Raise` of `Current_Rating * 1`, and every clan's frenzy penalty
is `Frenzy_Check_Mod` `"-1"`/`"-2"` — a real `Stat`. `"Modify"` in the file's header comment
is stale: the key the code reads is **`Modifier`**.

#### The History index is also read raw — `vhistory` **[VtMB]**

The `Effect` / `CritterScope` path above is how a History changes *stats*. The chosen row's
**index** is separately readable, and three story flags are derived from it rather than from any
trait effect.

The index is `m_iVHistoryID` (the value `histories000.txt` position defines and the save's Player
block carries), exposed on the player datamap as **`vhistory`** — a plain integer field, so
`pc.vhistory` marshals a number rather than manufacturing a callable. It is not a `stats.txt`
Stat: no container slot holds it, and `CVStatRef` does not resolve the name.

`chooseSire()` (`vamputil.py`) is the only reader, and the only writer of the three flags it
derives. It compares the raw index against three rows and assigns `1` on a match, `0` otherwise:

| Test | Flag set | `histories000.txt` row |
|---|---|---|
| `pc.vhistory == 1` | `G.Player_Homo` | 1 `Homosexual_Player` |
| `pc.vhistory == 80` | `G.Player_Insane` | 80 `Subtly Insane` |
| `pc.vhistory == 63` | `G.Player_Batshit` | 63 `Completely Batshit` |

Row numbers are file positions in the patch's 93-row table, and each names the History its flag
is about, so the three constants are self-checking against the file.

The flags outlive the function that sets them: `G.Player_Homo` gates dialogue and four of
`sp_theatre`'s twelve `logic_pythoncheck` gates (`courtroom_scene_bip1`, `embrace_male`,
`whisper_male`, `whisper_female`), plus the sire-model branches in `courtroomSire()` and
`nosferatuRevealer()`. Because `chooseSire()` is fired once, by the Embrace `trigger_once`
(`docs/vtmb/choreographed_scenes.md`), a session that never runs it leaves all three at `G`'s
default-on-miss `0`.

#### How the operators compose — `CVTraitEffectQuery` **[VtMB]**

A read is one pass over the character's `m_tEffectList` filling a 68-byte accumulator, then one
finalize. `ApplyEffects` (`101F9BF0`) builds the query (`101F6910`), lets every group in the list
fold its matching effects in (`CVTraitEffectGroup_t::Apply` `101F75A0` → the per-effect switch
`101F6C50`), then finalizes (`101F69A0`).

The query's fields and their **initial values**: the input `value`; `add` **0**; `mul` **1**,
`div` **1**, `max` **32000**, `min` **−32000**, each with a claim priority starting at **−1**;
`percent` **100**.

| Operator | What it does to the query |
|---|---|
| `0 +` / `-` | `add += amount` — every additive effect sums |
| `1 *` / `2 /` / `3 Max` / `4 Min` | claims a **single** slot: a higher group priority takes it outright; at equal priority the **smaller amount wins** — for `Min` too |
| `5 %` | `percent += (100 - amount)`, so `"50%"` reads as 150% and `"200%"` as 0 |
| `6 Value` | **replaces** the value outright (a named payload resolves through its own enum first) |
| `7 Cost` … `10 Duration` | the switch breaks — they carry payloads the buy path and the discipline/heal systems read, and no sheet read consumes them |

Finalize is `(value + add) * mul / div` (signed, truncating), then `× percent / 100` when
`percent != 100`, then clamp to `[min, max]`. The group priority is `CVTraitEffectGroup_t+0xC`, and
**no shipped group authors one**, so every group ties and the smaller-amount rule is what actually
decides. The shipped effects use only `+`/`-` (254), `Value` (63), `Max` (48), `Duration` (44),
`Damage` (13) and `BloodCost` (2) — no `*`, `/`, `%` or `Min` anywhere.

Two apparent copy-paste slips sit in the same switch: the `/` case stores its priority into the
`Max` slot's priority field, and the `Min` case tie-breaks against the `Max` slot's amount. Neither
is reachable with shipped data (no `/`, no `Min`), so the runtime implements the clean per-operator
reading rather than reproducing them.

**The bounds are the same walk.** `GetCurrent`'s "effective max" (`101FF060`) resolves the stat's
authored `Max` `CVStatRef` and hands the number to this identical pass keyed by the stat's own
`(category, index)`; the min accessor (`101FF010`) mirrors it. So a `+1` on a stat raises its
ceiling with it, and a `Max 4` both caps the value and lowers the ceiling to 4 — one rule, applied
twice.

**Addresses.** Client-side (`client.dll`): group/category loaders `FUN_10157560` /
`FUN_101576f0`, the per-effect loader `FUN_101568f0`, the modifier-string parser
`FUN_101567e0`. Server-side (`vampire.dll`): the `TraitEffectGroup` readers `FUN_101f7bb0` /
`FUN_101f7cc0`, and the same cost class at `CVStatSubCost_t::Parse` `FUN_101fb980` /
`GetCostToBuy` `FUN_101fbb60` / `GetCostToSell` `FUN_101fbc40`. Both DLLs carry the whole
`CVStat*` family; the chargen panel and the sheet UI exist only client-side.

### Masquerade / Humanity / Blood / Frenzy (`rules.txt`, `stats.txt`)

- **Masquerade** — the `Masquerade` stat is a 0–5 violation counter. An admitted supernatural
  incident calls `ChangeMasqueradeLevel(+1)` only after the player's
  `m_flMasqueradeTimerNext` deadline, then schedules `debug_masquerade_timer`. Mutation fires the
  output for the resulting level and `OnMasqueradeLevelChanged`; an increment above 4 loads
  `sp_masquerade_1`, making level 5 the native loss transaction. Criminal incidents do not mutate
  this stat. `ChangeMasqueradeLevel(delta)` / `GetMasqueradeLevel()`.
- **Humanity** — 0–10 (Def 7); frenzy checks roll against it; `HumanityAdd(delta)`.
  Toreador doubles gains / others double losses via clan trait-effects.
- **Blood** — `BloodPool` (Attributes slot `0xc`) is `Min 0` / `Max 15` / `Default 10`, and the
  literal 15 is the whole of the cap. `CBaseCombatCharacter::IncBloodPool` `0x10338cb0` is
  `CVStatList_t::IncBase(0xc)` `0x10200d60`, which adds the point only while the base is below the
  max the stat's own info block resolves (`FUN_101ff060` off `CVStatInfo_t+0xc`/`+0x10`);
  `DecBloodPool` `0x10338df0` → `DecBase` `0x10200ea0` is the mirror against `Min`. **The
  per-Generation pool ceiling is dead data in the shipped file**: `stats.txt` (lines 349–370)
  authors `"Max" "15"` and the `"Max" "Generation_Blood_Pool_Max"` line above it is commented out,
  and the string `Generation_Blood_Pool_Max` has **no reader anywhere in `vampire.dll`** — the
  `Generation_Blood_Pool_Max` table is present in the file and never consulted. So every character
  caps at 15 whatever its `Generation`, and the right rail draws 15 discrete points, three groups
  of five. Generation tables still cap trait rating and blood/turn; they do **not** cap the pool.
  `BloodPool_Max` (slot `0xd`, lines 374–388) is a **separate** stat — the file's own comment calls
  it "the Maximum (generally meaning the 'starting' bloodpool for a critter)" — with `Default 10`.
  Nothing clamps `BloodPool` to it; it is a datamap field (`base_bloodpool_max` / `bloodpool_max`,
  registered by `datamap_CBaseCombatCharacter_builder` `0x1031a600`) that the binary otherwise
  never reads. A meter that divides by it draws the wrong number of points.
  `VampHeal_Info` heals `1%` Max Health per `2.0s`, `BloodToHealthRatio 10` (1 blood → 10 HP).
- **The feed victim's bar** divides by the VICTIM's char-template `BloodPool`, not by the 15-point
  stat cap and not by `BloodPool_Max`. `CFeedBar::vfunc114` `0x100503d0` (client) seeds its
  denominator with the literal `0xf` and overrides it with the player's replicated
  `m_iClientFeedMaxBloodPool` whenever that is non-zero; a payload of 0 or ≥ 15 hides the bar
  outright. That field is written in exactly one place —
  `CBaseCombatCharacter::EnterGrappleState` `0x10329760`, from the victim's char-template
  `Attributes[BloodPool]` (`template+0xd0` then `+0x30`, i.e. slot `0xc`; the same word
  `CAI_BaseNPCTroika` `0x1029a0b0` seeds the victim's own slot 12 base from). Nothing clears it, so
  the drained bar retained through the release tail keeps the denominator it opened with.
- **Frenzy** — `VampFrenzy_Info`: triggers over `Dmg_Amount 20` (normal) / `15`
  (aggravated), or by hunger below `BloodPool_Min_For_Hunger 3`; `Default_Difficulty 5`
  against Humanity, plus `Frenzy_Check_Mod` (clan bane) and a low-blood penalty;
  cooldowns 190s success / 120s failure.

The die itself (pool → tier, 10-again, botch table) is `docs/vtmb/dice-system.md`.

### Zone legality — where a verb is allowed

Zone is one world-owned enum, not a property sampled independently from the player's current
volume. The singleton world retains `m_nAreaType` at server offset `+0x49c`, clamps it to `0..2`
and replicates it as the two-bit `DT_WORLD` property of the same name. The client receives the
field at `+0x430` and projects it into the icon above the health meter:

| Value | Client sprite | Area | Native policy |
|---:|---|---|---|
| 0 | `area_icon_combat` | combat | area type adds no Elysium verb block; terminal criminal and supernatural incidents are suppressed |
| 1 | `area_icon_safearea` | safe / Masquerade | witnessed incidents may reach police or Masquerade policy; entering ends Celerity and Protean |
| 2 | `area_icon_elysium` | Elysium | weapon/attack, feeding and Discipline admission is blocked; Bloodbuff during retained `LockPick` is the sole Discipline exception |

The authored `worldspawn` key `safearea` establishes a map baseline. `CWorldEvents::SetSafeArea`,
Python `world.SetSafeArea(0|1|2)` and the native `safearea` callback mutate the same retained field;
scripts use those calls to change policy inside a loaded map. A changed value is not merely
published. The server first applies it to every connected player: value 2 equips
`item_w_unarmed` and runs the ordinary all-Discipline teardown, including owned timed events and
targeted effects; value 1 ends only compiled Discipline indices 3 and 11, Celerity and Protean;
value 0 has no immediate teardown. It then marks the world state dirty for replication.

The server remains authoritative after that transition. The shared player-action predicate reports
Elysium or `nofrenzyarea` state and is consumed by weapon, feed and Discipline decisions.
`CBaseCombatCharacter::Weapon_CanSwitchTo` allows only `item_w_unarmed` while Elysium is active,
and the transition itself forces that item. Feed eligibility rejects before the paired feed
transaction. The shared Discipline authority calls the player eligibility virtual before either
the native-active or targeted path. That virtual admits compiled index 4, Corpus Vampirus /
Bloodbuff, before its ordinary blockers only when the player's retained action target reports
compact action 300, `LockPick`; this is the exact Elysium exception.

On the client, a changed replicated value recreates the corresponding HUD sprite and drives its
presentation timing. The focused quickbar Discipline dispatcher maps the visible selection to a
compiled index and sends `vdiscipline_int` without reading `m_nAreaType`. Client-side greying or
suppression is therefore not established by this pass and cannot replace server rejection.

Breaking *human* law does not touch the Masquerade counter; an admitted criminal incident enters
the delayed police-response transaction. An admitted supernatural incident is the separate native
route to a rate-limited Masquerade increase and, when `debug_supernatural_cop_spawn` is enabled,
may independently enter the same police response. Killing innocents costs Humanity, including
inside a combat zone. The player-side ordering and pursuit/alert state machine are
`player-entity.md`.

Zone joins the law transaction at two different boundaries. Activity producers and NPC witnessing
do not receive it: an accepted targeted Discipline raises authored `SupernaturalLvl` and, when
`Overt`, criminal activity 3; each NPC applies its own `pl_*` thresholds and visual witness test.
But the sole criminal- and supernatural-incident thunks then require `m_nAreaType != 0` before
entering police or Masquerade policy. Combat therefore suppresses an otherwise witnessed incident
at terminal admission, while Elysium prevents the forbidden verb before activity or effect commit.
There is no monolithic `zone + attempted verb + witness` predicate, but zone is not absent from the
transaction.

Map travel obtains the destination world's authored baseline rather than carrying an area value on
the player. Replication is statically closed; this pass did not establish a save-datamap record for
`m_nAreaType`, so same-map save/load retention, exact refusal feedback and frame-exact icon fades
remain controlled retail acceptance.

## 4. The opening flow — New Game → chargen → trial → tutorial

New Game reaches genesis, theatre, tutorial, and Santa Monica through the landmark chain in
`docs/vtmb/level_transitions.md`. This section owns what each stage does and the state carried between them.

> `sp_ninesintro` is **not** part of the opening — it is the later "arrive in Downtown
> LA" cutscene. It only shows up because it was pre-exported under `$ELYSIUM_EXPORT_ROOT/`.

### Character generation — `sp_genesisdevice_1`

A tiny abstract limbo map (~4.6 KB entity lump, skyname `hav`, dark fog): 14 entity records,
seven world hulls, an empty `.props`, and one light. Troika's codename (a *Star Trek* device that
creates life) for the chargen staging space.

**Chargen is engine-side, fired by an entity — not by script** **[VtMB/data]**. The
`newplayer` `trigger_once` at entry:

```
OnTrigger  ",,,0,-1,G.Story_State = -5,"                 # mark intro phase (field-6 python)
OnTrigger  "newplayer,Toggle,,0,1,ccmd.createplayer,"    # after 1s: open chargen UI, self-disable
```

`ccmd.createplayer` is the engine console command that runs the clan/attribute/ability
screens and writes the result **directly onto the player entity** (`pc.clan` int 2–8,
`pc.IsMale()`, `pc.base_<discipline>`). `demo.py` is **vestigial** — its functions
reference `cube_*`/`cagedancer_*` entities that don't exist on this map (leftover
E3/demo code); the only live outputs are `logic_auto → unhidePlus()` and a `logic_timer`.
The wizard's close tail teleports the player into `trigger_multiple "firetrans"`; its
`OnStartTouch → boogieout,ChangeNow` forces the `trigger_changelevel` to `sp_theatre` at landmark
`newgame`. The lifted spawn rests on a platform roughly 5.7 m above the exit volumes, so this
teleport is the exit, not a shortcut to a trigger the player walks into. **Clan is carried on the
player entity across the landmark**, read later.

### The intro cutscene — embrace + trial (`sp_theatre`)

**There is no Bink story video and no separate intro map.** The four `.bik` files
(`Vampire/media/`) are only the Activision/NVIDIA/Troika/White Wolf **boot logos**. The
opening cinematic is entirely **in-engine `logic_choreographed_scene`** playback in two
acts:

1. **The Embrace** — a `trigger` StartTouch fires a fade + camera track
   (`PlayAsCameraTarget`/`PlayAsCameraPosition`) + `embrace_o_matic,Start` (the `.vcd`
   scene) + `embrace_check,Test` (gender branch: `Embrace_bips1.mdl` vs `_female.mdl`,
   male/female VO).
2. **The Courtroom trial** (LaCroix judges you; your sire is staked) — `start_courtroom`
   fires `courtroom_scene_relay,Trigger` + field-6 `courtroomSire()`. The relay launches
   **seven parallel choreo scenes** — `courtroom_scene_bip2..bip7` started directly, and
   `bip1` through a `logic_pythoncheck` on `G.Player_Homo` that picks
   `courtroom_homo_scene_bip1` or `courtroom_normal_scene_bip1` — a camera track, animated
   props (`stake`/`sword`/`cigar` → `SetAnimation scene`), and `fillSeats()`. Cast:
   `Prince1` (LaCroix), `Jack`, `Nines`, `Damsel`, `Skelter`, `Isaac`, `Therese`, `VV`,
   plus clan seat-filler `Vampire*` NPCs (a `G.Player_Homo` variant swaps some in). A
   camera keyframe `OnReachedKeyframe → scene_over_relay → walk_out_relay` ends it.

### `theatre.py`

- **`fillSeats()`** — assign clan-appropriate `.mdl`s to the 8 seat-filler vampires
  (rotating so none matches the player's clan). Reads `pc.clan` + `pc.IsMale()`.
- **`courtroomSire()`** — pick the sire (`Sire2`) and two stakers by clan/gender
  (`G.Player_Homo` + `G.Patch_Plus` Malkavian variants).
- **`nosferatuRevealer()` / `nosferatuTransform()` / `castUnderstudy()`** — swap the
  embrace body-double / sire to a hideous Nosferatu model when clan == Nosferatu (5).
- **`setupMasqueradeActors()`** — de-dup background actors vs the player's clan.
- **`removeCamera()` / `setMitnickFail()`** — Mitnick side-quest bookkeeping (only on
  *returning* visits, not the opening).
- **`tutorialLoad()`** = `ChangeMap(2.5, "tutorial", "tutorial_change")` — **but has no
  caller**: no theatre entity output and no `.dlg` invokes it. The theatre→tutorial
  handoff is done **entirely by entities** (§orchestration); `tutorialLoad()` is a
  legacy/parallel path. (Corrects the assumption in `docs/vtmb/level_transitions.md` that it drives
  the transition.)

### Subtitles for the cutscene — a join

The trial's on-screen captions are a **join**, not a caption file **[VtMB/data]**:

```
.vcd speak token          →  .mp3 (audio)         + .dlg line-id (subtitle text)
courtroom_bip2_scene.vcd:
  event speak "prince_line1015"
    param "Character\dlg\Downtown LA\prince1\line1015_col_f.mp3"
```

Line `1015` of `dlg/downtown la/prince1.dlg` *is* LaCroix's caption ("My apologies for
disrupting…"). LaCroix = `Prince1`, so his lines are `prince1.dlg`. This is the same
`.dlg` line-table the dialogue system uses (§5) — cutscene captions are ordinary dialog
rows.

### The tutorial — `sp_tutorial_1` / `tutorial.py`

Entered via landmark `tutorial`. Two `logic_auto`s init on `OnMapLoad`:
`pc_0,MakePlayerUnkillable`; `Jack,WillTalk 0` (silent until cued);
`world,SetNoFrenzyArea 1`; `ccmd.wc_create` (runtime cubemap bake). Nothing fires on
arrival as an authored landmark output. At the zero-offset landmark position the initial standing
hull does enter the no-output `trig_autosave`; three overlapping changelevels are `NOTOUCH` and
`trig_popup_move` is disabled. A carried source-landmark offset can shift those contacts. The first
dialogue beat is armed by the `trig_off_porch` `trigger_multiple`, whose
`OnEndTouch` (walking off the theatre porch) sets `Jack,WillTalk 1`, calls
`Jack,StartPlayerDialogRemote 256` — opening `dlg/Main Characters/jack_tutorial.dlg` —
and spawns `blueblood_maker` plus `pc_0,CreateControllerNPC`. Exact spatial thresholds and reversed
equal-time action order are in `docs/vtmb/sp_tutorial_1-event-surface.md`.

> **`OnEnterMapHere` is an `info_landmark`-only output** **[VtMB]**. Its datamap builder
> (`FUN_100b7220`, the `CBaseLandmark` map alongside `OnSpawnOneCopCar` /
> `OnCopsInPursuit` / `OnHeightenedAlert`) holds the sole reference to the
> `OnEnterMapHere` / `m_OnEnterMapHere` string pair in `vampire.dll`. Map data that hangs
> the output on a `point_teleport` — `sp_tutorial_1`'s `teleport_very_beginning`,
> `sm_hub_1`'s `sewerB2_street` — is **inert**: the class declares no such output, so the
> keyvalue lookup drops the wire.

**The tutorial is a beat machine keyed on one integer.** Jack's NPC `OnDialogEnd` fires
`DialogPostProcess()` (field-6 python), which dispatches on **`G.Tut_Jack`** (a 0→18
counter Jack's dialogue increments) to advance each beat: open the right `popup_*` help
window, unlock the next door, begin the next `scripted_sequence`, enable triggers.
Selected beats: `0` = skip tutorial (→ leave); `1` = feeding; `2` = chopshop door;
`3` = raid scene; `8` = Sabbat; `9`/`11`/`12`/`13` = unarmed/melee/stealth/physics;
`14` = Jack hands the .38 + ammo; `15` = end (schedule `popup_58` + `end_fade`).

Clan-specific setup — `logic_set_clan_stuff` fires `SetClanPopups()` (swap each discipline
help-popup's text to the player's clan's disciplines; lock/unlock the discipline-test
rooms) and `OnDiscGuys()` (spawn extra test enemies by discipline level).
Discipline-room gates read a `G.Tutorial_Discflags` bitfield (`cPresence 0x800`,
`cPotence 0x400`, `cAuspex 0x4`, …) against `IsClan(player, …)`. A `saveState()`/`state()`
snapshot system rolls the player back to a `logic_reset_*` node on a botched step.

**Exit** — `LeaveTutorial()`: `G.Story_State = -2`, `MakePlayerKillable`,
`ChangeMap(2.5, "newgame", "trig_leave_tutorial")` → `trigger_changelevel
trig_leave_tutorial → sm_pawnshop_1`, landmark `newgame` — the Santa Monica pawnshop, the
real game start.

### Orchestration — what fires each step

| Transition | Trigger | Wiring |
|---|---|---|
| New Game → chargen | engine `map sp_genesisdevice_1` | menu/client `map` command |
| chargen UI opens | `newplayer` trigger_once | `OnTrigger → ccmd.createplayer` + `G.Story_State=-5` |
| chargen → theatre | wizard close → `teleport_player firetrans` | `firetrans.OnStartTouch → boogieout,ChangeNow`; `boogieout` names `sp_theatre @ newgame` |
| embrace | trigger StartTouch | `embrace_o_matic,Start` + camera + gender `Test` |
| trial | `start_courtroom` trigger_once | `courtroom_scene_relay,Trigger` + `courtroomSire()` |
| trial → walk-out | camera keyframe | `OnReachedKeyframe → scene_over_relay → walk_out_relay` |
| **theatre → tutorial** | `walk_out_cam_k` final keyframe | `tutorial_change,ScriptUnhide` + `controls,Deactivate` + `fade_to_tutorial,Fade`; then the (StartHidden) `tutorial_change` trigger_changelevel transitions. **Not** `tutorialLoad()`. |
| tutorial entry | landmark `tutorial` placement (no authored arrival output) | Jack stands cued-silent (`WillTalk 0`); `teleport_very_beginning`'s `OnEnterMapHere` wires are inert. Initial collision reconciliation can enter `trig_autosave` at the zero-offset reference. |
| tutorial first beat | `trig_off_porch` trigger_multiple | `OnEndTouch → Jack,WillTalk 1` + `Jack,StartPlayerDialogRemote 256` + `blueblood_maker,Spawn` + `pc_0,CreateControllerNPC` |
| tutorial beats | Jack `OnDialogEnd → DialogPostProcess()` | branch on `G.Tut_Jack`; + `logic_set_clan_stuff`, per-beat `scripted_sequence`/`ScheduleTask` |
| tutorial → Santa Monica | `LeaveTutorial()` → `ChangeMap` | `trig_leave_tutorial` → `sm_pawnshop_1`, landmark `newgame` |

### State carried across the opening

- `pc.clan` (2–8), `pc.IsMale()`, `pc.base_<discipline>` — written by `ccmd.createplayer`,
  read by `theatre.py` (model casting) and `tutorial.py` (clan popups / disc gates).
- **`G.Story_State`** — the intro spine: `-5` chargen, `-4` theatre, `-2` leaving
  tutorial. Later maps gate on positive values (10/15/20…); negative = pre-game.
- **`G.Tut_Jack`** — tutorial beat counter (0–18), the axis of `DialogPostProcess`.
- `G.Player_Homo` (romance/model branch), `G.Patch_Plus` (patch "Plus edition" gate),
  and the tutorial latches (`G.Tutorial_Feeding`, `G.Tut_Gun`, `G.Tutorial_Discflags`, …).
- `pc.SetQuest("Tutorial", 1)` on finish/skip.

Two engine console-commands appear with no data equivalent: **`ccmd.createplayer`**
(chargen UI) and **`ccmd.wc_create`** (runtime cubemap bake — irrelevant to a port that
bakes offline).

### `createplayer` opens a panel; it does not build a player **[VtMB]**

`createplayer` is a **`client.dll` ConCommand** (registered at `FUN_10173670` →
`FUN_100df430("createplayer", FUN_10173640, 0, 0, 0)`). Its handler is three lines:

```c
void FUN_10173640(void) {            // "createplayer"
  FUN_100f0ed0("CharEditPanel");     // FindHudElement -> DevWarning "Could not find Hud Element '%s'"
  FUN_101734f0();                    // (thiscall on the panel)
}
void FUN_101734f0(CharEditPanel *this) {
  engine->ExecuteClientCmd("giftxp 9000", 1);
  this->mode /*+0x274*/ = 1;
  FUN_10173350(this);                // show: two visible bytes + a vtable call
}
```

So **the player entity already exists when the wizard opens** — `giftxp 9000` is granted to it and
the panel edits it in place. The open handler shown above does not issue the pause itself; the
panel's show path takes the hold, and the close tail below proves that hold by issuing `v_unpause`.
The exact `v_setpause` issuing call site remains untraced; `FUN_10173350` is the candidate. Only
`giftxp 9000` runs in this handler; `vautolvl <clan>_CharGen` is issued later, when a clan is chosen
(§"The sheet baseline is bought").

**Closing the chargen host drives the map exit** **[VtMB/decompile]**. `CharEditPanel` close/hide
is `FUN_101740e0(this, bAccept)`. Its tail, when panel mode `+0x274` is non-zero, executes:

```c
engine->ExecuteClientCmd("v_unpause");
engine->ExecuteClientCmd("teleport_player firetrans");
```

Both ACCEPT and CANCEL use this close function, so both release the hold and leave genesis. The
meaning of the local byte that gates this tail is not yet recovered; it changes when the tail runs,
not the two commands it performs. `teleport_player` is a `vampire.dll` ConCommand whose own contract
is “Teleports the player to a named entity, or to an X Y Z coordinate”; an unresolved named target
prints `Could not find entity named %s`.

**One panel, three modes.** Three sibling ConCommands open the *same* `CharEditPanel` and differ
only in the mode int at **`+0x274`**:

| ConCommand | handler | `+0x274` | also does |
|---|---|---|---|
| `questlog` | `FUN_10173620` | **0** | show, set the in-game backdrop, clear `+0xcf9` |
| `createplayer` | `FUN_10173640` | **1** | `giftxp 9000`, show |
| `chooseteam` | `FUN_10173600` | **2** | `giftxp 9000`, show — the multiplayer templates (clans 9–11), which is what `unhidePlus()` branches on |

The per-mode call `FUN_101744c0(mode)` selects the panel's **backdrop**, mode 0 reaching
`interface/charactermaintenance/background` — so the painted street is a panel backdrop with the
character model composited over it, not a rendered scene.

## 5. Dialogue, NPC conversation & subtitles

Conversations are **`.dlg`** files (147 loose in `Unofficial_Patch/dlg/<hub>/`, ~50,393
rows) **[data]**.

### Physical format

Row-per-line, **CRLF**-terminated, Latin-1. Each row is exactly **13 fields**, each
wrapped `{` TAB `content` TAB `}` and concatenated with no separator. Parse: split the
row on `}{`, strip `{`/`}` and surrounding tabs. (One row corpus-wide has 14 fields — a
Troika typo in `kiki.dlg`; tolerate it.)

### Column schema (0-indexed, empirically verified)

| Col | Role |
|---|---|
| **0** | **Line ID** (int, unique in file). Convention: NPC lines are "tens" (1, 11, 21…); PC choices fill the gaps. |
| **1** | **Spoken text — male-PC variant** (the on-screen **subtitle** for NPC lines; the full **choice text** for PC lines). Inline `[stage directions]` are VO-recording director notes (emotion/delivery like `[sarcastic]`, pacing like `[pause]`, and speaker attributions like `[Cop Buddy2:]` on multi-VO lines) that shipped inside the localized strings — free-form English, not an engine cue vocabulary. The engine **strips them at display**; the parser keeps them (raw `Text()` is verbatim, `DisplayText()` strips). |
| **2** | **Spoken text — female-PC variant** (engine picks 1 vs 2 by `pc.IsMale()`; usually identical). |
| **3** | **Link / branch**: `#` = this is an **NPC line**; a number **N** = this is a **PC choice** that jumps to NPC line N; **`0` = END**; empty = padding. |
| **4** | **Condition (eval)** for PC choices — a `dlgexpr` gate; **or** an **NPC-speak action** for NPC lines. |
| **5** | **Action (exec)** run when the line is chosen/spoken (`;`-separated). |
| **6** | **Brujah-clan text variant** — always empty in shipped data. |
| **7** | **Gangrel-clan text variant** — always empty in shipped data. |
| **8** | **Nosferatu-clan text variant** — always empty in shipped data. |
| **9** | **Toreador-clan text variant** — always empty in shipped data. |
| **10** | **Tremere-clan text variant** — always empty in shipped data. |
| **11** | **Ventrue-clan text variant** — shipped only in `prince1.dlg` (8 rows). |
| **12** | **Malkavian-clan text variant** — the Malkavian variant of this row's text (NPC subtitle or PC choice), shown *instead of* col 1/2 when the player is Malkavian (empty = no variant). Not a "short label": across the corpus 9,576 rows carry both and col 12 differs from col 1 in **97%**, and the differences are Malkavian-speak ("Behave, I am your kind of monster" vs "Calm down, I'm not one of them"). |

**[corrected 2026-09-06]** Cols 6–12 are **the seven per-clan text columns**, in `clan_offset`
order Brujah, Gangrel, Nosferatu, Toreador, Tremere, Ventrue, Malkavian (`clan_offset`
`0x100e65d0`, read by `read_line_data` `0x100e61d0`) — col 11 is Ventrue, col 12 Malkavian. They
were previously recorded here as "unused (always empty)"; five of the seven (6–10) are indeed
unfilled in every shipped row, but 11 and 12 are not — Ventrue (8 rows, `prince1.dlg`) and
Malkavian (9,576 rows) are shipped clan text. `get_display_text` (`0x100e1ad0`) prefers a filled
clan column over the gendered text for a PC of that clan.

Animation/camera/gesture are **not** in the `.dlg` — they live in the `.vcd` (§cinematics).
**[corrected 2026-09-06]** the parenthetical "(cols 6–11 empty)" that used to qualify this
sentence is wrong per the corrected column table above; cols 6–11 are five of the seven per-clan
text columns (unfilled except Ventrue's col 11), not a reserved/unused block.

### Runtime / branching

1. `StartPlayerDialog`, `StartPlayerDialogRemote`, and `StartPlayerDialogUnforced` gate the
   request and schedule the NPC's dialogue work. The player-side path reaches
   `CDialog::Acquire` (`0x100e05f0`), which calls `CDialog::GetStartingLine`
   (`0x100e0b10`) **[VtMB]**. The opener is selected from authored state; it is not the first
   NPC line with display text.

   The three Tier-1 inputs are **not aliases** in the hash-pinned server binary **[VtMB, RE46]**:

   | Input | Distinct server behavior |
   |---|---|
   | `StartPlayerDialog` (`0x1029ef80`) | Common player/partner and NPC-state guards; an integer input is stored at NPC `+0x5bac`; forced byte `+0x6495` is set; schedule/activity `0x6d`. |
   | `StartPlayerDialogRemote` (`0x1029f060`) | Common guards; the input variant is **not read**; forced byte is set; distinct schedule/activity `0x6e`. |
   | `StartPlayerDialogUnforced` (`0x1029f120`) | Common guards plus a player-side refusal predicate; an integer is stored at `+0x5bac`; forced byte is cleared; schedule/activity `0x6d`. |

   Therefore the tutorial's authored `StartPlayerDialogRemote 256` does not decode into flags in
   this handler. The purpose of the integer stored by the other forms remains open. None of the
   three complete input bodies writes player/NPC origin, angles, velocity, camera pose, or input
   state. They schedule downstream dialogue work; physical placement, facing, or controller
   transfer must not be inferred from their final presentation. The gated comparison capture is
   `research/cases/dialogue-camera/`.
2. `GetStartingLine` scans the parsed rows in **physical file order**, record stride `0x34`.
   A row is a starting-condition sentinel when its male text at `+0x04` contains, case
   insensitively, `starting condition`, `starting-condition`, or `starting_condition`
   (`0x100df240`). For each sentinel it parses col-4 at `+0x10` through
   `CDialogDependency::Parse` (`0x100e8fc0`) and tests it with the live player and NPC through
   `CDialogDependency::Test` (`0x100e94e0`). The **first passing sentinel whose numeric col-3
   link resolves to a line wins**. A passing sentinel with an invalid link warns and scanning
   continues; later passing rows never override an earlier valid one.
3. If no sentinel wins, a non-empty NPC `usescript` is executed through
   `CDialogDependency::CallPyDialogFunc` (`0x100ea2d0`) in Python eval mode `0x102`; an integer
   result is the starting line. An absent `usescript` selects line `1`; a present script that errors
   or returns a non-integer yields `0`. `Acquire` validates the selected id and, if it is invalid,
   falls back to the first stored line id. The retail debug-only forced-line override precedes the
   scan and is not gameplay state.
4. The selected NPC line's col-4 is an **action**, run with col-5 when the line is spoken — not
   a gate. After NPC line **N** is spoken, gather the contiguous
   run of PC rows after it (N+1, N+2, … up to the next `#`). Show each ordinary PC row whose col-4
   condition is true as a menu entry (text = the Malkavian col 12 when the PC is Malkavian, else col 1/2).
   A passing `(Auto-Link)` or `(Auto-End)` row is control flow, never a visible response.
5. Player picks → its col-5 action runs → jump to the NPC line in its col-3 link → repeat.
6. **Link `0` ends** the conversation. `(Auto-End)`/`(Auto-Link)` are editor-generated
   silent-transition placeholders. They resolve only after the preceding NPC turn has been
   presented; their col-5 action then runs under the same action-before-link rule as a real pick.

Jack's opening tutorial pins the observable automatic ordering **[data, live]**: Malkavian response
23 (`I shall undertake your dark tutelage.`) links to NPC row 81 (`[like the Fonz]Alright.`); row
81 is displayed and spoken, with no response marker on screen; hidden `(Auto-Link)` row 82 then
links to NPC row 83 (`Uhh... why don't we, uh, step out back here.`). Therefore an implementation
must retain row 81 as the active presented turn and may not resolve row 82 synchronously while
entering it. **[corrected 2026-09-06]** The retail completion signal is now recovered — this
paragraph previously read "the exact retail completion signal has not yet been isolated in the
binary **[inferred]**". `NPCNotifyDoneTalking` (`0x100e4780`) reads `flags[0]`, and Auto-End
(flag `0x10`) becomes automatic only when `LookupSpeechFile` (`0x100e1880`) finds audio for the
NPC line; when it does not, the engine forces one visible response with value `-1` instead of
auto-advancing. Voice-turn completion is the reproduced observable boundary, and that forced
value-`-1` response — the retail rule, not a port invention — is what covers the no-audio case so
the displayed line is never skipped.

The file order is authored control flow, not an incidental parser detail. In
`jack_tutorial.dlg`, `G.Tut_Jack == 1 and G.Tut_Patch == 1` links to line 85 before the
two blueblood alternatives; the Nosferatu rat-feeding condition links to 561 before the
generic 551 condition; and duplicate `G.Tut_Jack == 18 and G.Tut_Ashot == 0` rows link to
425 then 246, making the latter shadowed. A port must preserve row order and stop on the first
passing valid link.

### Dialogue close and `DialogPostProcess`

`DialogPostProcess()` is downstream of opener selection and remains required. On close,
`CDialog::Release` (`0x100e5240`) flushes the pending dialogue event script and
`CDialog::CallPendingNPCEventScript` (`0x100e5c70`), clears the live dialogue state, then calls
the owning NPC's dialogue-end path (`0x102c0360`) **[VtMB]**. That path fires the NPC's
`m_OnDialogEnd` output at `+0x5f5c`. Jack's authored `OnDialogEnd` Python payload calls the level
script's `DialogPostProcess()` **[data/script]**.

The selected line and choices write authoritative `G` values before this output fires.
`DialogPostProcess()` reads `G.Tut_Jack` and its secondary flags and performs the world-side beat
transition: tutorial checkpoint snapshot, teleport/fade, popup, scripted sequence, trigger/door,
or map travel. Its module-local `G_tut` dictionary is a reset checkpoint copied by `saveState()`;
it is not the authoritative story-state store and it does not select a dialogue opener.

**Female take selection (corrected 2026-09-07).** `FUN_100e15c0` returns `'F'` only when
`FUN_100df030(row)` holds: `col2 != NULL && strcmpi(col1, col2) != 0`. Troika's editor copies col-1
into col-2 on most rows (17,566 filled against 211 distinct female takes shipped; every one of
Jack's tutorial rows is a copy), so "col-2 non-empty" is the wrong test — it asks for a
`line<id>_col_f` take that was never recorded and silences the line. A copy differing only in case
is still a copy. Ported in `ElysiumDlgText::ChosenTakeLetter`; 17 shipped `_col_f` files sit on
copied rows retail never selects either.

**`EndDialog` input (appended 2026-09-06).** A script-fired `EndDialog` at the NPC is retail's
`CDialog::Release` (`0x100e5240`): the parked NPC event script is flushed, the live dialogue state
is cleared, then `m_OnDialogEnd` fires. The port's `FElysiumNpc::InputEndDialog` therefore tears the
world session down itself when this NPC owns it, before firing `OnDialogEnd`. The world's own
teardown queues the same input with the closed session's serial as the param, so a close that
arrives after the flushed col-5 re-opened the same NPC is recognised as stale: the old
conversation is counted and its output fired, the live one stays.

### Port design: state-based opener selection

Keep the selector inside the host-agnostic dialogue branch machine; do not special-case Jack or
cache `G.Tut_Jack` on the conversation. `FElysiumDlgConversation::Start()` recomputes the opener
from current state on every acquisition:

1. Give `FElysiumDlgLine` a pure starting-sentinel predicate over raw `TextMale`, recognizing the
   three retail spellings case-insensitively. Preserve the parser's physical `Lines` order.
2. Add a pure `SelectStartingLine` pass to `FElysiumDlgConversation`. For each sentinel, evaluate
   `Condition` through the existing injected `FCondFn`; parse `Link` as an integer; resolve it
   through `FElysiumDlgFile`; return immediately on the first passing valid NPC target. Warn and
   continue for a passing invalid link.
3. Add one injected fallback callback returning an optional integer line id. The NPC adapter owns
   it because it has `UseScript`, world, player, `self`, and activator context: evaluate the raw
   `usescript` through the installed script host and accept only an integer result. An absent script
   yields retail line `1`; a script error or non-integer result becomes `0`, and any invalid final id
   yields the file's first stored id.
4. Feed the resolved index into the existing `EnterNpcLine` path so NPC actions, choice gates,
   audio/UI, close, `OnDialogEnd`, and `DialogPostProcess()` keep their existing ownership and
   ordering. The opener must never call `DialogPostProcess()` directly.

Acceptance pins the rules rather than one Jack playthrough: synthetic overlap proves first-match
file order; all three sentinel spellings classify; a passing invalid link continues; all-false
conditions exercise `usescript` and line-1 fallback; and `jack_tutorial.dlg` selects 85 for the
patch-first overlap, 561 before 551 for the Nosferatu overlap, and 425 before the duplicate 246.
An end-to-end test writes a `G` flag from a selected dialogue line and observes that value from
`OnDialogEnd`, proving line actions still precede `DialogPostProcess()`.

### Subtitles — literal text, no string table

Dialog subtitles are the **literal col-1 (male) / col-2 (female) text**, verbatim —
there is **no string-table index and no `closecaption`/caption/subtitle file anywhere**
(the whole patch tree has only `gameui_english.txt`, which is UI-widget strings).
Localisation swaps the whole `.dlg`. Cutscene captions are the same `.dlg` text, reached
by the `.vcd` join (§4). **One subtitle source, the `.dlg` text field.**

### Voice + lip-sync — path convention (no filename column)

Audio resolves by **path**, not a column:

```
sound/character/dlg/<hub>/<dlg-stem>/line<ID>_col_e.{wav|mp3|lip|vcd}
```

`<ID>` = col-0 line id; `_col_e` = English. Only **NPC "tens" lines** get audio — **the
PC is silent** (no PC-choice VO). `.wav`/`.mp3` = voice (mostly in the VPKs; the patch
ships ~5,274 loose `.lip` + `.vcd`). `.lip` = Source phoneme/lip-sync (`VERSION 1.2`,
embeds the plaintext + timing). `.vcd` = Valve choreography (Faceposer) — actor/timing/
gesture channels, and it names the `.wav` by the same relative path with the clip
duration.

### Discipline-gated lines & the Python coupling

Field-4 conditions front a `dlgexpr` **skill-check** (`docs/vtmb/python_bridge.md` grammar):
`"<Skill> <threshold>"`, implicit `>=`, optionally `& <python-expr>`. **Success/failure
is presentation, not a die roll in the row** — the choice is *offered only if the check
passes*; "fail" outcomes are authored as sibling PC rows with complementary conditions
pointing at a different NPC line. Real rows **[data]**:

```
ricky.dlg  id5  link=61  Seduction 7 & (not pc.IsMale() and G.Johnny_Dead == 0)
lufang.dlg id322 link=331 Persuasion 7 & pc.humanity >= 5
ji.dlg     id323 link=331 Intimidate 7 & G.Patch_Plus == 1
doll1.dlg  id2  link=151  Seduction 3 & OneOfSet(1,4)      # 1-of-4 random variant
```

A col-4 condition, whatever its `dlgexpr` shape, is evaluated as a Python expression and gates the
line **true only for a non-zero integer result** — the same integer-only Python-gate truth rule
`docs/vtmb/python_bridge.md` owns (a truthy string, list, float, or `None` all read false; an error
reads false). This one rule governs every col-4 availability gate identically: PC-choice rows,
NPC-line rows, and the `starting condition` sentinel rows resolved by `GetStartingLine`. Two
neighbouring `.dlg` Python paths are **not** this boolean gate: the col-5 **action** runs for its
side effects with the return value discarded, and the NPC `usescript` **selector** uses its returned
integer directly as a line id.

Actions (col 5, and NPC col 4) run against the live `__main__`:
`npc.SetDisposition("lay", 1)`, `pc.SetQuest("Mercurio", 2)`, `G.Mercurio_Know = 1`,
multi-statement `pc.SetQuest("Barabus",2); G.Story_State = 55; G.Barrabus_Exit = 1`.
`pc`/`npc` are engine-bound Character objects, `G` the flag-bag, `IsClan`/`OneOfSet`/
`IsMale` are `vamputil.py` helpers — all resolved through the datamap reflection layer
(`docs/vtmb/python_bridge.md`), so a port needs a `dlgexpr` evaluator over the `G`/Character/
`vamputil` surface, not a bespoke dialog API.

### Cinematics summary

No pre-rendered story cutscenes. Every cinematic is a **`logic_choreographed_scene`**
driving a `.vcd` over `.mdl` animation (`BaseAnim`/`MaleAnim`/`FemaleAnim` name the shared
cinematic model; the cast binds **by actor name**, not through `target1..4`) with scripted
`camera_keyframe`/`camera_track` camera (`PlayAsCameraPosition`/`PlayAsCameraTarget`,
`RestoreCameraToPlayerControl`) — the scene format's own camera events are unused.
Captions = the `.vcd` `speak` token → `.dlg` line-id join. Deferred beats use
`ScheduleTask(delay, "<pysource>")`. Format + event semantics: `docs/vtmb/choreographed_scenes.md`.

### Retail conversation chain (recovered 2026-09-06)

Recovered from `vampire.dll` / `client.dll` (D0). The eight
arms below are either reproduced or named as a modernization; nothing is silently
dropped.

1. **Entry.** `CBasePlayer::PlayerUse` (`0x10167850`) resolves the use target, tests the
   character's `WillTalk` latch (virtual `+0x49c`, set by `InputWillTalk` `0x103418f0`), clears
   the NPC schedule and pushes AI schedule `0x6a`, then calls player vtable slot 414
   (`FUN_10178280`, the real StartDialog). The `StartPlayerDialog*` inputs land on the same slot
   through `CAI_BaseNPCTroika::StartTask` (`0x102a1910`) with schedules `0x6d`/`0x6e`. Common
   guards: a player exists, the player has no live partner (`player+0xfe8`),
   `IsBusyWithDiscipline(npc)` false, `m_bfAINPCFlags2 & 0x10000000` clear. `Unforced` adds the
   player-side refusal predicate `0x10178170`: a set of combat timers on the player
   (`+0x1d1c` no-dialogue-until stamp, `+0x1dd0`/`+0x1dd8`, an enemy count, `+0x1cf8` with
   `FLT_MAX` as the clear sentinel, and `0x10175180` blocking while the entity at `+0x1db0` is in
   state 3). `CPlayerEvents::InputClearDialogCombatTimers` (`0x10227250`) resets them.
   `FUN_10178280` refuses when `m_bForceDialogStart` (`npc+0x6495`) is clear and the predicate
   holds; otherwise `CDialog::Acquire`, `SetDialogPartner`, input lock, remember whether the
   active weapon was drawable (`player+0x1e01`) and switch to `item_w_unarmed`, then build the
   `camera_cinematic` from `default_camera`.
2. **Dependency.** `CDialogDependency::Parse` (`0x100e8fc0`) turns col-4 into a struct: trait
   class (`+0x04`: 0/1 attribute-or-ability, 2 discipline, 4 feat), trait id (`+0x08`), inversion
   flag (`+0x0c`, a negative threshold selects `<` instead of `>=`), threshold (`+0x14`), sex gate
   (`+0x224`: 0 requires female, 1 requires male), a Python part, `m_CompoundType` (1 one only,
   2 AND, 3 OR) and `m_CompoundPrecedence`. `TestSimple` (`0x100e9760`): classes 0/1 compare the
   stat, class 2 requires **both** the discipline rating and the blood pool (stat `0xc`) to meet
   the threshold and refuses discipline id 6 unless `clan_offset == 5` (Ventrue), class 4 is
   `FeatValue >= threshold` except feat `0x16` which routes to `FrenzyComparison`. `TestPython`
   (`0x100e9ff0`) is true iff the eval result is a non-zero `PyInt`; error is false. `ParseDep`
   (`0x100e9290`) writes the wire flags `0x01` discipline, `0x02` feat, `0x04` python present,
   `0x08` event script present, and records the blood cost (threshold for disciplines, 0
   otherwise).
3. **Turn.** `get_pc_responses` (`0x100e82d0`) walks rows after the current NPC line to the next
   `#` or sentinel, admits ungated rows and rows passing the dependency, and **stops at 4**.
   `process_pc_line` (`0x100e8520`) returns 1 keep, 0 drop, -1 drop and set auto-terminate. An
   empty list without auto-terminate replaces the NPC text with `"I do not have a valid reply."`
   and one dummy response. Auto-Link/Auto-End are flags `0x20`/`0x10`, one per turn; Auto-End
   becomes automatic only when `LookupSpeechFile` finds audio for the NPC line, otherwise the
   engine forces one visible response with value -1. Choices are hidden while `IsTalking()`
   (`0x102c0aa0`: live scene or `m_flTalkTime > curtime`) through `ShowPlayerChoices`.
4. **Ordering.** `process_npc_line` (`0x100e8100`) runs the NPC row's col-4 immediately and
   stashes col-5 at `+0x30ea` for `CallPendingNPCEventScript` (`0x100e5c70`), flushed when the
   NPC finishes talking, on `NPCNotifyDoneTalking` (`0x100e4780`), or on `Release`. A pick
   (`CDialog::Pick` `0x100e4bd0`, `dialogpick` ConCommand) is refused while the NPC is still
   speaking, maps the index to its link, echoes subtype 5 for history, **charges the dependency**
   (`pc_charge_dependency` `0x100e8b90`: subtract blood, `AddFakedDisciplineEffect`), runs the
   choice's col-5, then `fill_packet`/`message_send` or `Release` on link 0 / auto-terminate.
   Pick `-1` releases, `-2` is the hurry verb (`0x102c0bb0`).
5. **Presentation.** `CHudDialog` (client.dll, `DialogControl` `0x100553f0`) paints
   `"%d. %s"` bottom-up; `CDialogDependency::ToStr` (`0x100ea5e0`) prefixes discipline choices
   with N dot glyphs (`0x7f`); `GetFontForFlagsDependency` (`0x10053f10`) picks font and colour
   by feat id 6/7/8 and discipline id 6; flags exactly `0x20` draw a pulsing
   `"(press 1 to continue)"`; `Dlg_Malk` font for a Malkavian PC; six bracket pairs stripped
   (`0x100e8060`). `CClientModeDialog` (`0x10042a80`) swallows movement key-downs, so held keys
   are ignored, not cancelled.
6. **Voice.** `generate_speech_filename` (`0x100e1680`) builds
   `sound/character/<dlgpath>/line<id>_col_<C>.<ext>`; `0x100e15c0` picks `C` as the **text
   column used**: the clan letter when that clan column is non-empty (only `m` Ventrue and `n`
   Malkavian are pinned by shipped audio), else `f` for a female PC with a col-2 variant, else
   `e`. `LookupSpeechFile` (`0x100e1880`) probes three extensions in order, mp3 first. Text with
   no letters resolves to `sound/character/dlg/ellipses.<ext>` (`0x100df0b0`). A `.vcd` hit plays
   through a `scripted_scene` (handle `npc+0x6554`, carries `.lip` and gestures); otherwise
   `CHAN_STREAM` at `m_flSpeechVol`. PC lines are never voiced (`message_send` `0x100e58e0`).
7. **Close.** `CDialog::Release` (`0x100e5240`) flushes the pending scripts, fires
   `OnDialogEnd` (`npc+0x5f5c`), clears the partner, destroys the camera, unlocks input and
   restores the holstered weapon (`FUN_10178400`).
8. **Data.** Rows are 13 dwords, stride `0x34`: id, male, female, link, dependency, event
   script, then **seven clan text columns** in `clan_offset` order Brujah, Gangrel, Nosferatu,
   Toreador, Tremere, Ventrue, Malkavian (`0x100e65d0`, `read_line_data` `0x100e61d0`). Shipped
   data fills Ventrue (8 rows, `prince1.dlg`) and Malkavian (9,576 rows). A row is dropped at
   parse when the id is negative or the male text is shorter than 2 characters.

### Dependency parse — measured against the shipped corpus (2026-09-06, D1)

Reproducing arm 2 pinned three things the summary above leaves implicit **[corpus, data]**:

- **A half is a check only if its first whitespace token RESOLVES.** `FUN_102047d0` lowercases the
  half, strips an `M_`/`F_` prefix, replaces the first `-` with a space (raising the inversion
  flag, so the threshold parses positive), then `strtok`s on whitespace: token 0 goes to
  `CVStatRef` (`0x10204570` — four `stats.txt` containers in declaration order, then `feats.txt`),
  token 1 is `atoi`'d as the threshold. **There is no relop in this grammar**, and every one of the
  1,583 shipped skill fronts is the implicit `>=`. Because feats are searched last, `Persuasion`
  and `Seduction` resolve as ABILITY/FEAT by that order and not by which table a reader expects.
- **Corpus shape.** 147 files, 7,216 gated PC rows, 1,583 skill fronts over 15 trait names, of
  which **459 are inverted** (`Humanity -8`, `Persuasion -7` — every authored low-humanity and
  failure route) and **295 are disciplines** carrying a blood price. Zero skill-shaped fronts fail
  to resolve.
- **`+0x1c` is one Python buffer.** `ParseDep` `Q_trimspace`s each Python half into the same
  256-byte field, so when BOTH halves are Python the second overwrites the first and the compound
  still tests a simple dependency that was never claimed — `TestSimple`'s
  `Unhandled dialog dependency` arm, i.e. false. See `retail-defects.md` §6.

### Session lifetime details recovered with the 2026-09-06 defect pass

Four points arms 1, 4 and 7 above leave implicit, pinned while closing a code-review pass on the
port **[decompilation; one inference, marked]**:

- **`WillTalk` ships SET.** `vampire.dll` carries no constructor write and no keyfield writer for
  the latch at virtual `+0x49c`; `InputWillTalk` (`0x103418f0`) is its only writer.
  `sp_tutorial_1` fires `Jack,WillTalk 0` at map load (see §"tutorial entry" above) and re-enables
  him from `trig_off_porch`'s `OnEndTouch` with `WillTalk 1`. **INFERRED**, not read off an
  initializer: a load-time disable is meaningless against a false default, and with one the 79
  authored calls corpus-wide would be the only thing that could ever make a character talkable,
  leaving `+use` dead on every NPC no script had cued. The port defaults the latch to true on
  `FElysiumNpc` and keeps `WillTalk 0` as the disabler. The real initializer remains unrecovered.
- **`Acquire` precedes the holster.** In `FUN_10178280` the order is `CDialog::Acquire`,
  `SetDialogPartner`, input lock, *then* remember `player+0x1e01` and switch to `item_w_unarmed`.
  `Acquire` is what loads and validates the `.dlg`, so an open that fails there never reaches the
  weapon switch. The port matches: the holster is taken only after `OpenConversation` succeeds,
  which is the same set of failures (no `dialogname`, a `.dlg` that will not load).
- **`Release` flushes on EVERY teardown.** `CDialog::Release` (`0x100e5240`) runs the pending NPC
  event script before it clears the live state, so a conversation displaced mid-line — a second
  acquire, a kill, a level teardown — still runs the col-5 `process_npc_line` parked at `+0x30ea`.
  It is not a property of the explicit close path.
- **`pc_charge_dependency` has no inversion test.** `0x100e8b90` branches on `cost != 0` alone, so
  an INVERTED discipline row (an authored failure route) is still charged its threshold in blood
  when picked, even though `TestSimple`'s inverted arm never read the pool. `ParseDep` records
  `BloodCost` for it for exactly that reason. Only the wire flag that drives the client's dot
  glyphs is suppressed for an inverted dependency.

## 7. Open questions

Consolidated from the four investigations; each gates a real decision.

**Loop / runtime**
- ~~`GameFrame` body ordering (queue vs thinks)~~ — **resolved: think-first** (RE2:
  `Physics_RunThinkFunctions` at `0x1011ac1b`, then the sole `CEventQueue::ServiceEvents` at
  `0x1011ac34`).
- ~~Where usercmd processing (player movement) sits relative to the think pass~~ —
  **resolved: movement runs first**, and not inside `GameFrame` at all (RE21, §1 above).
- ~~Whether `ScheduleTask` enqueues into `CEventQueue`~~ — **resolved:** PyMethodDef body
  `FUN_10196ea0` reaches queue adapter `FUN_100ce0d0`, and service executes it through
  `FUN_100ce8a0`.
- Fixed vs. variable step is settled as **variable** from cvar absence; Elysium-Unreal may
  still *choose* a fixed physics tick instead — a deliberate divergence, not a fidelity break.

**Scripting (from `docs/vtmb/python_bridge.md`, still open)**
- `G` default-on-miss value (assumed `0`) — confirm in Ghidra; silently changes branching.
- Error-to-false path for the 89 malformed retail `.dlg` snippets — the `PyErr_Clear`
  around `PyRun_String`.
- `__setattr__` datamap-write path not yet decompiled.

**RPG rules (need `vampire.dll` / `client.dll`)**
- ~~Chargen point totals — how the quiz tallies convert to spendable dots per priority.~~
  **Resolved (RE25):** the pools are built in **`client.dll`**, one per category, as the
  clan-keyed `rules_tables.txt` `Subpool_*` value (zero on every shipped clan bar
  `Subpool_Disciplines` = 1) plus the tier table `2/1/0` (attributes) / `3/2/1` (abilities),
  routed by `Attrib_Order`/`Ability_Order`. See "Chargen" above.
- ~~Feat roll math — how `Base0 + Base1` forms the dice pool.~~ **Resolved (RE24):** the base list
  is variable-length and `Feats::FeatValue` sums it into the pool, clamped to `MaxValue` — see
  "What a feat evaluates to" above. The `"Normal"` weighting *curve* stays with
  `docs/vtmb/dice-system.md`; what is settled here is that the feat resolves to an index into that table.
- ~~`Cost.Raise` operand — is `Current_Rating` pre- or post-purchase?~~ **Resolved (RE25):
  pre-purchase**, and it is the stat's **base** value; `Sell(r) ≡ Buy(r−1)`. See "Buying a
  dot" above.
- ~~`AwardExperience` encoding — confirm `floor(value/100)` and the trailing `01`.~~ **Resolved
  (RE24):** `floor` confirmed, with the residue carried; the trailing `01` is not an encoding at
  all — give-once is the `m_ExpList` ledger. See "XP & leveling" below.
- ~~The Stamina→Health derivation.~~ **Resolved (RE24): there is none** — `Max_Health` is an
  authored stat and `Health` counts damage. See "Health is a damage counter" above.
- ~~Discipline `-1` sentinel — clamp/purchase gating for non-clan disciplines (the commented
  `Raise_Clan_Discipline` vs `Raise_Other_Discipline` dual formula).~~ **Resolved (RE25):**
  the sentinel gates the **row filter**, not the price — a by-value sheet section admits only
  `0 ≤ value < 6`. The dual formula was never implemented: neither key exists in `client.dll`
  or `vampire.dll`. See "Buying a dot" above.
- Clan banes — enforcement path/stacking of the attribute caps, Humanity doubling, frenzy
  mods, feeding restrictions (named as trait-effects, enforced in code) — see "Trait effects"
  above for the group/operator layer, whose loader and operator enum RE25 settled. **What
  stacks, and in what order, is still open.** RE24 adds one piece of the addressing:
  `CVStatRef` (see "How a trait is addressed" above) can also target a **ConVar**
  (`default_fov`, `vchar_skip_intro`, `vamplight_enabled`) or an **item name**
  (`item_w_claws`, `item_w_fists`), not only a stat or an `Fx_*` flag.

**Dialogue**
- ~~Whether the NPC-line col-4 "action" slot is ever evaluated as a *condition*.~~ **Resolved (9.1,
  by data):** it is an **action (exec)**, not a gate. `jack_tutorial.dlg`'s entry line carries col-4
  `G.Story_State = -3` — an assignment, which would syntax-error if evaluated as a condition. So an NPC
  line runs col-4 + col-5 when spoken; only a PC choice's col-4 is the eval gate.

**Save**
- The exact `.sav` block order/format if import of original saves is ever wanted (the four
  block handlers are identified; the wire layout is not decoded).
