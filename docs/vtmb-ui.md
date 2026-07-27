# VtMB's UI — what the original actually is

Engine-neutral facts about VtMB's user interface: which code owns which screen, what data
drives it, where the art lives, and the coordinate model it is authored in. This is the
**design intent** the modern re-skin is checked against (`remaster-direction.md` axis 1 — the
UI has no classic mode), not a port target.

Its Unreal counterpart is **`ui-architecture.md`** (the CommonUI/Slate stack, the design tokens,
the screen inventory as rebuilt). The pairing works like `controls.md` ↔ `input-architecture.md`:
a new fact about how *VtMB* draws a screen goes here; a decision about how *Elysium* draws it
goes there. Per-task status lives in `roadmap.md` (**PL8**, **8.6**, 8.8, 8.9, 8.10).

`m0_menu_build.md` is the `GameUI.dll` decompile reference and is **partly superseded by this
doc** — see "Corrections" at the end.

Addresses are `client.dll` unless stated, imagebase `0x10000000`. Recovery method and the
headless workspace: `tools/ghidra/README.md`.

---

## 1. There are two UI stacks, and only one of them ships

| Stack | Owns | Scheme |
|---|---|---|
| **`GameUI.dll`** — stock Source VGUI2 (`CBasePanel`, `CGameMenu`, `CGameMenuButton`) | the `.res`-driven dialogs: options pages, load/save, confirm/notify, text entry | `Resource/TrackerScheme.res` |
| **`client.dll`** — VtMB's own C++ UI (`CVMainMenu`, `CHud*`, `CharEditPanel`, `CSignUI`, …) | the main + pause menu, the whole in-game HUD, the character sheet, quest log, barter, maps, sign panels | `Resource/VampireScheme.res` |

The menu the player sees is **client.dll's**. `GameUI.dll`'s `CGameMenu` is Source boilerplate
that VtMB links but does not present — which matters because its geometry (west alignment, a
`(6,0)` text inset, `Vamp_MainFont@44`) has been mistaken for the shipped menu's.

**Both schemes are loaded.** They are not alternatives:

- **`VampireScheme.res`** — loaded by `CVMainMenu`'s ctor (`FUN_10065700`). Its `Fonts` block is
  **empty**; it supplies `Colors`, `BaseSettings` and `Borders`. This is where the gold `V*`
  palette lives, so it skins every client.dll surface.
- **`TrackerScheme.res`** — the VGUI2 scheme, with the full tiered `Fonts` alias table. Skins
  GameUI.dll's dialogs. A `trackerscheme-uhd.res` ships loose alongside it.
- `vampirece2scheme.res` ships and is not loaded.

### The palette that matters (`VampireScheme.res`, verbatim RGBA)

| Key | RGBA | Role |
|---|---|---|
| `VUnselectedText` | `171 140 95` | the gold — idle labels, sheet rows, HUD chrome |
| `VDesHeaderText` | `255 240 191` | bright gold — section headers |
| `VSelectedText` | `255 255 255` | selected row / title |
| `BrightControlText` | `109 207 246` | **cyan** — the active tab |
| `BaseText` / `ControlText` | `216 222 211` | body copy (bone) |
| `VWarningText` | `255 0 0` | warnings |
| `VDisabledText` | `128 128 128` | disabled |
| `BorderBright` / `BorderDark` | `136 145 128` / `45 49 40` | control bevels |
| `ControlBG` / `WindowBG` / `SelectionBG` | `0 0 0 0` | **all four background colours are fully transparent** — the UI floats on the scene, it does not sit on panels |

Blood red is *not* in the scheme. The menu's red is hardcoded (§2).

---

## 2. The main menu — `CVMainMenu`

### Build path

`CVMainMenu` ctor `FUN_10065700` loads the scheme, then `FUN_10067540` and `FUN_10067030` (the
particle scene). `FUN_10065f40` builds the item list: for each entry in the active item set it
`operator_new(0xf4)`s a **`CVMenuButton`** (244 bytes), sets its font from the scheme alias
`MainMenu`, and pushes it into the button array at `+0x170` (count `+0x17c`).

### Item vocabulary

Twelve `BTN_*` tokens exist. A label is resolved by formatting **`VMainMenu_<token>`**
(`FUN_10065eb0`) and looking it up in the localized string table; on a miss it falls back to a
hardcoded English table. This is **not** the `#GameUI_*` namespace that skins GameUI.dll.

```
BTN_NEWGAME  BTN_LOADGAME  BTN_SAVEGAME  BTN_RELOAD   BTN_CONTINUE  BTN_MAINMENU
BTN_OPTIONS  BTN_QUIT      BTN_MULTIPLAYER  BTN_VIEWINTRO  BTN_TUTORIAL  BTN_MANUAL
```

Observed sets: **main menu** = New Game / Load Game / Options / Quit; **pause** = Continue /
Reload / Load Game / Save Game / Options / Main Menu. Multiplayer ships and is suppressed.

### Colour

Item colour is the literal **`0xc00000a8`** written in `FUN_10065f40` — packed `(r,g,b,a)`
little-endian = **RGBA(168, 0, 0, 192)**. Hardcoded in code, read from neither scheme. Hover is
an instant colour swap; there is no fade or slide anywhere in the menu path.

### The layout law — a 1024×768 virtual canvas

`CVMainMenu::PerformLayout` (`FUN_100660e0`). The two scale factors are `1/1024` and `1/768`
(`_DAT_1022b97c`, `_DAT_10229d30`); the two padding constants are `20.0` and `4.0`
(`0x10224ae8`, `0x10225528`), all read out of `.rdata`:

```
sx = screenW / 1024
sy = screenH / 768

btnW  = maxLabelWidth  + round(20 · sx)      // widest label sets the column width
btnH  = maxLabelHeight + round( 4 · sy)
pitch = btnH + round(2 · sy)

x   = (screenW − btnW) / 2                   // the column is CENTRED
y_i = menuY + 2 + i · pitch
```

Every button is sized to the *widest* label, so the column is one uniform block. This is the
**same virtual canvas `CSignUI` uses** — signs, menu and HUD share one
authored coordinate model, and it is 1024×768, not VGUI's 640×480.

### The animated backdrop

`FUN_10067030` reads `resource/MainMenuParticles.txt`: `camera_rotation`, `camera_fov` (default
45), `camera_near` (2), `camera_far` (4096), `default_skybox` (default `holylight`; the file
ships `MM_Skybox`), `music` (default `music/Vampire_Theme_Mono.wav`; the file names
`Vampire_Theme.mp3`, a loose install file outside the VPKs). It then walks `Particle` blocks,
each naming an `emitter`, an `origin` and an `angle`.

Beyond the scripted emitters the menu spawns two hardcoded ones — **`MM_cursor_emitter`** and
**`MM_menu_emitter`** — which no prior doc lists.

The emitter graph (`particles/m_*.txt`, 26 scripts) draws 22 sprites, which are loose **`.tga`**
files under `particles/` (not `.tth/.ttz`): `fire-sprite`, `starpresence`, `cloud`, `bloodcel`,
`bloodcel2`, `bloodlines2`, `vtm_glow2`, and 15 `mm_<clan>` symbols. Every menu particle is
`mask 0` (additive). Structure detail: `m0_menu_build.md` §9.

---

## 3. The HUD — C++ classes, no layout file

There is **no `.res` for the HUD**. Every element is a `client.dll` class drawing materials from
`materials/hud/**` at positions compiled in. The classes, recovered from RTTI:

| Class | Element |
|---|---|
| `CHudManager`, `CHudElement`, `CManagedHudElement`, `CHudManagerPanel` | the element registry and draw pass |
| `CBloodBar`, `CFrenzyBar`, `CFeedBar` | the blood / frenzy / feeding meters |
| `CHealthAnkh`, `CBossHealthBar` | player health, boss health |
| `CStealth`, `CProgBar` | the stealth gauge, generic progress bars (lockpick, targeted attack) |
| `CAmmoCounter`, `CHudWeaponSelection` | ammo readout, weapon select |
| `CDiscipline`, `CTargeted` | discipline icons, the targeting cursor |
| `CMoneyBar`, `CItemInfo` | money, item tooltips |
| `CHudInfoBar`, `CHudAreaIcon`, `CHudUseIcon` | the info-bar messages, area label, `+use` context icon |
| `CHudDialog`, `CCenterPrint`, `CMessageChars`, `CHudTextMessage` | dialogue box, centre print, message crawl |
| `CLoadingDisc`, `CPhysicsHand`, `CCredits`, `CConsole` | loading spinner, physics-hand cursor, credits, console |

Related screens, same stack: `CharEditPanel` (+ `CharEditCharPanel` / `EquipPanel` / `InfoPanel`
/ `StatsPanel`), `QuestLogPanel`, `VBarterUI`, `VItemInfoUI`, `VMapScreenUI`, `VHotkeysUI`,
`VCharWizardUI`, `CSignUI`.

### HUD art trees

| Tree | Count | Contents |
|---|---|---|
| `materials/hud` | 73 | the retail chrome: `bloodbar_*`, `healthgraphic*`, `lightgauge_0..4`, `frenzybar_*`, `feedbar`, `sneak_icon_*`, `lockcursor_*`, `ammodis_frame_*`, targeting cursors |
| `materials/hud/new_ui` | 62 | the shipped in-game bar set: `bloodbarframe`, `healthbarframe`, `stealthframe`, `firstbar…fourthbar` frames + lace + colour fills |
| `materials/hud/disciplines` | 92 | per-discipline icons |
| `materials/hud/context_icons` | 83 | the 72-entry `use_icon` enum — owned by `UE_use_icons.py`, packed to one atlas (PL3) |
| `materials/hud/infobar_icons` | 27 | info-bar glyphs |
| `materials/hud/crosshairs` | 19 | per-weapon reticles |
| `materials/hud/signs` | 69 | in-world sign art |
| `materials/hud/inventory_images` | ~350 | item / armour / weapon icons |
| `materials/interface/charactermaintenance` | 130 | sheet chrome: clan symbols, bubbles, buttons, dividers |
| `materials/interface/{pop_ups,worldmap,sewermap,tipinfoscreen,widescreen,mainmenu}` | 90 / 77 / 66 / 15 / 39 / 3 | the remaining screens; `mainmenu` holds `vtm_title` (1024×512) |

**`new_ui` is retail, not the Unofficial Patch.** Most of the tree lives in the VPKs; the patch
overrides only the blood- and faith-bar art loose (`blood_bar*`, `bloodbar*`, `faith*`,
`blooduseglow*`). So "the patch HUD" is retail structure with patched art — the same patch-first
resolution every other decoder in this repo already uses, not a separate layout.

---

## 4. Type — what the original sets, and what it means

VtMB ships **28 bitmap `.fnt` families** (`materials/fonts/<face>_<size>_<weight>_<flags>.fnt`
plus `-pageN` atlases; glyphs live in the atlas alpha). Decoder: `tools/fnt.py`. Face inventory
and metrics: `m0_menu_build.md` §3.

The load-bearing fact for the re-skin is not the atlases but what they are drawn as: **every
label in the game is small-caps with wide tracking**, and body copy drops to a plain sans. That
is an authored art decision, not a hardware constraint, so it survives into the remaster even
though the atlases do not. The 640×480-tier font aliasing does not
survive — vector type scales continuously.

---

## 5. Extracted form — `out/ui/`

`tools/UE_extract_ui.py` (**PL8**) mirrors the above, patch-first, whole-game. The `.fnt`
atlases are deliberately **not** extracted; vector faces live in `Content/Fonts`
(`tools/fetch_ui_fonts.py`).

```
out/ui/
  resource/<name>.res      25 layouts + both schemes, byte-for-byte
  strings.json             194 localized tokens (UCS-2 source → UTF-8)
  menu/title.png           the title lockup, 1024×512
  menu/particles/*.txt     the scene + 25 emitter/particle scripts it references
  menu/sprites/*.png       the 22 sprites the graph draws
  menu/skybox/*.png        the six MM_Skybox faces
  art/<tree>/<name>.png    503 decoded HUD + interface materials
  manifest.json            per-file pixel size + anything unresolved
```

---

## 6. Corrections to `m0_menu_build.md`

That doc is the `GameUI.dll` decompile reference. Four of its claims describe
the menu that does not ship; they are superseded here:

| `m0_menu_build.md` | Corrected |
|---|---|
| §2 "`vampirescheme.res` … **not** loaded — `trackerscheme.res` is" | both load; **VampireScheme skins client.dll**, TrackerScheme skins GameUI.dll |
| §5 labels come from `gameui_english.txt` `#GameUI_*` | client.dll's menu resolves **`VMainMenu_BTN_*`** with a hardcoded English fallback |
| §7 items are west-aligned, inset `(6,0)`, auto-sized | that is `CGameMenuButton` (unused). `CVMainMenu` **centres** and sizes every button to the widest label |
| §7 "no hardcoded x/y/w/h … anchor matched visually" | the anchor is not empirical: **`sx=W/1024`, `sy=H/768`, pad `20×4`, gutter `2`** are constants in `.rdata` |

Its still-valid content: the `.fnt` format and face inventory (§3), the TrackerScheme colour and
alias tables (§4), the dialog `.res` inventory (§6), the particle-scene structure (§9).

## 7. Open

- The `CVMainMenu` member at `+0x308`/`+0x310` supplies the column's top `y`; the constant that
  seeds it has not been traced, so vertical anchoring is still matched by eye.
- `CVMenuButton`'s internal text alignment (within its uniformly-sized box) is unrecovered — the
  column is centred, but whether the glyphs are centred or left-set inside each button is not.
- The HUD element positions are compiled in and have not been decompiled; only the class
  inventory and the art trees are recovered.
