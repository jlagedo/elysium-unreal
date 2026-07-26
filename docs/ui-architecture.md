# UI architecture — the Unreal re-skin

How Elysium builds its user interface. The VtMB-facts counterpart is **`vtmb-ui.md`** (which code
owns which screen, the two schemes, the 1024×768 canvas law, the HUD class inventory); this doc
owns the Unreal stack, the design tokens and the screen inventory as rebuilt. The pairing works
like `controls.md` ↔ `input-architecture.md`: a new fact about *VtMB's* UI goes there, a decision
about *ours* goes here. Per-task status: `roadmap.md` (8.6, 8.8, 8.9, 8.10). The owner calls behind
the choices below are dated in `decisions.md` (2026-07-26).

**The direction**: VtMB's screen structure, palette, iconography and strings are kept; its craft is
replaced (`remaster-direction.md` axis 1). There is no classic UI mode.

---

## 1. The stack

**CommonUI + CommonInput**, with widget trees built in **C++ Slate**.

CommonUI supplies the activatable-widget stack, input routing, focus and gamepad navigation — most
of what roadmap 8.10 would otherwise hand-roll. What it normally costs is a Widget Blueprint asset
per screen and an editor content loop; that cost is avoided by overriding `RebuildWidget()` on a
`UCommonActivatableWidget` subclass and returning a Slate tree. So the screens are code, diffable
and reviewable, while the plumbing is the engine's.

The only asset CommonInput requires is a `CommonUIInputData` (the Back/Click actions). Nothing else
in the UI is a `.uasset` except the typefaces.

| Type | Role |
|---|---|
| `UElysiumUISubsystem` | GI-scoped owner of the screens. Creates/shows/tears down, owns the input-mode switch. GI-scoped because the menu outlives any one world — it is up before the first map and survives the travel New Game triggers. Verbs: `elysium.menu [pause]`, `elysium.menu.close` |
| `UElysiumMainMenu` | the main / pause menu (`UCommonActivatableWidget`) |
| `ElysiumUIStyle.{h,cpp}` | the design tokens — palette, type ramp, spacing, the virtual canvas — plus `FElysiumUIFontLibrary` |
| `ElysiumUIStrings.{h,cpp}` | the authored string table read from `out/ui/strings.json` |
| `ElysiumUITexture.{h,cpp}` | PNG → transient texture, shared by the use-icon atlas, sign backgrounds and the title lockup |

**A `UCommonActivatableWidget` added straight to the viewport is collapsed until activated.**
`bAutoActivate` only fires for widgets pushed onto a `UCommonActivatableWidgetContainer`, so
`ShowMenu` calls `ActivateWidget()` by hand. Without it the tree builds correctly and draws nothing.

**The debug UI outranks nothing.** While Cog holds ImGui input capture it consumes the click before
Slate sees it, so every menu item is dead — a restored Cog layout is not cosmetic, it makes the game
unplayable by mouse. Cog therefore boots dormant and discards its layout between runs
(`elysium.CogPersist`, see `Source/ElysiumUE/CLAUDE.md`), and `ApplyInputMode` revokes any capture
when a screen comes up. F1 still re-enables Cog deliberately.

## 2. The virtual canvas

Everything is authored in **VtMB's own 1024×768 space** and scaled once by `ScreenH / 768`
(`ElysiumUI::ScaleFor`), applied by an `SDPIScaler` at each screen's root. Consequences:

- The layout constants recovered from `CVMainMenu::PerformLayout` are the literal layout code.
- Width is *not* divided — the virtual width is `ScreenW·768/ScreenH`, so content reflows into real
  widescreen and ultrawide with no letterbox and no `//ws-fix` coordinate pairs.
- Signs (`CSignUI`, `decisions.md` 2026-07-23), the menu and the HUD share one law.

This is deliberately **not** the engine's `UIScaleCurve`. The curve would restate the same ratio in
an ini and could then drift from the canvas the panels are authored against; `ScaleFor` is the one
definition.

## 3. Design tokens

Colours are read from the install's own `VampireScheme.res` — the scheme `client.dll` loads — not
invented. The chrome is **gold**; blood red is an accent for the menu column, the pips and critical
states, and cyan marks the active tab.

| Token | Source | Use |
|---|---|---|
| `Gold` / `GoldLit` | `VUnselectedText` / `VDesHeaderText` | chrome, labels, headers |
| `Bone` / `BoneDim` | `BaseText` / `DimBaseText` | body copy |
| `Cyan` | `BrightControlText` | the active tab |
| `Blood` / `BloodLit` | `0xc00000a8` in `CVMainMenu` (hardcoded, in neither scheme) | menu items, pips |
| `Scrim` | **ours** | see below |

**The scrim is ours and it is a knob, not a constant.** Every background colour in `VampireScheme`
is fully transparent, because VtMB's menu floats over a dark particle field. A real 3D backdrop is
not that reliable: how much dimming the type needs is a property of where the camera points. Blood
red on a sunlit alley wall needed a lot; the night street outside the Asylum needs almost none.
`elysium.MenuScrim` (default `0.22`) is the dial — the presentation layer paying for a modernised
backdrop, not a divergence from the look.

## 4. Type

The **Nocturne** system (`decisions.md` 2026-07-26): **Spectral SC** for small-caps labels,
**Spectral** for body copy, **Inter** for data and numerals. VtMB's small-caps-with-wide-tracking
signature is kept — it is an authored art decision, not a hardware constraint — while the 28 bitmap
`.fnt` atlases are not.

Faces reach the runtime as committed **`UFontFace` assets** under `/Game/VtMB/UI/Fonts`, imported by
`tools/make_ui_fonts.py` from `Content/Fonts` (placed by `tools/fetch_ui_fonts.py`).
`FElysiumUIFontLibrary` composes them into one runtime `UFont` per role with the weights as named
typeface entries, because `FSlateFontInfo` resolves a composite font, not a bare face.

Two constraints worth knowing before touching this:

- **`make_ui_fonts.py` cannot run in the content.bat umbrella.** Importing a `UFontFace` flushes
  Slate's font cache and `FSlateApplication::Get()` asserts in a commandlet (`-run=pythonscript`
  never creates a Slate application; `-AllowCommandletRendering` does not help). It needs a
  Slate-enabled editor and is run by hand — a rare regeneration, only when a typeface changes.
- Sizes in `ElysiumUI::Type` are virtual px. The sign panel still resolves the older Plex/Zilla set
  through `ElysiumSignFonts.cpp`; 8.8 migrates it onto this ramp.

## 5. The menu backdrop

The menu stands in front of **real game geometry**, not a port of VtMB's particle scene (which the
Godot prototype never verified against ground truth, so "faithful" was not testable —
`decisions.md`). `UElysiumMapSubsystem::TravelForMenu` loads `elysium.MenuMap` (default `sm_hub_1`,
the Asylum frontage) as a **backdrop**.

A backdrop is an ordinary map build **minus the player**. The substrate builds in full, because the
NPCs standing and idling in frame *are* entities — a look-only build is an empty street, and the
crowd is why that vantage was chosen. `AElysiumGameMode` returns null from
`GetDefaultPawnClassForController` and makes an `ACameraActor` at `elysium.MenuVantage` the view
target. The mode is latched at **Travel** time, not when the map actor spawns, because `PostLogin`
— which decides the pawn — runs before `BeginPlay`.

The map's own logic runs, and that is a feature: `sm_hub_1`'s streetlight relays cycle the crossing
signals behind the menu. What it also means is that the map can try to talk to the player, so
**while a menu is up `AElysiumHUD` stands the player-facing HUD down** — no reticle, no sign panel,
no dialogue box (`IsMenuUp()`). Not hypothetical: `sm_hub_1`'s `havenbum` opens a conversation
unprompted, and the B4 box drew over the menu until it was gated. The conversation still runs in the
entity world; only its UI is withheld. The `env_fade` quad still draws — it is a screen effect, not
a HUD element.

Leaving the backdrop is an ordinary Travel: New Game opens the story entry with a player in it.

Because a real 3D scene is far less predictable than a designed backdrop, two things are knobs
rather than constants: `elysium.MenuScrim` (§3) and `elysium.MenuMap`.

The menu camera is deliberately **not** an entry in `ElysiumVantages::Table`: that table is the
profiling and screenshot baseline and `Resolve("")` returns every vantage for a map, so adding one
there would silently change what `profile.bat` and `shots.bat` measure. Retune with
`elysium.campos`, which logs a paste-ready position/rotation.

`elysium.BootMenu 0` boots straight into play for A/B; `-ElysiumMap=` bypasses the menu entirely.

## 6. Screenshots and the UI

`ElysiumScreenshot::Request` takes **`bShowUI`**, default false.

- The **regression harness** (`FElysiumShotRun`, `shots.bat`) keeps `false`: a shot must not change
  because a Cog window happened to be open, and every existing baseline was captured that way.
- The **MCP `elysium_screenshot` tool** passes `true`, because its job is to show what the player
  sees and since 8.6 that includes the menu.

The Canvas HUD draws with the world and appears either way — which is why a UI-free capture can look
convincing (the reticle is there) while every Slate widget is silently missing.

**Open:** the shots harness therefore cannot see the UI layer. When 8.9 puts the HUD on this stack,
either it grows UI-inclusive vantages or HUD regressions go unwatched.

## 7. Screen inventory

Built:

- **Main menu** — title lockup (from `out/ui/menu/title.png`, the user's own art), item column laid
  out by the RE'd law: every item sized to the widest label + `20×4` virtual px, `pitch = height + 2`,
  centred. Labels resolve `VMainMenu_BTN_*` against the authored table with retail English as the
  fallback, which is what `CVMainMenu` itself does.
- **Pause menu** — the same widget with the pause item set. `SaveGame` is enabled only in game,
  reproducing the *entire* main-menu-vs-pause difference in retail (`CBasePanel::OnThink`).

Not built: chargen (9.4), load/save (9.5), options + accessibility (8.10), the sign/popup re-skin
(8.8), the HUD (8.9), the dialogue UI (9.2). Load Game / Save Game / Options draw disabled rather
than absent, so the screen's shape matches the original's even where the backing system is missing.
