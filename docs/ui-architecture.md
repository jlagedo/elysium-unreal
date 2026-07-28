# UI architecture — the Unreal re-skin

How Elysium builds its user interface. The VtMB-facts counterpart is **`vtmb-ui.md`** (which code
owns which screen, the two schemes, the 1024×768 canvas law, the HUD class inventory); this doc
owns the Unreal stack, the design tokens and the screen inventory as rebuilt. The pairing works
like `controls.md` ↔ `input-architecture.md`: a new fact about *VtMB's* UI goes there, a decision
about *ours* goes here. Per-task status: `roadmap.md` (8.6, 8.8, 8.9, 8.10).

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
| `UElysiumCharacterScreen` | the character screen — sheet / info / quest log, one shell parameterised for chargen's tab set too. Verb: `elysium.charscreen`; keys `C` and `L` |
| `ElysiumUIStyle.{h,cpp}` | the design tokens — palette, type ramp, spacing, the virtual canvas — plus `FElysiumUIFontLibrary` |
| `ElysiumUIStrings.{h,cpp}` | the authored string table read from `out/ui/strings.json` |
| `ElysiumUITexture.{h,cpp}` | PNG → transient texture, shared by the use-icon atlas, sign backgrounds and the title lockup |

**A `UCommonActivatableWidget` added straight to the viewport is collapsed until activated.**
`bAutoActivate` only fires for widgets pushed onto a `UCommonActivatableWidgetContainer`, so
`ShowMenu` calls `ActivateWidget()` by hand. Without it the tree builds correctly and draws nothing.

**A screen takes the mouse back from the debug UI.** While Cog holds ImGui input capture it consumes
the click before Slate sees it, so every menu item is dead — a restored Cog layout is not cosmetic,
it makes the game unplayable by mouse. No input mode can arbitrate that, because the capture is a
Slate catcher widget rather than a mode; only revoking it can. Cog therefore boots dormant and
discards its layout between runs (`elysium.CogPersist`, see `Source/ElysiumUE/CLAUDE.md`), and the
input scope stack revokes any capture the moment a UI-only scope is pushed
(`runtime-architecture.md` §8.1). F1 still re-enables Cog deliberately — the revocation fires at
push time, not continuously, because the front end has a menu up permanently.

## 2. The virtual canvas

Everything is authored in **VtMB's own 1024×768 space** and scaled once by `ScreenH / 768`
(`ElysiumUI::ScaleFor`), applied by an `SDPIScaler` at each screen's root. Consequences:

- The layout constants recovered from `CVMainMenu::PerformLayout` are the literal layout code.
- Width is *not* divided — the virtual width is `ScreenW·768/ScreenH`, so content reflows into real
  widescreen and ultrawide with no letterbox and no `//ws-fix` coordinate pairs.
- Signs (`CSignUI`), the menu and the HUD share one law.

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

**Grounds are ours, because VtMB has none.** Every background colour in `VampireScheme` is fully
transparent — its menu floats over a dark particle field, where the type never needs help. A real 3D
backdrop is not that reliable: how much dimming the type needs is a property of where the camera
points. Two answers, one per menu layout (§7):

- `elysium.MenuScrim` (default `0.22`) — the **classic** layout's global dimmer, applied to the
  whole frame because a centred column can land on anything the camera framed.
- The **rail** layout's veil — a horizontal ramp reaching zero by mid-frame, so only the strip the
  type sits on is paid for and the lit half of the backdrop is untouched. `MenuScrim` does not
  reach it.

**Blood red marks selection; it is not the ground.** Menu items rest in `Bone` and arm in
`BloodLit`, and a drawn-but-dead row drops to `BoneDim` — so *off* reads as off rather than as a
second red. The recovered `0xc00000a8` is what *armed* means; the
classic layout keeps it as the resting colour, which is what makes the A/B worth having.

## 4. Type

The **Nocturne** system: **Spectral SC** for small-caps labels,
**Spectral** for body copy, **Inter** for data and numerals. VtMB's small-caps-with-wide-tracking
signature is kept — it is an authored art decision, not a hardware constraint — while the 28 bitmap
`.fnt` atlases are not.

Faces reach the runtime as committed **`UFontFace` assets** under `/Game/VtMB/UI/Fonts`, imported by
`tools/make_ui_fonts.py` from `Content/Fonts` (placed by `tools/fetch_ui_fonts.py`).
`FElysiumUIFontLibrary` composes them into one runtime `UFont` per role with the weights as named
typeface entries, because `FSlateFontInfo` resolves a composite font, not a bare face.

Two constraints:

- **`make_ui_fonts.py` cannot run in the content.bat umbrella.** Importing a `UFontFace` flushes
  Slate's font cache and `FSlateApplication::Get()` asserts in a commandlet (`-run=pythonscript`
  never creates a Slate application; `-AllowCommandletRendering` does not help). It needs a
  Slate-enabled editor and is run by hand — a rare regeneration, only when a typeface changes.
- Sizes in `ElysiumUI::Type` are virtual px. The sign panel still resolves the older Plex/Zilla set
  through `ElysiumSignFonts.cpp`; 8.8 migrates it onto this ramp.

## 5. The menu backdrop

The menu stands in front of **real game geometry**, not a port of VtMB's particle scene — that
scene was never verified against a ground-truth capture, so "faithful" was not testable. `UElysiumMapSubsystem::TravelForMenu` loads `elysium.MenuMap` (default `sm_hub_1`,
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

Two things are knobs rather than constants for that reason (§3): `elysium.MenuScrim` and
`elysium.MenuMap`.

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

UI regression captures must request `bShowUI`; tracker work for HUD/UI coverage lives in
`roadmap.md` 8.9.

## 7. Screen inventory

Screen implementation status lives only in `roadmap.md`. The entries below record the shared
design shape and constraints, not a second completion ledger.

- **Main menu** — **two layouts, one widget**, A/B'd live by `elysium.MenuLayout`. Labels resolve
  `VMainMenu_BTN_*` against the authored table with retail English as the fallback in both, which is
  what `CVMainMenu` itself does.

  **Rail (1, the default).** The menu stands in a right-hand rail: title lockup (from
  `out/ui/menu/title.png`, the user's own art) on the bottom edge of a fixed head block, then the
  item column right-aligned against a gold hairline, then a reserved caption line. Every horizontal
  constant is measured **from the right edge**, never as a fraction of 1024 — the virtual canvas
  scales with the aspect ratio (§2), so a fraction would drift the rail inward on ultrawide. Three
  pieces carry it:

  - **The veil** (§3) and **the hairline** are code-authored alpha ramps
    (`ElysiumUI::MakeAlphaRamp`), coloured by brush tint — the ramp the layout constants were tuned
    against, rather than a gradient asset that could drift from them.
  - **The tick** — one blood bar riding the hairline, eased onto the armed row over ~130 ms by
    `NativeTick`. Row geometry is analytic (the row table is built as the column is), so the marker
    is placed by padding rather than by querying geometry. Slate ticks off the application, not the
    world, so it keeps running while a pause holds the world — which is the pause menu's only state.
  - **The seal** — a `mm_<clan>` sigil off the menu particle sprite sheet at 10% gold, bleeding off
    the right edge behind the column: the Camarilla ankh in the front end (no character exists yet),
    the PC's own clan in a session, resolved through `PlayerSheet()`. The emitter graph is not
    reproduced; its art is.

  **Armed ≠ enabled.** A row whose destination is missing is left *enabled* so it can take hover and
  focus — a disabled `SButton` takes neither, and arming is what makes its caption ("No saved games
  yet.") reachable. The click is gated instead, and the label colour reports the state. The armed row
  also *persists* when the pointer leaves the rail, so the marker reads as a cursor rather than a
  hover highlight.

  **Classic (0).** `CVMainMenu::PerformLayout` verbatim: every item sized to the widest label +
  `20×4` virtual px, `pitch = height + 2`, the column centred, blood red at rest, the whole frame
  behind it knocked back by `elysium.MenuScrim`. Kept so the divergence stays measurable rather than
  asserted.

  Both knobs are read at tree-build time, so `UElysiumUISubsystem::RebuildMenu` (a console-variable
  sink on each) takes an open screen down and puts it back up.
- **Pause menu** — the same widget with the pause item set. `SaveGame` is enabled only in game,
  reproducing the *entire* main-menu-vs-pause difference in retail (`CBasePanel::OnThink`). The rail
  replaces the wordmark with a `Paused` eyebrow — repeating the lockup over a held game is not what
  it is for — and the game-over mode replaces it with the reason the run ended, set in blood at
  display size. All three heads sit on the same block, so the item column starts at one Y in every
  mode.

Load Game / Save Game / Options draw disabled rather than absent, so the screen's shape matches
the original's even where the backing system is missing.

- **Character screen** — `UElysiumCharacterScreen`, one screen entered on a tab. `L` opens it on the
  quest log, `C` on the sheet, and pressing the other key while it is up **switches tab** rather than
  closing; that is what makes them two doors into one screen. Verb: `elysium.charscreen
  [sheet|info|quest]`. Scope `ElysiumInput::Priority::Character` (45) — above a conversation, below a
  menu, so a pause can open over it. Opening is gated on the app state being `Playing`; closing is
  not, so a state change can never strand it open.

  **The shell is parameterised on the four axes the chargen wizard differs on**
  (`FElysiumCharacterScreenMode`): the tab set, `EElysiumSpendMode`, whether the name is a text
  entry, and the footer. Retail splits the same apparent screen across three `client.dll` classes
  that share art and layout but not code (`vtmb-ui.md`); one shell is the same thing to the player
  and is what makes chargen a body rather than a rebuild.

  **The Sheet body is one widget serving both hosts.** `BuildSheet` draws the same three category
  blocks, the same feats panel and the same detail panel for chargen and for the in-game level-up
  screen, over the same `FElysiumChargenState`. `Mode.Spend` selects only the currency — the seven
  category pools or the `Experience` slot — and the heading counters and the detail panel's corner
  read that one field. **A zero pool draws no parenthetical at all**, which is what the retail
  capture shows for a tertiary category.

  Both hosts edit a **scratch**, never the character: nothing reaches the player entity until
  ACCEPT, which is what makes CANCEL a discard rather than an undo log. That is also why the row's
  bubbles have four states rather than two — a dot the character came in with, a dot bought this
  session and not yet committed, a dot the trait-effect layer added on top of the base, and nothing.
  VtMB ships art for all four (`cm_bubble_{filled,pending,bonus,empty}`).

  A row is drawn only while its **current** value is in `[0, 6)`, which is what gives a non-clan
  discipline no row at all rather than a greyed one. Left-click raises, right-click sells back, and
  the whole row is the hit target rather than the bubbles.

  **The Base tab is chargen's only extra body**: clan, gender and history as rows of selectable
  words with a framed write-up beside them, and a single `NEXT` in the footer. Retail draws three
  dropdowns; at chargen every list is short enough to show whole, and a list that is always open is
  one fewer state than a combo box — the UI has no classic mode (`remaster-direction.md` axis 1).

  **Info is still a framed placeholder.**

  A tab or hub change swaps the strip, footer and body **in place** (`Refresh`), never through a
  teardown: the screen changes tab from inside its own key handler, and destroying the widget there
  would drop keyboard focus and churn the input scope for what is a content change.

  **The chrome is VtMB's own decoded sheet art**, every piece guarded — `out/ui/art/` is gitignored,
  so each image degrades to a token-drawn equivalent and logs Verbose once rather than leaving a
  hole. The panel frames are 9-sliced from a **measured UV sub-rectangle**: each is a power-of-two
  page with the frame drawn top-left and the rest transparent, so the region is the frame's own
  extent and the margin is the corner scroll's share of it. `cm_divider` carries a curl at both ends,
  so the two rule terminals are two sub-rectangles of one page rather than one image mirrored.

  `Palette::Amber` is the art's gold, sampled off the divider and the frames — warmer than the
  scheme's text gold, which is a *text* colour. Drawn rules take the amber so a hairline continuing a
  bitmap rule reads as one line.

  **Two divergences, both ours.** The unread marker is *cleared* when the player leaves a hub or
  closes the screen (`MarkQuestsRead`): VtMB sets the byte on every write and never reads it, so its
  own clear rule is unrecoverable, and without one the marker would say "updated" forever. Per hub
  rather than globally, so markers survive on hubs that were not looked at. And **Failed collapses**
  to a labelled rule and a count while it is empty instead of holding a third of the screen, which is
  its usual state.

  Quest rows are ordered **newest assignment first** — `Order` is `max+1` at assignment, so it is the
  real chronology — and the hub tabs carry live open-quest counts, so work in a hub you are not
  looking at is still visible as a number.

- **The character stage** — `FElysiumCharacterStage`, the body behind the screen's panels. Raised
  for **both** hosts: VtMB's own screen draws the character *through* its translucent panels in game
  as well as at chargen, which is what proves the body is geometry and not a picture. Three transient
  actors in a pocket of whatever world is loaded — an `ACameraActor` the controller looks through
  (`UElysiumGameFlowSubsystem::EnterMenuBackdrop` is the precedent), an unlit quad carrying
  `charactermaintenance/background` as a **fixed wallpaper**, and the body itself with collision off,
  standing the idle `UElysiumNpcAnimSubsystem::PickIdleClip` would give an NPC. The mesh goes through
  the same world-free `ElysiumNpcVisual::LoadMesh` the game's own NPC bodies use, resolved by
  `FElysiumClanTable::PlayerBodyStem` — the one lookup `Elysium.Content.PlayerBodies` also asserts.

  It **remembers the previous view target and restores it on teardown**, before the input scope pops:
  the in-game screen has a camera to give back and chargen does not, and a null is what says so.
  Every piece degrades independently — no `.glb`, no body; no backdrop texture, no quad; no world, no
  stage — and the panels stand alone in each case.

- **The chargen popup** — `UElysiumChargenPopup`, one `charcreatewizard.txt` popup full-screen: the
  framed page art with the question at its top-left and the surviving answers as numbered lines
  below. The entry popup and every quiz question are the same widget; they differ only in the
  `FElysiumWizPopup` handed to it. The answers carry their own numbers in the data ("1. Male?"), so
  nothing is prefixed. The authored `Region`/`TextRegion` are read as **intent**, not as a runtime
  coordinate system: the page is centred in the virtual canvas and its text column takes the
  proportion the data asks for. The widget draws and reports which line was clicked; everything that
  decides what happens next is `ElysiumChargen::WizChoose`.
