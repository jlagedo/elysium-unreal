# M0 — Full-Fidelity Menu Rebuild

> **Partly superseded — read `vtmb-ui.md` first.** This doc decompiles `GameUI.dll`'s
> `CBasePanel`/`CGameMenu`, which VtMB links but **does not present**. The menu the player sees
> is `client.dll`'s own `CVMainMenu`. Four claims below are therefore wrong about the shipped
> menu — the loaded scheme (§2), the label namespace (§5), item alignment and the layout
> constants (§7). `vtmb-ui.md` §6 tabulates each correction. Still valid here: the `.fnt` format and face inventory (§3), the TrackerScheme colour
> and alias tables (§4), the dialog `.res` inventory (§6), and the particle-scene structure (§9).

> **Reference, not a port target. Elysium-Unreal does not port VGUI.** The direction is
> *remaster* (`docs/remaster-direction.md` axis 1): VtMB's screen **structure** — inventory,
> panel anatomy, reading order, palette, iconography, strings — is kept and re-skinned on a
> modern resolution-independent Slate/UMG stack with vector type (roadmap **8.6**). There is
> no classic UI mode, no 640×480 scale box, and no runtime `.fnt` bitmap atlas.
>
> What this doc is **for**, then: the authoritative record of *what the original UI contains and
> why* — the `GameUI.dll` decompile findings (§7), the source-data inventory (§2), the scheme
> and `.res` semantics, the font roles and their metrics, the particle background. That is the
> design intent every re-skinned screen is checked against, and the spec `menu_extract.py`
> (PL8) extracts to.

**Goal:** rebuild the VtMB main menu + in-game pause menu as a faithful port of the
original Valve **VGUI2** UI — real fonts, real scheme, real `.res` layouts, real 3D
particle scene — not a hand-composited look-alike. Actions may be stubbed
(settings/load/save unimplemented); the target is that **look, structure, and
behavior** are exact. Current state against that target is the ledger in §0.

This document is self-contained: it captures the full source exploration + the
`GameUI.dll` decompile results, and specifies what to build. Companion: the overall
plan is `docs/rebuild-strategy.md` (this is the drill-down for its M0 milestone).
Decompile tooling + dumps live under `tools/ghidra/` (`README.md`).

**Bring-your-own:** every byte below comes from the user's own install. The pipeline
(`tools/menu_extract.py`) extracts to `game/content/ui/` at build time; nothing
game-sourced is committed. The repo keeps Elysium branding.

**Locked decisions (2026-07-16):** runtime `.fnt` fonts · a general `.res`→`Control`
loader rendering every dialog (inert where unbacked) · geometry informed by a
`GameUI.dll` decompile · VtMB's VGUI logical coordinate space (proportional).

---

## 2. Source data inventory (all in the VPKs unless noted)

| Purpose | Path |
|---|---|
| **Scheme** (the loaded skin) | `resource/trackerscheme.res` |
| Menu item list + commands | `resource/gamemenu.res` |
| Localized strings (`#GameUI_*`, UCS-2) | `resource/gameui_english.txt` |
| Dialog layouts | `resource/{newgamedialog,loadgamedialog,savegamedialog,dialogoptionsingame,optionssub*,confirmdialog,notifydialog,textentrydialog,contentcontroldialog}.res` |
| Fonts (bitmap) | `materials/fonts/<face>_<size>_<weight>_<flags>.fnt` + `-pageN` (`.tth/.ttz`) |
| Title art | `materials/interface/mainmenu/vtm_title.*`, `particles/bloodlines.tga` |
| Particle scene | `resource/mainmenuparticles.txt`, `particles/m_*.txt` |
| Particle sprites | `particles/{fire-sprite,starpresence,cloud,bloodcel,bloodcel2,mm_<clan>}.*` |
| 3D-skybox backdrop | `materials/skybox/mm_skybox{ft,bk,up,dn,lf,rt}.tth/.ttz` |
| Theme music (**loose file**, not in VPKs) | `<install>/Vampire/sound/music/vampire_theme.mp3` |

Not the scheme: `vampirescheme.res` (empty `Fonts`) and `vampirece2scheme.res` (binds
TTF names Percolator/Dominican) are **not** loaded — `trackerscheme.res` is.

---

## 3. Fonts — runtime bitmap `.fnt`

VtMB ships **Source bitmap-font atlases**, not TTFs. `<face>_<size>_<weight>_<flags>.fnt`
+ `-pageN` atlases; glyphs live in the atlas **alpha** channel. Decoder exists:
`tools/fnt.py` — header `u32[9]`; `@36` 256-byte char→glyph map; `@292` glyphCount ×
44-byte entries (advance `@0`, leftBearing `@18`, −height `@20`, page `@24`, width
`@26`, 4×f32 UV `@28`).

Face inventory (present as `.fnt`): `vamp_mainfont` (menu + title labels; sizes
14,16,20,21,26,27,32,35,40,43,44,48,54,55,71,88,111), `vamp_plain`, `vamp_small`,
`vamp_dialog_base` (+ discipline variants), `vamp_ammo`, `vamp_lcd`,
`vamp_handwriting1`, `tahoma`, `times_new_roman`, `trebuchet_ms`, `troika_games`
(studio logo), `marlett` (symbols). **Menu label font = `Vamp_MainFont` weight 1000.**

---

## 4. Scheme — `trackerscheme.res`

Resolution-tiered (base + `_640` families, 640×480 logical). Parse `Colors`,
`BaseSettings`, `Fonts` (aliases), `Borders`.

**Colors (verbatim, RGBA 0–255):**

| Key | RGBA | Use |
|---|---|---|
| `BloodNormal` | `168 9 9 255` | menu label **idle** |
| `BloodBright` | `192 15 15 255` | menu label **armed / hover** |
| `BloodDim` | `128 4 4 128` | menu label **disabled** |
| `DialogBackground` | `57 37 27 255` | load/save/etc dialog fill |
| `Dlg_FillBkg` | `0 0 0 175` | dark fade behind dialogs |
| `ControlBG` / `ControlDarkBG` | `57 47 37` / `40 30 21` | control / scrollbar bg |
| `BaseText` / `BrightBaseText` / `DimBaseText` | `216 222 211` / `255 255 255` / `150 159 142` | body text |
| `BorderBright` / `BorderDark` | `145 128 128` / `40 30 21` | control bevels |

**Font aliases (subset):** `MainMenu_640` → Vamp_MainFont 44 · `PercolatorMedium/Small`
→ Vamp_MainFont 35/26 · `Copperplate` → Vamp_Small 18 · `Dominican` → Vamp_Plain 22
(Vamp_Dialog_Base 16 @640) · `Dlg_Base` + discipline fonts → Vamp_Dialog_Base ·
`Default*` → Tahoma · `Marlett` → symbol font. Borders are multi-inset color rules
(`BaseBorder`, `InfoWinBorder`, `ButtonDepressedBorder2`, `TabActiveBorder`, …).

---

## 5. Menu definition + strings

`resource/gamemenu.res` (item → command):

| Item | Command |
|---|---|
| New Game | `OpenNewGameDialog` |
| Load Game | `OpenLoadGameDialog` |
| Save Game (name `SaveGame`) | `OpenSaveGameDialog` |
| Multiplayer (SubMenu) | Find Servers / Customize / Create Server |
| Options | `OpenOptionsDialog` |
| Quit | `Quit` |

Multiplayer is present in the `.res` but suppressed by the shipped game (§8).

Labels from `gameui_english.txt` (UCS-2): "New Game", "Load Game", "Save Game",
"Multiplayer", "Options", "Quit" (leading `&` = keyboard mnemonic, stripped at render).

**Pause menu** = the same menu; the decompile shows the only difference is `SaveGame`
being enabled in-game (§8). The pause item set observed in-game is Continue / Reload /
Load Game / Save Game / Options / Main Menu.

---

## 6. Dialogs — VGUI `.res` loader (640 space)

Each screen is a KeyValues `.res` control tree: `ControlName` (Frame, Label, Button,
RadioButton, CheckButton, Slider, ComboBox, ListPanel/PropertySheet, BuildModeDialog)
with `xpos/ypos/wide/tall`, `labelText` (→ `#GameUI_*`), `textAlignment`, `command`,
`default`, `tabPosition`.

**Coordinate model:** logical VGUI space with per-panel proportional scaling as
authored. Font base is ~640; several dialogs use larger absolute panels
(e.g. `newgamedialog` 372×260 @ (390,270); `dialogoptionsingame` 824×736). Each panel
carries its own `proportional` flag + coordinates.

Dialog inventory: `newgamedialog` (difficulty: Training / Easy / Medium / Hard + Play /
Cancel), `loadgamedialog`, `savegamedialog`, `dialogoptionsingame` +
`optionssub{video,audio,mouse,keyboard,gameplay,voice,visual,advanced}`,
`confirmdialog`, `notifydialog`, `textentrydialog`, `contentcontroldialog`.

---

## 7. Decompile findings — `GameUI.dll` (resolved)

Ghidra 12.1.2; MSVC RTTI recovered the C++ class names. Imagebase `0x10000000`.
Tooling + dumps under `tools/ghidra/` (local-only; the whole tree is gitignored, never committed).

**Build path.** `CBasePanel::CreateGameMenu` (`FUN_10003ef0`) loads
`Resource/GameMenu.res` via KeyValues → recursive loader (`FUN_10004150`; reads
`label`/`command`/`name`/`SubMenu`) → `CGameMenu::AddMenuItem`; hardcoded fallback if
the `.res` fails. Commands dispatch through `RunMenuCommand` (`FUN_10004250`).
`CBasePanel` is a **singleton** (`DAT_1006c184`); menu ptr at `+0xfc`.

**Containers.** `CGameMenu` (custom `vgui2::Menu` subclass, 0x90 bytes, vtable
`0x10058ddc`, ctor `FUN_10033910`): `CUtlVector` of items (count `+0x64`, array
`+0x6c`), armed index `+0x80`, embedded `MenuScrollBar` `+0x70`, min-width `+0x60=99`.
`AddMenuItem` (slot 121, `FUN_10033d90`) `new`s a **`CGameMenuButton`** (0x104=260
bytes, vtable `0x1005a7ec`, ctor `FUN_1003df70`→`FUN_1003e010`).

**Item style (pinned).** `CGameMenuButton` init sets content alignment **3 = west/left**,
text inset **(6, 0)** (= scheme `Menu."TextInset" "6"`), font `Vamp_MainFont @44`,
colors the scheme `Blood*`. Items auto-size to their label.

**Layout & animation (resolved — the key result).**
- **No slide/fade animation.** `CBasePanel` overrides only 3 vtable slots: [20] dtor,
  [78] `RunMenuCommand`, [101] `OnThink`. It does **not** override `PerformLayout`.
- `CBasePanel::OnThink` (`FUN_100040f0`) does one thing: `menu.FindChildByName("SaveGame")
  .SetEnabled(engine->IsInGame)` — the entire main-menu-vs-pause difference.
- **No hardcoded x/y/w/h** in any menu class: stock vgui2 vertical auto-layout, spacing
  from font metrics. The on-screen anchor is **matched visually** (no constant exists).
- `+0x100..+0x11c` (init `-1`) are engine **interface pointers**, not anim timers.
- Motion = the **particle scene** + **instant** idle→armed hover-color swap (no
  `AnimationController` fade in the menu path).

**Address map:** `10003ef0` CreateGameMenu · `10004150` KV loader · `10004250`
RunMenuCommand · `100040f0` OnThink · `10033910` CGameMenu ctor · `10033d90`
AddMenuItem · `1003df70`/`1003e010` CGameMenuButton ctor+init · vtables `10058ddc`
CGameMenu / `1005a7ec` CGameMenuButton / `1004ff3c` CBasePanel.

**Net:** a static, west-aligned `Vamp_MainFont@44` list in Blood colors with instant
hover, over the live particle scene — no animation to reproduce, anchor matched to
reference.

---

## 8. In-game vs. main menu

Same `CGameMenu`; `CBasePanel::OnThink` gates `SaveGame` by in-game state. Multiplayer
is present in `gamemenu.res` but suppressed by the shipped game. The game's own menus
show: main menu = New Game / Load Game / Save Game(disabled) / Options / Quit;
pause = Continue / Reload / Load Game / Save Game / Options / Main Menu.

---

## 9. Animated background — the particle scene

`resource/mainmenuparticles.txt` drives a **3D scene**: camera `fov 50`, near 2 /
far 4096, `default_skybox MM_Skybox`, `music Vampire_Theme.mp3`. Three emitter groups
at origin `[0,0,-30]`, spawning in a cylinder (θ 0–360, radius 75):

- `M_Clouds_Emmiter`: `M_Clouds` (cloud, drifts down, z 57), `M_Fire` (fire-sprite,
  rate 400, z −5), `M_Fire2` (starpresence sparks, z 0).
- `M_Clans_Emmiter`: `M_Bru..M_Tre` (MM_<clan> symbols, z 50, `theta_speed −2` orbit).
- `M_Cels_Emmiter`: `M_Cels`/`M_Cels2` (BloodCel/BloodCel2, z 45–55).

Each child (`particles/m_*.txt`) is a mini-language: lifetime, sprite, keyframed
size/rotation/velocity/θ-speed, per-channel RGB + brightness/alpha keyframes, `mask 0`
= additive.

The original composites particles by summing sRGB-encoded color values directly into
an 8-bit framebuffer — an additive, premultiplied, gamma-space blend (`mask 0`), not a
linear-HDR blend. The camera sits at the emitter origin, looks down Source +X, and
yaws continuously at `camera_rotation` deg/s; each emitter group spawns on a radius-75
ring at `[0,0,-30]`, so only ~14% of any ring is inside the camera's view frustum at a
given moment.

**Confidence.** Only the fire emitter's on-screen brightness/color has been
quantitatively matched against a reference capture of the running game (`G/R`/`B/R`/`R`
channel ratios and clip stats). The cloud, cel, and clan-logo emitters are
reconstructed from the script data alone and have not been checked against a capture —
naive reconstructions of the 17 clan-logo spawns read sparse and faint (~34 particles
total, ~2 live per spawn, ~5 on screen, peak brightness 10–150/255), so their size,
brightness, density, and motion as authored are unverified. A pinned capture (or short
recording) of the real menu at a known camera yaw would let these be checked the way
the fire band was.
