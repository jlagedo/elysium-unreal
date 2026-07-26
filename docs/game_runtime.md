# VtMB game runtime — main loop, state, and the opening flow

How *Vampire: The Masquerade – Bloodlines* actually runs: the frame loop, the
three-layer split (engine / game DLL / Python), what state the game keeps and where
it lives, the character sheet and RPG rules, the New Game → chargen → trial → tutorial
sequence, and the dialogue/subtitle system.

This is the **integrating overview**. The mechanics it references are documented in
depth in the sibling docs — this one ties them together and adds the loop, the state
model, the RPG data, and the opening sequence:

- `docs/python_bridge.md` — the CPython 2.1 embedding, the datamap reflection API, the
  four call paths, `G`.
- `docs/entity_io.md` — the Source I/O bus (7-field outputs, `ScriptHide`/`Unhide`, the
  usable set, `use_icon`).
- `docs/level_transitions.md` — the map chain and the three spawn mechanisms.
- `docs/recovered/dice-system.md` — the World-of-Darkness d10 resolution.
- `docs/audio_pipeline.md` — the sound engine (Miles mixer, MS-ADPCM/MP3 codecs),
  the bespoke SoundScheme ambience/music system, DSP rooms, and the Godot mapping.
- `docs/animation_and_movers.md` — skeletal `.mdl` v2531 animation (bones, the RLE
  keyframe tracks, skinning) and brush movers (doors/buttons/spinners), + Godot mapping.
- `docs/rebuild-strategy.md` — the milestone roadmap this feeds.

Evidence is tagged where it matters: **[VtMB]** = read from the user's own DLLs
(strings/symbols/addresses); **[SDK]** = Source SDK 2013 reference; **[data]** =
plain-text game data; **[script]** = a level `.py`; **[inferred]** = reasoned, not yet
decompiled. Retail image bases: `engine.dll` `0x20000000`, `vampire.dll` `0x10000000`,
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
| Menu/UI | `GameUI.dll`, `vgui2.dll` | main menu, pause (see `docs/m0_menu_build.md`) |
| Script VM | `Bin/vampire_python21.dll` | stock CPython 2.1 — runs the story scripts |

### The host frame

`engine.dll` owns the classic Source `_Host_RunFrame` pipeline (all present as literal
profile-scope strings **[VtMB]**): `CEngine::Frame` → `_Host_RunFrame`, with fixed
sub-stages **Input → Server → Client → Sound → Render**. The **Server** stage runs
`SV_Frame` → the game DLL's per-frame entry.

The engine queries `vampire.dll` by versioned interface string **[VtMB]**:
`ServerGameDLL002`, `ServerGameClients002`, `ServerGameEnts001` (matching classes
`CServerGameDLL`/`CServerGameClients`/`CServerGameEnts` exported with RTTI). The
per-frame server entry is **`IServerGameDLL::GameFrame(bool simulating)`** **[SDK
eiface]** = `CServerGameDLL::GameFrame` `0x10571fc0` **[VtMB]**. `simulating` is false
when paused / no player — the port's "advance world state this frame or not."

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
when something fires them, through the four paths in `docs/python_bridge.md` (output
field 6, `logic_pythoncheck`, dialogue field 4/5, `ScheduleTask`).

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

`ScheduleTask(delay, "<source>")` posts a Python-typed event at `curtime + delay`
**[inferred, strong — string co-location]**; `GameFrame` drains the queue each frame and
`PyRun_String`s each due entry's stored source against `__main__`. **Delayed I/O,
`ScheduleTask`, and timed disciplines all converge on this one time-ordered queue.**

> **Correction to the existing docs.** `docs/python_bridge.md` and `docs/entity_io.md`
> attribute the `__main__.%s` wrap to `engine.dll`. The format string lives in
> **`vampire.dll`** (`0x1055e370`, inside the event-queue region) **[VtMB]**, which is
> where field-6 / `ScheduleTask` dispatch architecturally belongs. `engine.dll` owns
> only the VM boot (`Py_Initialize`, search path `\vampire\python`); the game-logic
> dispatch is all server-DLL.

**The Python "tick point" for a port:** drain a single time-ordered event queue inside
the server step, evaluating any script/I/O entry whose time ≤ current game time.
Everything else Python-side is call-triggered and needs no separate scheduler.

### Responsibility matrix

| Subsystem | Owner | Evidence |
|---|---|---|
| Host loop / frame pacing / timing cvars | engine.dll | `_Host_RunFrame*`, `CEngine::Frame`, `fps_max`/`host_timescale` **[VtMB]** |
| Rendering (world/models/lightmaps/decals) | engine.dll (+ client.dll HUD/FX) | `CRender::FrameBegin/End`, `VModelInfo` **[VtMB]** |
| Physics / collision | `VPHYSICS.DLL` (loaded by engine); vampire.dll owns movetypes/think-driven physics | `VPhysicsCollision007` **[VtMB-engine]**; `CBaseEntity::PhysicsSimulate`, `CPhysicsHook` **[VtMB-vampire]** |
| Player movement | vampire.dll (`CGameMovement`) | `sv_stepsize`, `speed_runbase` **[VtMB]**; ported — `docs/source_movement.md` |
| Sound | engine.dll (`S_*`/`SNDDMA_*`), triggered by server | `_Host_RunFrame_Sound` **[VtMB]**; `ambient_generic`/`PlaySound` **[data]** |
| Entity spawning & datamap | vampire.dll | `CBaseEntity::Spawn`, datamap build `FUN_100a22f0`, `GetDataDescMap` vtable+0x148 **[VtMB/doc]** |
| Entity I/O dispatch (delays) | vampire.dll `CEventQueue` | `EventQueue`, output field 3 **[VtMB/doc]** |
| Think scheduling | vampire.dll | `SetNextThink`/`m_flNextThink`/`Physics_RunThinkFunctions` **[VtMB]** |
| AI / NPC / nav | vampire.dll | `npc_VHumanCombatant` 28-input surface **[doc]** |
| RPG rules (dice/combat/disciplines/feats) | vampire.dll (compiled), parameterised by data | `Failed to match Discipline Event` **[VtMB]**; `CalcFeat`/`BumpStat` **[doc]**; `dice-system.md` |
| Quests | vampire.dll state + Python logic | `SetQuest`/`GetQuestState` **[doc]** |
| Dialogue | vampire.dll eval, `.dlg` data, Python expr half | `CDialogDependency::TestPython` `0x10563a90` **[VtMB]** |
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
flags level scripts *read* are written only by dialogue (`docs/python_bridge.md`) — so
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

- **Attributes** (9; Min 0/Max 10, Default 1; `Raise = Current_Rating * 4`):
  `Strength, Dexterity, Stamina` · `Charisma, Manipulation, Appearance` · `Perception,
  Intelligence, Wits`. Physical/Social/Mental is an ordering enum (`Attrib_Order`), not
  nested blocks. Charisma/Manipulation/Appearance carry `"Disabled" "1"` (exposed but
  flagged; Appearance still feeds Seduction/Nosferatu).
- **Abilities** (12; Min 0/Max 5, Default 0; `New 3`, `Raise = Current_Rating * 3`):
  Talents `Brawl, Dodge, Intimidation, Subterfuge` · Skills `Firearms, Melee, Security,
  Stealth` · Knowledges `Computer, Finance, Investigation, Academics` ("Scholarship").
  Talents/Skills/Knowledges is `Ability_Order_Lookups`, not nested. Several disabled.
- **Disciplines** (Min 0/Max 5; `New 10`, `Raise = Current_Rating * 5`): `Animalism,
  Auspex, Blood_Healing, Celerity, Corpus_Vampirus, Dementation, Dominate, Fortitude,
  Obfuscate, Potence, Presence, Protean, Thaumaturgy` (+ Hunter-mode ones). Each has
  `*_Targeting*` sub-stats. A discipline a clan **cannot** learn reads **`-1`** (sentinel);
  owned-but-unraised reads `0`.
- **Active_Disciplines** — parallel `Active_*` stats holding the currently-toggled level;
  drive per-frame effects (e.g. `Active_Obfuscate` gates stealth-attack bonuses).
- **Derived / bookkeeping** (flat `Stat`s, `Raise 10000` = unbuyable): `Clan`
  (`NameFunc ClanNameFunc`), `Gender` (0=F,1=M), `BloodPool` (0–15, Def 10;
  `BloodPoolEmptyFunc` on 0), `BloodPool_Max`, `Health`/`Max_Health` (Def 100),
  `Health_Aggravated_Dmg`, `Generation` (Def 0; drives three lookup tables),
  `Armor_Rating`, `Level` (1–3), `Humanity` (0–10, Def 7, `Raise = Current_Rating * 2`),
  `Masquerade` (0–5 **violation counter**, Def 0, 5 = game over), `Experience` (Def 0),
  `Soak_Pool`, `Frenzy_Check_Mod`, `Starting_Equipment`, `AutoLevel_Template`, …

**Divergences from tabletop VtM to record:** `Willpower` is present but fully commented
out; there are **no Virtues** (Conscience/Self-Control/Courage) and **no Backgrounds**
category anywhere. Money is its own field (`MoneyAdd`/`CurrentMoney`), not a `Stat`.

### Feats — the derived-roll layer (`feats.txt`)

`.dlg` skill-checks and verb rolls are **Feats**, not raw stats. A Feat is an
`Attribute + Ability` pair resolved via `pc.CalcFeat("<feat>")` (case-insensitive), each
naming a `"Normal"` dice weighting table (where `dice-system.md`'s d10 resolver plugs in):

| Feat | = Attribute + Ability | Feat | = Attribute + Ability |
|---|---|---|---|
| Lockpicking (`Intrusion`) | Dexterity + Security | `Persuasion` | Charisma + Academics |
| `Sneaking` | Dexterity + Stealth | `Seduction` | Appearance + Subterfuge |
| `Hacking` | Wits + Computer | `Intimidate` | Intelligence + Intimidation |
| `Inspection` | Perception + Investigation | Unarmed (`Close_Combat_Brawl`) | Brawl + Strength |
| `Research` | Academics + Intelligence | Melee (`Close_Combat_Melee`) | Melee + Strength |
| `Haggle` | Finance + Manipulation | Ranged (`Ranged_Combat`) | Firearms + Perception |

So a `.dlg` gate "Persuasion 7" means `CalcFeat("Persuasion") >= 7`.

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

Display enum `ClanNameFunc` (`strings.txt`): `1 Brujah … 7 Ventrue`. **Two indexings
exist** — the player-entity `pc.clan` uses **2–8** (Brujah 2 … Ventrue 8, per the level
scripts **[script]**), while `clandoc`/`rules_tables` use another. Banes are named as
`ClanEffect`/`FrenzyEffect` trait-effects (`traiteffects000.txt`) but enforced in
`vampire.dll`.

### Chargen — a personality quiz (`charcreatewizard.txt`)

Not a manual point-buy: the wizard asks `Popup` questions whose answers increment
abstract Traits (`Combat, Non-Combat, Social, Stealth, Intellect, Unarmed, Armed,
Ranged`), then scores each clan against the tallies (`ConnectionScores`,
Primary/Secondary/Tertiary weighting) to **auto-suggest a clan**. Point pools are the
priority tiers in `stats.txt` (`Subpool_Attribute_* = 2/1/0 +Kine 1/1/1`;
`Subpool_Ability_* = 3/2/1 +Kine 1/1/1`); `Subpool_Disciplines = 1` for each clan
discipline. The exact per-priority dot totals are computed in `vampire.dll` (open).

### XP & leveling

- **Spend** (`Costs.Raise` vs `Current_Rating`): attribute `×4`, ability `×3`, discipline
  `×5`, humanity `×2` XP per dot (whether `Current_Rating` is pre- or post-purchase is
  open).
- **`experience_table.txt`** is the **quest-reward → XP** map (not a cost table):
  `pc.AwardExperience("<Key>")` looks up the key; **XP = floor(value/100)** (trailing `01`
  = give-once) **[inferred]**.
- **`levelingtemplate_000.txt`** — ordered auto-level templates (NPCs + CharGen builds);
  the engine "buys up" listed traits in order as points allow.

### Masquerade / Humanity / Blood / Frenzy (`rules.txt`, `stats.txt`)

- **Masquerade** — the `Masquerade` stat is a 0–5 violation counter (5 = loss);
  `ChangeMasqueradeLevel(delta)` / `GetMasqueradeLevel()`.
- **Humanity** — 0–10 (Def 7); frenzy checks roll against it; `HumanityAdd(delta)`.
  Toreador doubles gains / others double losses via clan trait-effects.
- **Blood** — `BloodPool` 0–15; `VampHeal_Info` heals `1%` Max Health per `2.0s`,
  `BloodToHealthRatio 10` (1 blood → 10 HP). Generation tables (indexed by `Generation`)
  cap trait rating, pool max, and blood/turn.
- **Frenzy** — `VampFrenzy_Info`: triggers over `Dmg_Amount 20` (normal) / `15`
  (aggravated), or by hunger below `BloodPool_Min_For_Hunger 3`; `Default_Difficulty 5`
  against Humanity, plus `Frenzy_Check_Mod` (clan bane) and a low-blood penalty;
  cooldowns 190s success / 120s failure.

The die itself (pool → tier, 10-again, botch table) is `docs/recovered/dice-system.md`.

## 4. The opening flow — New Game → chargen → trial → tutorial

New Game does **not** load the tutorial directly (`docs/level_transitions.md`). The real
chain, all via landmark transitions:

```
New Game
  → map sp_genesisdevice_1          levelscript "demo"      (character creation)
       newplayer trigger_once → ccmd.createplayer + G.Story_State = -5
       walk into boogieout (trigger_changelevel, landmark "newgame")
  → map sp_theatre                  levelscript "theatre"   (embrace + LaCroix trial)
       embrace cutscene → courtroom trial (5 choreo scenes) → walk-out → fade
       walk_out_cam_k final keyframe: ScriptUnhide tutorial_change + controls off + fade
  → trigger_changelevel tutorial_change (map sp_tutorial_1, landmark "tutorial")
  → map sp_tutorial_1               levelscript "tutorial"  (Jack's guided tutorial)
       LeaveTutorial() → trig_leave_tutorial → sm_pawnshop_1 (Santa Monica — real game start)
```

> `sp_ninesintro` is **not** part of the opening — it is the later "arrive in Downtown
> LA" cutscene. It only shows up because it was pre-exported under `tools/out/`.

### Character generation — `sp_genesisdevice_1`

A tiny abstract limbo map (~4.6 KB entity lump, skyname `hav`, dark fog) — no gameplay
geometry, just `info_player_start`, a light, and one trigger. Troika's codename (a *Star
Trek* device that creates life) for the chargen staging space.

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
Walking forward crosses `boogieout` (`trigger_changelevel → sp_theatre`, landmark
`newgame`); **clan is carried on the player entity across the landmark**, read later.

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
  legacy/parallel path. (Corrects the assumption in `level_transitions.md` that it drives
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
arrival itself: the first beat is armed by the `trig_off_porch` `trigger_multiple`, whose
`OnEndTouch` (walking off the theatre porch) sets `Jack,WillTalk 1`, calls
`Jack,StartPlayerDialogRemote 256` — opening `dlg/Main Characters/jack_tutorial.dlg` —
and spawns `blueblood_maker` plus `pc_0,CreateControllerNPC`.

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
| chargen → theatre | `boogieout` trigger_changelevel | `map sp_theatre`, landmark `newgame` (walk in) |
| embrace | trigger StartTouch | `embrace_o_matic,Start` + camera + gender `Test` |
| trial | `start_courtroom` trigger_once | `courtroom_scene_relay,Trigger` + `courtroomSire()` |
| trial → walk-out | camera keyframe | `OnReachedKeyframe → scene_over_relay → walk_out_relay` |
| **theatre → tutorial** | `walk_out_cam_k` final keyframe | `tutorial_change,ScriptUnhide` + `controls,Deactivate` + `fade_to_tutorial,Fade`; then the (StartHidden) `tutorial_change` trigger_changelevel transitions. **Not** `tutorialLoad()`. |
| tutorial entry | landmark `tutorial` placement (nothing fires on arrival) | Jack stands cued-silent (`WillTalk 0`); `teleport_very_beginning`'s `OnEnterMapHere` wires are inert (output is `info_landmark`-only) |
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

## 5. Dialogue, NPC conversation & subtitles

Conversations are **`.dlg`** files (147 loose in `Unofficial_Patch/dlg/<hub>/`, ~50,393
rows) **[data]**. No `.dlg` parser exists in the repo yet.

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
| 6–11 | unused (always empty). |
| **12** | **Malkavian-PC line** — the Malkavian variant of this row's text (NPC subtitle or PC choice), shown *instead of* col 1/2 when the player is Malkavian (empty = no variant). Not a "short label": across the corpus 9,576 rows carry both and col 12 differs from col 1 in **97%**, and the differences are Malkavian-speak ("Behave, I am your kind of monster" vs "Calm down, I'm not one of them"). |

Animation/camera/gesture are **not** in the `.dlg` (cols 6–11 empty) — they live in the
`.vcd` (§cinematics).

### Runtime / branching

1. Open at the first NPC line with content (the leading blank NPC lines are not real turns).
   An NPC line's col-4 is an **action**, run with col-5 when the line is spoken — not a gate
   (9.1, resolved by data; see §7). VtMB's exact opener-selection among gated leading NPC lines
   is not yet RE'd; the runtime uses the first-with-text rule as an interim.
2. After an NPC line **N** is spoken (running its col-4/5 actions), gather the contiguous
   run of PC rows after it (N+1, N+2, … up to the next `#`). Show each PC row whose col-4
   condition is true as a menu entry (text = the Malkavian col 12 when the PC is Malkavian, else col 1/2).
3. Player picks → its col-5 action runs → jump to the NPC line in its col-3 link → repeat.
4. **Link `0` ends** the conversation. `(Auto-End)`/`(Auto-Link)` are editor-generated
   silent-transition placeholders.

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

Field-4 conditions front a `dlgexpr` **skill-check** (`docs/python_bridge.md` grammar):
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

Actions (col 5, and NPC col 4) run against the live `__main__`:
`npc.SetDisposition("lay", 1)`, `pc.SetQuest("Mercurio", 2)`, `G.Mercurio_Know = 1`,
multi-statement `pc.SetQuest("Barabus",2); G.Story_State = 55; G.Barrabus_Exit = 1`.
`pc`/`npc` are engine-bound Character objects, `G` the flag-bag, `IsClan`/`OneOfSet`/
`IsMale` are `vamputil.py` helpers — all resolved through the datamap reflection layer
(`docs/python_bridge.md`), so a port needs a `dlgexpr` evaluator over the `G`/Character/
`vamputil` surface, not a bespoke dialog API.

### Cinematics summary

No pre-rendered story cutscenes. Every cinematic is a **`logic_choreographed_scene`**
driving a `.vcd` over `.mdl` animation (`BaseAnim`/`MaleAnim`/`FemaleAnim` name the shared
cinematic model; the cast binds **by actor name**, not through `target1..4`) with scripted
`camera_keyframe`/`camera_track` camera (`PlayAsCameraPosition`/`PlayAsCameraTarget`,
`RestoreCameraToPlayerControl`) — the scene format's own camera events are unused.
Captions = the `.vcd` `speak` token → `.dlg` line-id join. Deferred beats use
`ScheduleTask(delay, "<pysource>")`. Format + event semantics: `choreographed_scenes.md`.

## 6. Where Elysium is today vs. this (the gap)

The runtime currently **shortcuts** the whole opening: `GameManager.NewGame()` sets
`Launch.Map = sp_tutorial_1` and loads it as a bare `map` — spawning at
`info_player_start` (`GameScene`, `docs/level_transitions.md`), with a dev
`SpawnOverride` pinning the warehouse alley. There is **no chargen, no theatre trial, no
Jack dialogue, no beat machine, no landmark chain** yet. Mapping the real flow onto the
`docs/rebuild-strategy.md` milestones:

- **Chargen** — a native character-creation screen replacing `ccmd.createplayer`, writing
  the sheet fields (clan 2–8, gender, `base_*`) onto the player entity. New; not in the
  current milestones as a UI, but the RPG data (§3) is the backing.
- **The event queue + think loop (M2 backbone)** — the one time-ordered queue draining
  each server step, serving delayed I/O *and* deferred script entries, plus a
  `SetNextThink` analog. This is the single most load-bearing runtime primitive.
- **Landmark transitions** — `trigger_changelevel` + `info_landmark` relative placement
  (the opening is four maps chained this way), replacing the bare-`map` spawn.
- **`logic_choreographed_scene` + camera tracks (M6)** — for the embrace/trial cinematics
  and the tutorial's scripted beats.
- **`.dlg` runtime + `dlgexpr` evaluator (M4)** — the 13-column loader (§5), the branch
  machine, and the shared expression core over `G`/Character/`vamputil`.
- **`G` + quests + the block save** — a dynamic `{name:int}` `G` store, a `{name:int}`
  quest map, and the four-block save (entity fields / event queue / think contexts / `G`).

## 7. Open questions

Consolidated from the four investigations; each gates a real decision.

**Loop / runtime**
- ~~`GameFrame` body ordering (queue vs thinks)~~ — **resolved: think-first** (RE2,
  `roadmap-archive.md`: `Physics_RunThinkFunctions` at `0x1011ac1b`, then the sole
  `CEventQueue::ServiceEvents` at `0x1011ac34`). Still open in the same body: where usercmd
  processing (player movement) sits relative to the think pass — **RE21**
  (`DumpFuncs funcs=10571fc0`).
- Confirm `ScheduleTask` truly enqueues into `CEventQueue` (vs. a separate list) — decompile
  the thunk behind PyMethodDef `ScheduleTask` `0x10590530`.
- Fixed vs. variable step is settled as **variable** from cvar absence; a port may still
  *choose* Godot's fixed physics tick — a deliberate divergence, not a fidelity break.

**Scripting (from `docs/python_bridge.md`, still open)**
- `G` default-on-miss value (assumed `0`) — confirm in Ghidra; silently changes branching.
- Error-to-false path for the 89 malformed retail `.dlg` snippets — the `PyErr_Clear`
  around `PyRun_String`.
- `__setattr__` datamap-write path not yet decompiled.

**RPG rules (need `vampire.dll`)**
- Chargen point totals — how the quiz tallies convert to spendable dots per priority.
- Feat roll math — how `Base0 + Base1` forms the dice pool and what the `"Normal"` weighting
  curve is (tie to `dice-system.md`).
- `Cost.Raise` operand — is `Current_Rating` pre- or post-purchase?
- `AwardExperience` encoding — confirm `floor(value/100)` and the trailing `01`.
- Discipline `-1` sentinel — clamp/purchase gating for non-clan disciplines (the commented
  `Raise_Clan_Discipline` vs `Raise_Other_Discipline` dual formula).
- Clan banes — enforcement path/stacking of the attribute caps, Humanity doubling, frenzy
  mods, feeding restrictions (named as trait-effects, enforced in code).

**Dialogue**
- ~~Whether the NPC-line col-4 "action" slot is ever evaluated as a *condition*.~~ **Resolved (9.1,
  by data):** it is an **action (exec)**, not a gate. `jack_tutorial.dlg`'s entry line carries col-4
  `G.Story_State = -3` — an assignment, which would syntax-error if evaluated as a condition. So an NPC
  line runs col-4 + col-5 when spoken; only a PC choice's col-4 is the eval gate. (`decisions.md`
  2026-07-24.)

**Save**
- The exact `.sav` block order/format if import of original saves is ever wanted (the four
  block handlers are identified; the wire layout is not decoded).
