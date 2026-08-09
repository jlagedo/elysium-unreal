# Controls, keybinds and control options (VtMB)

The input surface of *Vampire: The Masquerade – Bloodlines*: what the game can be told to do,
which key says it, where that mapping is stored, and what the options UI exposes. Engine-neutral
VtMB facts — the *tuning numbers* behind look and movement (sensitivity, pitch clamps, speeds,
FOV) live in `docs/vtmb/source_movement.md`, and the `+use` look-cursor / `use_icon` machinery lives in
`docs/vtmb/entity_io.md`. This doc owns the **binding layer**.

## Provenance

Everything below is read out of the user's own install.

- `scripts/kb_act.lst`, `scripts/kb_def.lst`, `scripts/kb_trans.lst`, `scripts/kb_keys.lst` —
  the bindable-action list, the default binds, and the two key-name tables.
- `cfg/*.cfg` — `default.cfg`, `config.cfg`, `user.cfg`, `autoexec.cfg`, `valve.rc`
  (mirrored verbatim to `$ELYSIUM_EXPORT_ROOT/cfg/` by `pipeline/src/elysium_pipeline/exporters/UE_extract_cfg.py`).
- `resource/optionssub*.res` + `resource/gameui_english.txt` — the options-dialog layout and
  its strings.
- Decompiles and byte-scans of `Bin/engine.dll`, `Vampire/cl_dlls/client.dll`,
  `Vampire/cl_dlls/GameUI.dll`, `Bin/vgui2.dll`, `Vampire/dlls/vampire.dll` via
  `research/tooling/ghidra/driver/run.ps1 -Script DumpGrep`.
- `Unofficial_Patch/python/vamputil.py` — the patch's own runtime rebinding.

A few **player-facing behaviours** below are read from community documentation rather than from
the install: the two GameFAQs guides under `$ELYSIUM_WORK_ROOT/research/reference-source/`
(rezzzman's *Mod Developer Guide* and the walkthrough). Each such claim is marked where it
appears, with the evidence that would confirm it. Command *existence* is always confirmed against
the shipped binaries.

Both the **retail** (VPK) and **Unofficial Patch 11.5** (loose, patch-first) copies of every
data file are cited where they differ; the patch shadows retail for all four `kb_*.lst` and all
`cfg/*.cfg`.

## The model in one paragraph

VtMB inherits Quake's binding model wholesale. A **key** (named by a string) is bound to a
**console command string**; pressing the key executes that string. A command beginning with `+`
is a *button*: `+jump` runs on key-down and `-jump` on key-up, and the engine tracks which
physical key started the press so overlapping keys release correctly (`"Three keys down for a
button '%c' '%c' '%c'!"`, `client.dll`). Everything else runs once per key-down. There is no
separate "action" abstraction anywhere in the engine — an action *is* a console command string,
which is why the options UI's action list is a shipped text file (`kb_act.lst`) rather than
compiled data, and why `bind "f" "vm_feed"` can point at a **user-defined alias** just as
legitimately as at a compiled command.

Three layers, three owners:

| Layer | Owner | Artifact |
|---|---|---|
| key name ↔ keynum | `engine.dll` | compiled table at `0x201a56c0` |
| what is bindable | `client.dll` / `vampire.dll` / `engine.dll` ConCommands + `cfg` aliases | — |
| what is bound | `cfg/config.cfg` (live), seeded from `cfg/default.cfg` | Valve console text |

## Key names and keynums

`engine.dll` carries the `keyname_t` table at VA **`0x201a56c0`** (imagebase `0x20000000`),
stride **12 bytes**: `char *name; int _unused(0); int keynum`, null-terminated. Every printable
ASCII key binds by its **literal character** (`bind "a"`, `bind "1"`, `bind "["`) and so needs no
table entry; the table names only the non-printable keys — plus one alias, `SEMICOLON`, because
`;` is the console's command separator and cannot appear as a bare bind key.

| keynum | name | keynum | name |
|---|---|---|---|
| 9 | `TAB` | 147 | `INS` |
| 13 | `ENTER` | 148 | `DEL` |
| 27 | `ESCAPE` | 149 | `PGDN` |
| 32 | `SPACE` | 150 | `PGUP` |
| 59 | `SEMICOLON` (alias for `;`) | 151 | `HOME` |
| 127 | `BACKSPACE` | 152 | `END` |
| 128 | `UPARROW` | 160–174 | `KP_HOME`, `KP_UPARROW`, `KP_PGUP`, `KP_LEFTARROW`, `KP_5`, `KP_RIGHTARROW`, `KP_END`, `KP_DOWNARROW`, `KP_PGDN`, `KP_ENTER`, `KP_INS`, `KP_DEL`, `KP_SLASH`, `KP_MINUS`, `KP_PLUS` |
| 129 | `DOWNARROW` | 175 | `CAPSLOCK` |
| 130 | `LEFTARROW` | 203–206 | `JOY1`–`JOY4` |
| 131 | `RIGHTARROW` | 207–238 | `AUX1`–`AUX32` |
| 132 | `ALT` | 239 | `MWHEELDOWN` |
| 133 | `CTRL` | 240 | `MWHEELUP` |
| 134 | `SHIFT` | 241–245 | `MOUSE1`–`MOUSE5` |
| 135–146 | `F1`–`F12` | 255 | `PAUSE` |

`ALT`, `CTRL` and `SHIFT` are **single keys, not modifiers** — there is no chord syntax. A bind
is one key to one command string; a two-key combination is expressible only as an alias chain.

### The second key-code space

`scripts/kb_trans.lst` (UTF-16, `<num>\t"NAME" "NAME"`) numbers the same keys **differently** —
`MOUSE1` is 200 there, not 241. It is not the engine's table: `Bin/vgui2.dll` is its only
consumer, which reads it as the VGUI `KeyCode`→display-name localization table (it emits
`KEY_TAB`-style tokens). VGUI key codes and engine bind keynums are separate enums; do not
cross-map them.

`scripts/kb_keys.lst` (retail VPK only, ASCII, a fourth `DEFAULTCOLOR` field) is the same table
in an older format and is referenced by **no binary in the install** — dead shipped data. The
patch ships `kb_trans.lst` and not `kb_keys.lst`.

## What is bindable — the command inventory

`client.dll` owns the input/camera/joystick/mouse commands; `vampire.dll` (server) owns the
gameplay verbs; `engine.dll` owns console/system verbs. `+`/`-` pairs are marked ±.

### Movement

| Command | Effect |
|---|---|
| ±`forward`, ±`back` | move |
| ±`moveleft`, ±`moveright` | strafe |
| ±`moveup`, ±`movedown` | swim / ladder |
| ±`left`, ±`right` | turn (keyboard yaw, `cl_yawspeed`) |
| ±`lookup`, ±`lookdown` | keyboard pitch (`cl_pitchspeed`) |
| ±`speed` | walk modifier (held) |
| ±`strafe` | strafe modifier — turn keys become strafe while held |
| ±`duck` | crouch |
| ±`jump` | jump |
| ±`klook`, ±`mlook`, ±`jlook` | keyboard / mouse / joystick look modes |
| `centerview`, `force_centerview` | recentre pitch |
| `impulse` | classic impulse channel |

`in_mlook`, `in_jlook`, `in_graph` are the persisted toggle states; `default.cfg` ends with a
bare `+mlook` line, which is why mouse-look is on from a cold start and `m_side`/`m_forward`
(mouse-as-movement factors) are inert.

### Combat and items

The command list below is the input vocabulary. Its command → usercmd → predicate → gameplay
action → effect routing is consolidated in `docs/vtmb/gameplay-verbs.md`; damage after an
accepted attack is in `docs/vtmb/combat-and-damage.md`.

| Command | Effect |
|---|---|
| ±`attack`, ±`attack2` | primary / ordinary secondary-fire buttons |
| ±`wpn_secondaryatk` | held composite: the melee-block button plus ordinary `attack2` |
| ±`reload` | reload |
| ±`use` | the world-interaction verb (see `docs/vtmb/entity_io.md` for the trace, `use_icon`, `PassesUseFilter`) |
| `slot1`…`slot8` | inventory categories (see below) |
| `invnext`, `invprev`, `lastinv` | cycle / last selection |
| `holster`, `dropitem` (`dropitem %d`), `inven_drop_curr`, `inven_drop %d` | holster / drop |
| `toggleinven`, `toggleuiside`, `showinventory <n>` | selection-UI verbs |
| `vhotkey #N` / `vhotkey_int %d` | fire hotkey slot N (client) |
| `showhotkeys`, `hidehotkeys`, `sethotkeys`, `init_hotkeys` | the hotkey window (`VHotkeysUI`) |
| `vdiscipline #N`, `vdiscipline_int <N>`, `vdiscipline_last`, `vdiscipline_endall` | disciplines (server-side, `vampire.dll`; `vdiscipline_int` is also in `client.dll`) |
| ±`feed` | feeding |
| `zoom_sensitivity_ratio` | extra mouse scale while zoomed |

The `slotN` numbering is the one the patch's `kb_act.lst` documents: `slot1` Disciplines,
`slot2` Melee, `slot3` Ranged, `slot4` Thrown, `slot5` Armor/Clothing, `slot6` General
Inventory. Retail exposes only `slot2`/`slot3`/`slot5`/`slot6` in its action list.

#### The quickbar is select-then-cast

Activating a power is **two stages, two console commands**:

1. `vhotkey #N` **selects** slot N into the queue drawn below the blood meter;
2. `vdiscipline_last` **casts** whatever is selected — retail's `MOUSE2` bind.

The selection is **deferred by one frame**. Both commands in one statement cast the *previously*
selected power and only then switch, so a one-key cast has to spend a frame between them —
`bind "key" "vhotkey #X; wait 1; vdiscipline_last"`.

`showhotkeys` (`k`) opens `VHotkeysUI`, where the ten slots are assigned. A slot holds a **weapon,
a discipline or a blood pack** — it is a general quickbar, not a discipline bar — and a tiered
discipline exposes its level in a drop-list on the slot.

Two verbs cast **without** the `vdiscipline_last` confirm, reaching tier 1 only:
`vdiscipline_int <N>` indexes the compiled discipline table, and `vdiscipline #N` takes the Nth
discipline on the character sheet. Neither reaches the upper tiers of Animalism or Thaumaturgy.
Passive disciplines (Bloodbuff, Celerity) therefore fire on the keypress alone.

`vdiscipline_endall` (`F8`) ends every active discipline, so some are **sustained states** rather
than instants.

`toggleuiside` (`t`, relabelled "Toggle Discipline/Weapon" by the patch, `client.dll`) switches
**what the mouse wheel cycles** — weapons or disciplines. `invnext`/`invprev` act on whichever
list is selected; the accompanying UI shift is cosmetic.

*Community-sourced, pending decompilation:* the one-frame deferral, the slot contents, and the
`vdiscipline_int` index table (`0` Nightwisp Ravens, `1` Auspex, `3` Celerity, `4` Bloodbuff,
`5` Hysteria, `6` Trance, `7` Fortitude, `8` Obfuscate, `9` Potence, `10` Presence, `11` Protean,
`12` Bloodstrike). Decompiling `vampire.dll`'s `vdiscipline_int` handler would confirm the table
and the argument form; `client.dll`'s `vhotkey_int` handler would confirm the deferral.

#### What `+attack` does — directional combos and the block

Unarmed and melee share one move set, selected by **`+attack` plus the movement direction held
with it**: three directional combos, plus a fourth for `+attack` with no direction held.

**The block verb is `+wpn_secondaryatk`** [VtMB decompiled]. The pinned client registers two
independent `kbutton_t` pairs. `+attack2` alone packs held bit `0x800` and press edge `0x01000000`.
`+wpn_secondaryatk` presses a second button, then jumps into that same `+attack2` handler; its
release path clears both in the same order. The second button packs held bit `0x08000000`, so this
command is a held composite, not a persistent client toggle.

On the server, player `+0x2088` is the current-button field and `0x10160ec0` is its only direct
`0x08000000` test. The predicate additionally requires ground contact and an active weapon whose
capability mask intersects `0x18000`; success makes the classifier return compact action code `13`,
and the ordinary selector requests `ACT_PREBLOCK`. `+attack2` alone cannot set that block bit.
Blocking remains a real mechanic with the `Defence` feat behind it, while the ordinary attack2 bits
continue through weapon secondary-fire policy. `uv run elysium research input_action_survey`
hash-pins both DLLs and every instruction span in this chain. *(Directional move set:
community-sourced.)*

### Camera

| Command | Effect |
|---|---|
| `togglecamera`, `thirdperson`, `firstperson`, `camortho` | view mode |
| ±`camin`, ±`camout` | dolly |
| ±`campitchup`, ±`campitchdown`, ±`camyawleft`, ±`camyawright` | orbit |
| ±`cammousemove`, ±`camdistance`, ±`commandermousemove` | mouse-driven camera |
| `snapto`, `cam_command` | camera control |

Camera cvars: `cam_snapto`, `cam_idealyaw`, `cam_idealpitch`, `cam_idealdist` (85),
`cam_targetangle` (15), `cam_yaw`, `cam_collide`, `cam_trace_radius` (9.0), `cam_fadestart`
(32) / `cam_fadeend` (18), the clamp set `c_minpitch`/`c_maxpitch`/`c_minyaw`/`c_maxyaw`/
`c_mindistance`/`c_maxdistance`/`c_orthowidth`/`c_orthoheight`, the spring damper
`cdamp_on`/`cdamp_hookesconstant`/`cdamp_hookesconstantwall`/`cdamp_springlength`/`cdamp_maxdist`,
and the scripted-camera families `camfeed_*`, `camdead_*`, `camseduct_*` (feeding, death and
seduction shots). `camera_prefs` and `camera_weaponswitch` are archived player preferences;
their recovered bit semantics and runtime behavior are canonical in `docs/vtmb/camera-view-modes.md`.

### UI, game and system

| Command | Owner | Effect |
|---|---|---|
| ±`chareditor`, `togglechareditor` | client | character sheet |
| ±`questlog` | client | quest log |
| `dlghist`, `dlgscrlup`, `dlgscrldn` | client | dialogue history + scroll |
| `cancelselect` | engine/client/GameUI | the ESC verb (close panel / open menu) |
| `togglemainmenu` | engine/client | menu |
| `pause` | engine/vampire | pause |
| `save quick`, `load quick` | engine | quicksave / quickload |
| `snapshot` | engine | screenshot |
| `toggleconsole` | engine | console |
| `bind`, `unbind`, `unbindall`, `alias`, `exec`, `echo`, `incrementvar`, `writeconfig` | engine | the console/config verbs |
| `vstats`, `vdmg`, `giftxp`, `faith`, `blood`, `god`, `noclip`, `skill`, `player_sequence`, `infobar_message` | vampire | gameplay/debug verbs the patch's aliases call |

Several real commands are content/UI machinery rather than player binds:

| Command | Owner | Effect |
|---|---|---|
| `teleport_player <targetname>` / `<x> <y> <z>` | vampire | move the player to a named entity or coordinate; missing names report `Could not find entity named %s` |
| `player_immobilize` / `player_mobilize` | vampire | take and return player control for a scripted moment — a **server-side lock on the player**, not a client input-mode change |
| `v_setpause`, `v_unpause` | client | take/release the character-panel modal hold |
| `vskip_intro` | vampire | skip the current intro scene; not the `vchar_skip_intro` chargen-footer ConVar |
| `createplayer` | client | show `CharEditPanel` in character-creation mode |

**`vphysicshand` is a dead bind.** Both `default.cfg` files and the patch's `kb_def.lst` bind
`p` to it, but the literal appears in no shipped binary and in no level script — the physics-hand item
(`vdata/items/item_s_physicshand.txt`) exists, the console verb does not. Under the patch, `p`
is rebound to `skip` anyway (last bind wins).

### Open: what conversation does to held input

Conversation runs under its own client mode, `CClientModeDialog` (`client.dll`). Whether it
cancels held movement buttons or merely ignores them is **not established** — the command
strings near its RTTI descriptor are the ordinary ConCommand name pool
(`centerview` at `0x10282b24` is referenced only by its own registration, `FUN_1018f5e0`), so
string adjacency proves nothing here. Decompiling `CClientModeDialog`'s mode-enter/mode-exit
vtable slots would settle it.

## Where bindings live — files and load order

```
engine startup (Host_ReadConfiguration, engine.dll FUN_2008b660 @0x2008b660)
  ├─ cfg/config.cfg absent?  → seed it from cfg/default.cfg
  ├─ exec config.cfg        ; Cbuf_Execute
  └─ exec user.cfg          ; Cbuf_Execute
engine startup, later                          (engine.dll FUN_2008ec20)
  └─ exec valve.rc
        ├─ // exec default.cfg      ← commented out in the shipped valve.rc
        ├─ exec language.cfg
        ├─ exec joystick.cfg
        ├─ exec autoexec.cfg
        └─ stuffcmds                ← replays +command-line console statements
server, per map / difficulty                   (vampire.dll)
  └─ exec skill<N>.cfg, exec game.cfg, exec map_edit.cfg
```

Each file's role:

| File | Role |
|---|---|
| `cfg/default.cfg` | Troika's factory defaults — the full bind set + every archived cvar. Read only to seed a missing `config.cfg`. |
| `cfg/config.cfg` | the **live** settings file; rewritten by the engine on exit |
| `cfg/user.cfg` | user/mod overrides, exec'd *after* `config.cfg` so it wins. The Unofficial Patch's whole alias vocabulary lives here, including the Basic/Plus switch `alias patchtype "setPlus()"` (see `docs/vtmb/python_bridge.md`). |
| `cfg/autoexec.cfg` | late overrides via `valve.rc` |
| `cfg/joystick.cfg` | controller setup — **exec'd but not shipped**; `valve.rc` references it and no such file exists in retail or the patch |
| `cfg/language.cfg` | `sv_language 0` |
| `cfg/skill1.cfg` | difficulty-scaled damage/health table (server) |
| `cfg/multiplayer.cfg` | the vestigial MP/coop block |

`Host_WriteConfiguration` (`engine.dll` `0x2008b760`) writes `config.cfg` as: `unbindall`, then
every binding as `bind "<KEY>" "<command>"`, then every `FCVAR_ARCHIVE` cvar, then a trailing
`+mlook` and/or `+jlook` if those look modes are engaged. That trailing `+mlook` in the shipped
`default.cfg` is the same mechanism, not a hand-authored line.

### Bindings are rewritten at runtime, from Python

`config.cfg` is not only written at exit — the patch **reads it back and rebinds live keys while
the game runs**. `vamputil.py`'s `FixKeyBindings()` scans `cfg/config.cfg` for whichever key
carries `vdiscipline_last`, writes `bind <KEY> "vm_discipline"` into `cfg/console.cfg`, and fires
the patch alias `execonsole` (`alias execonsole "exec console.cfg"`, `user.cfg`) to execute it.
It then repeats the scan for `feed`, rebinding that key to `vm_feed`.

So the patch re-routes discipline and feed onto its own aliases **following the player's own
choice of key**, rather than assuming the default. `vm_feed` is why one key is two verbs:
`checkFeed()` calls `prayerStart()` for clans 9–11 and `feedStart` otherwise.

The same mechanism is available to any mod — generate console text from Python, exec it — and is
documented as the way to read keys into Python directly (`host_writeconfig backup.cfg`,
`unbindall`, `bind "1" "OnInput('1')"`, later `exec backup.cfg`). **`bind` is therefore a verb the
running game executes, not merely a line a config file carries.**

## Default bindings

Retail is `cfg/default.cfg` from `pack000.vpk`; Patch is the Unofficial Patch 11.5
`Unofficial_Patch/cfg/default.cfg`. `—` means unbound.

### Movement

| Key | Retail | Patch |
|---|---|---|
| `w` / `s` | `+forward` / `+back` | same |
| `a` / `d` | `+moveleft` / `+moveright` | same |
| `UPARROW` / `DOWNARROW` | `+forward` / `+back` | same |
| `LEFTARROW` / `RIGHTARROW` | `+left` / `+right` (turn) | `+moveleft` / `+moveright` (strafe) |
| `,` / `.` | `+moveleft` / `+moveright` (strafe) | `+left` / `+right` (turn) |
| `'` / `/` | `+moveup` / `+movedown` | same |
| `b` / `v` | `+moveup` / `+movedown` | same |
| `SPACE` | `+jump` | same |
| `CTRL` | `+duck` | same |
| `SHIFT` | `+speed` (walk) | same |
| `END` | — | `+strafe` |
| `INS` / `DEL` | — | `autospeed` / `automove` (patch aliases: toggle walk/run, toggle auto-move) |
| `u` / `j` | — | `+lookup` / `+lookdown` |
| `;` | `+mlook` | same |

The patch **swaps the arrow keys and the `,`/`.` pair**: arrows strafe, comma/period turn.

### Combat, items, disciplines

| Key | Retail | Patch |
|---|---|---|
| `MOUSE1` / `ENTER` | `+attack` | same |
| `MOUSE2` | `vdiscipline_last` | `vm_discipline` → `checkDiscipline()` |
| `TAB` | `+wpn_secondaryatk` | same |
| `r` | `+reload` | same |
| `e` | `+use` | same |
| `f` | `+feed` | `vm_feed` → `checkFeed()` |
| `g` | — | `vm_passives` (casts the seven passive disciplines in one alias) |
| `h` | `holster` | same |
| `BACKSPACE` | `dropitem` | same |
| `i` | `slot6` | same |
| `[` / `]` | `invnext` / `invprev` | same |
| `\` | `lastinv` | same |
| `MWHEELUP` / `MWHEELDOWN` | `invprev` / `invnext` | same |
| `t` | — | `toggleuiside` |
| `1`–`0` | `vhotkey #1`–`#10` | same |
| `k` | `showhotkeys` | same |
| `F1` / `F2` / `F3` | `slot2` / `slot3` / `slot5` | same |
| `F4` / `F5` / `F6` | — | `slot6` / `slot4` / `slot1` |
| `F8` | `vdiscipline_endall` | same |

### UI, camera, system

| Key | Retail | Patch |
|---|---|---|
| `ESCAPE` | `cancelselect` | same |
| `` ` `` | `toggleconsole` | same |
| `c` | `+chareditor` | same |
| `l` | `+questlog` | same |
| `HOME` / `PGUP` / `PGDN` | `dlghist` / `dlgscrlup` / `dlgscrldn` | same |
| `z` | `togglecamera` | same |
| `F9` / `F12` | `save quick` / `load quick` | same |
| `F10` | `snapshot` | same |
| `p` | `vphysicshand` (dead) | `skip` → `skipScene()` |
| `PAUSE` | — | `pause` |
| `KP_5` | — | `cam_restore` (alias: `cam_yaw 0; cam_idealdist 85; cam_targetangle 15`) |
| `KP_UPARROW` / `KP_DOWNARROW` | — | `+camin` / `+camout` |
| `KP_LEFTARROW` / `KP_RIGHTARROW` | — | `cam_rotateleft` / `cam_rotateright` (`incrementvar cam_yaw -180 180 ∓15`) |

### `kb_def.lst` disagrees with `default.cfg`

`scripts/kb_def.lst` is the *options UI's* notion of the defaults (what the **Use Defaults**
button restores); `cfg/default.cfg` is the *engine's*. They are not generated from each other
and they diverge, in both retail and the patch:

- `[` / `]` — `kb_def.lst` says `invprev` / `invnext`; `default.cfg` says `invnext` / `invprev`,
  in both retail and the patch. (`MWHEELUP`/`MWHEELDOWN` agree across the two files.)
- Retail `kb_def.lst` binds `,` twice — `+moveleft`, then `pause`. Last wins, so restoring
  defaults from the UI leaves `,` on `pause`.
- Patch `kb_def.lst` binds `p` twice — `vphysicshand`, then `skip`. Last wins → `skip`.
- `kb_def.lst` lists neither `ESCAPE`/`cancelselect` nor `` ` ``/`toggleconsole`; those exist
  only in `default.cfg`.

So "Use Defaults" in the options screen does not reproduce a fresh `config.cfg`.

## The control options UI

`GameUI.dll` builds Options as a tabbed dialog (`COptionsDialog`) over eight pages, each with a
`.res` layout under `resource/`:

| Page class | `.res` | Tab label (retail → patch) |
|---|---|---|
| `COptionsSubKeyboard` | `OptionsSubKeyboard.res` | `#GameUI_Keyboard` — "Keyboard" → **"Controls"** |
| `COptionsSubMouse` | `OptionsSubMouse.res` | "Mouse" |
| `COptionsSubGameplay` | `OptionsSubGameplay.res` | "Gameplay" |
| `COptionsSubVideo` | `OptionsSubVideo.res` | "Video" |
| `COptionsSubVisual` | `OptionsSubVisual.res` | "Visual" |
| `COptionsSubAudio` | `OptionsSubAudio.res` | "Audio" |
| `COptionsSubVoice` | `OptionsSubVoice.res` | "Voice" |
| `COptionsSubAdvanced` | `OptionsSubAdvanced.res` | "Advanced" |

### Controls page (`COptionsSubKeyboard`)

Constructor `0x100196a0` (`GameUI.dll`, imagebase `0x10000000`). Layout:
`listpanel_keybindlist` (a `ListPanel`, 480×258) plus three buttons — **Use Defaults**
(`Defaults`), **Set New Key** (`ChangeKey`), **Clear Key** (`ClearKey`).

The list has **three columns**, added at `0x10019a20`:

| Column key | Header | Width |
|---|---|---|
| `Action` | `#GameUI_Action` — "Action" | 245 |
| (blank key) | `#GameUI_KeyButton` — "Key/Button" | 100 |
| `AltKey` | `#GameUI_Alternate` — "Alternate" | 100 |

So **every action supports two bindings**, a primary and an alternate.

Flow:

1. `0x10019c50` reads `scripts/kb_act.lst` (via `%skb_act.lst` with the `scripts/` prefix) and
   builds one row per line: field 1 is the console command (stored as the row's `Binding`),
   field 2 the human label. A row whose label is the literal `blank` becomes a **header** row
   (`Header` = 1, `HeaderString`) — a feature **neither** shipped `kb_act.lst` uses. The patch's
   grouping is done the crude way instead, with `" " " "` rows that render as empty separators.
2. `0x10019eb0` finds a row by its `Binding` string (case-insensitive) — the lookup used when
   painting current binds and when applying defaults.
3. `0x1001a520` reads `scripts/kb_def.lst` for **Use Defaults** and re-applies each
   key→command pair to the matching row.
4. Applying changes emits plain console text: `unbind "%s"`, `bind "%s" "%s"`, and `unbindall`
   for a full reset. The confirmation prompt is `#GameUI_ConfirmDefaultBindings` — *"Are you
   sure you want to reset all values to default?"*

`engine.dll` reads `scripts/kb_def.lst` independently as well (`"Couldn't open kb_def.lst"`),
for its own default-binding path.

### `kb_act.lst` — the bindable-action list

Two columns, `"<console command>" "<label>"`. Retail lists **39** actions. The patch rewrites the
file to **68 rows — 64 actions plus 4 `" " " "` spacers** — relabelling several (`+duck` "Duck" → "Crouch", `+wpn_secondaryatk`
"Toggle attack mode" → "Secondary Mode" despite the held composite recovered above, `togglecamera` "Toggle 3rd person camera" → "Toggle
View") and adding the ten `vhotkey #N` slots, `slot1`/`slot4`, the camera verbs
(`+camin`/`+camout`/`cam_rotateleft`/`cam_rotateright`/`cam_restore`), `autospeed`, `automove`,
`+lookup`/`+lookdown`, `vm_passives`, `toggleuiside`, `pause` and `skip`. That is the patch's *"Added console key and all hotkeys to the controls options
submenu"* (readme line 663) and *"Made auto-move and walk/run toggles be definable"* (line 988).

An action is bindable through the UI **only if it is listed in `kb_act.lst`** — the file is the
whitelist. Commands bound in `default.cfg` but absent from it (`toggleconsole`, `cancelselect`,
`vphysicshand`) still work, they are just not editable in-game.

### Mouse page (`COptionsSubMouse`)

Three checkboxes and one slider:

| Control | cvar | Label (retail → patch) |
|---|---|---|
| `ReverseMouse` | `m_pitch` sign | "Reverse mouse" → **"Invert mouse"** |
| `MouseLook` | `in_mlook` | "Mouse look" |
| `MouseFilter` | `m_filter` | "Mouse filter" |
| `Slider` (`CCvarSlider`) | `sensitivity` | "Mouse sensitivity" |

Defaults from `default.cfg`: `sensitivity 3`, `m_pitch 0.022`, `m_yaw 0.022`, `m_filter 0`,
`m_side 0.8`, `m_forward 1`, `lookspring 0`, `lookstrafe 0`. Degrees per mouse count is
`m_pitch × sensitivity` = 0.066 (see `docs/vtmb/source_movement.md`). The patch only moves the widgets
(30 px row pitch, taller slider) — no cvar change.

### Gameplay page (`COptionsSubGameplay`)

| Control | cvar | Label |
|---|---|---|
| `AutoDisc` | `vdiscipline_allow_renewables` | "Auto Renew Disciplines" |
| `DamageFloaters` | `damage_floaters` | "Show Damage Floaters" |

The patch widens both rows to 420 px and adds a static `Patch` label reading
`#GameUI_Patch` = "Unofficial Patch 11.5".

### Advanced page — auto-aim

`OptionsSubAdvanced.res` carries the `Auto-Aim` checkbox (`#GameUI_AutoAim`) over `sv_aim`
(default `0` in both configs) plus the content-lock button.

## Joystick

`client.dll` registers the full Quake-lineage joystick set — `joystick` (master enable, default
`0`), `joyname`, `joyadvanced`, `joyadvaxisx/y/z/r/u/v`, `joyforwardthreshold` (0.15),
`joysidethreshold`, `joypitchthreshold`, `joyyawthreshold`, `joyforwardsensitivity`,
`joysidesensitivity`, `joypitchsensitivity`, `joyyawsensitivity`, `joywwhack1`, `joywwhack2`,
`joyadvancedupdate`, and the `+jlook`/`-jlook` look mode. Devices are enumerated through the
Win32 `joyGetDevCapsA`/`joyGetNumDevs`/`joyGetPosEx` MM API (`"joystick found"` /
`"joystick not found -- invalid joystick capabilities (%x)"`); `-nojoy` on the command line
disables the subsystem, `-nomouse` the mouse (`cl_mouseenable`).

There is **no controller UI** — no options page, no `.res`, and `JOY1`–`JOY4` / `AUX1`–`AUX32`
are absent from `kb_def.lst` and `kb_act.lst` in both retail and the patch. `valve.rc` execs a
`joystick.cfg` that the game does not ship. Gamepad support in VtMB is the raw cvar layer and
nothing else.

## Notes for Elysium-Unreal

Facts that constrain the rebuild, not decisions. The Unreal design they feed is
**`docs/architecture/input-architecture.md`** (build task: roadmap **10.6**; the remapping screen is **8.10** on
**8.6**'s stack):

- **`kb_act.lst`'s whitelist**, in the patch's richer, better-labelled form, is the natural
  source for a remapping screen's action list — already grouped, labelled, and the players'
  mental model.
- **Two bindings per action** (primary + Alternate) is the original's contract, and both the
  keyboard defaults and the mouse-wheel binds rely on it.
- `+`/`-` button pairs map cleanly onto Unreal's pressed/released events; the one-key-owns-the-
  press rule matters for overlapping keys.
- The `,`/`.` ↔ arrow-key swap, the ten `vhotkey` slots, and the numpad camera verbs are
  **patch** behaviour, not retail. The **patch set is the one that ships** — with `vphysicshand`
  dropped and the `kb_def.lst`/`default.cfg` disagreements resolved to `default.cfg`.
- `config.cfg` round-tripping (`unbindall` + `bind` lines + archived cvars) is what the embedded
  CPython VM's `FixKeyBindings` reads — the VM's `nt.getcwd`/`sys.moddir` redirect to
  `$ELYSIUM_EXPORT_ROOT/` exists so that read resolves (roadmap 9.3b, `docs/vtmb/python_bridge.md`).
  The file is a projection of the Enhanced Input key profile rather than the settings model, but
  **the projection cannot be write-only**: `FixKeyBindings` reads it and then issues `bind`, so a
  runtime `bind` has to reach the key profile or the patch's own re-routing silently does nothing
  (`docs/architecture/input-architecture.md` § "Remapping and persistence").
- Auto-aim (`sv_aim`) defaults to **off**.
- Gamepad support has no original to reproduce (see "Joystick" above). Anything here is new
  work under the Presentation/Feel axes rather than a port; the device layer is the engine's
  `GameInputWindows` plugin with authored PlayStation device configs (`docs/architecture/input-architecture.md` §
  Gamepad).
