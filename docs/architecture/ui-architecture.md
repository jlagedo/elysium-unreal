# UI architecture — the Unreal re-skin

How Elysium builds its user interface. The VtMB-facts counterpart is **`docs/vtmb/vtmb-ui.md`** (which code
owns which screen, the two schemes, the 1024×768 canvas law, the HUD class inventory); this doc
owns the Unreal stack, the design tokens and the screen inventory as rebuilt. The pairing works
like `docs/vtmb/controls.md` ↔ `docs/architecture/input-architecture.md`: a new fact about *VtMB's* UI goes there, a decision
about *ours* goes here. Per-task status: `docs/project/roadmap.md` (8.6, 8.8, 8.9, 8.10).

**The direction**: VtMB's screen structure, palette, iconography and strings are kept; its craft is
replaced (`docs/project/reconstruction-direction.md` axis 1). There is no classic UI mode.

---

## 1. The stack

**CommonUI + CommonInput**, with widget trees built in **C++ Slate**.

CommonUI supplies the activatable-widget stack, input routing, focus and gamepad navigation — most
of what roadmap 8.10 would otherwise hand-roll. What it normally costs is a Widget Blueprint asset
per screen and an editor content loop; that cost is avoided by overriding `RebuildWidget()` on a
`UCommonActivatableWidget` subclass and returning a Slate tree. So the screens are code, diffable
and reviewable, while the plumbing is the engine's.

CommonInput's Accept and Back rows are supplied by the native `UElysiumCommonUIInputData` class
(keyboard and controller defaults), selected in `DefaultGame.ini`. Nothing in the source-authored
UI requires a Widget Blueprint or data-table asset; the generated typefaces remain the only UI
`.uasset` inputs.

| Type | Role |
|---|---|
| `UElysiumUISubsystem` | GI-scoped flow facade. It owns menu/character/chargen policy and scratch state, but delegates screen lifetime and composition to the local-player owner. Verbs: `elysium.menu [pause]`, `elysium.menu.close` |
| `UElysiumPlayerUISubsystem` | The local-player lifetime owner and only viewport-entry surface. It owns the stable `UElysiumHUDModel`, reconciles retained dialogue/sign/loot screens and queued notifications, rebinds the current world's publisher across travel, creates the unified root, and exposes semantic `PushWidget` / `RemoveWidget` operations. A conversation or sign is one modal lifetime whose published state updates in place. Non-shipping verbs: `elysium.hud.preview off\|passive\|combat\|weapon\|discipline\|inventory\|critical\|radial\|brief\|elysium\|sneak`, `elysium.hud.notify item\|quest\|complete\|failure\|generic <text> [quantity]` |
| `UElysiumUIRoot` | The single local-player root. Paint order is structural rather than numeric: passive HUD, transient stack, notification queue, game-modal stack, system-modal stack, runtime-loading stack. Every root slot explicitly fills the player viewport and every layer is instant until it owns an authored transition. Hiding the HUD collapses only its passive surface, never the root or a menu/loading screen above it. |
| `UElysiumActivatableScreen` | Shared CommonUI screen lifecycle. It installs and releases one centrally defined Elysium screen policy on activation/deactivation while returning no CommonUI input-mode config, keeping `UElysiumInputSubsystem` the sole `SetInputMode` authority. The scope owns mode, cursor policy and gameplay contexts; CommonUI owns focus and restores the screen's desired target. |
| `UElysiumActionButton` | The programmatic `UCommonButtonBase` used for every action. It carries a stable action id, executable state, label/caption and accessible text; mouse, CommonUI activation and shortcuts all reach the same semantic callback. A non-executable explanatory row remains focusable. |
| `UElysiumNavigableScreen` | The focus/selection owner for interactive screens. It restores selection by action id, wraps linear lists, supports explicit neighbours and horizontal/vertical groups, synchronizes hover with focus, repairs dynamic lists, and suppresses duplicate activation until the action set transitions. |
| `UElysiumHUDWidget` | The resolution-independent in-world surface — life, vitae, standings, the hand and worn slots, the inventory selector, the stealth cluster, reticle and full-viewport fade. Regions and their rules: § 3. Unowned regions collapse instead of displaying fabricated runtime data. |
| `UElysiumNotificationScreen` | One passive item/quest/notice card in the root's CommonUI FIFO. It is top-centred, safe-zone aware, non-focusable and timed in real UI seconds; it never installs an input scope. |
| `UElysiumMainMenu` | the main / pause / game-over menu (`UElysiumNavigableScreen`) |
| `UElysiumCharacterScreen` | the character screen — sheet / info / quest log, one shell parameterised for chargen's tab set too. Verb: `elysium.charscreen`; keys `C` and `L` |
| `UElysiumDialogueScreen` / `UElysiumChargenPopup` / `UElysiumSignScreen` | Dynamic game-modal screens. Responses/answers retain authored shortcuts; a sign presents one Continue action while the entity world remains the final dwell and close-policy authority. |
| `UElysiumTerminalScreen` | A transparent game-modal screen which clips a modern 36×24 terminal panel to the physical monitor's projected screen rectangle. It renders revisioned terminal view state and routes text or semantic actions; the entity world owns commands and effects. Full contract: `docs/architecture/computer-terminal-architecture.md`. |
| `ElysiumUIStyle.{h,cpp}` | the design tokens — palette, type ramp, spacing, the virtual canvas — plus `FElysiumUIFontLibrary` |
| `ElysiumUIStrings.{h,cpp}` | the authored string table read from `$ELYSIUM_EXPORT_ROOT/ui/strings.json` |
| `ElysiumUITexture.{h,cpp}` | PNG → transient texture, shared by the use-icon atlas, sign backgrounds and the title lockup |

Every activatable screen enters through `UElysiumPlayerUISubsystem`; callers select a semantic
layer rather than a viewport Z-order. The root initializes a fresh or pooled instance before its
container activates it, which is when the caller supplies all per-open state and the input-scope
policy. Direct `AddToViewport`, `AddViewportWidgetContent`, manual `ActivateWidget`, and additional
`SetInputMode` writers are architectural violations. `Elysium.Substrate.UI.CompositionPolicy`
guards the viewport and input-mode boundaries in editor automation.

**A screen takes the mouse back from the debug UI.** While Cog holds ImGui input capture it consumes
the click before Slate sees it, so every menu item is dead — a restored Cog layout is not cosmetic,
it makes the game unplayable by mouse. No input mode can arbitrate that, because the capture is a
Slate catcher widget rather than a mode; only revoking it can. Cog therefore boots dormant and
discards its layout between runs (`elysium.CogPersist`, see `Source/ElysiumUE/CLAUDE.md`), and the
input scope stack revokes any capture the moment a UI-only scope is pushed
(`docs/architecture/runtime-architecture.md` §8.1). F1 still re-enables Cog deliberately — the revocation fires at
push time, not continuously, because the front end has a menu up permanently.

The runtime boundary is one-way: `UElysiumPresentationSubsystem` publishes an
`FElysiumViewState`; the player UI subsystem projects it into the Blueprint-readable model and
reconciles modal dialogue, signs and computer terminals; widgets render that state. A selector sends commands through
the input/command layer and never mutates the model or entity world. `UElysiumSignScreen` exposes a
visible Continue action and sends dismissal through the presentation subsystem;
`FElysiumEntityWorld` revalidates `MinShowTime` and `CloseOnLeftClick` before it closes anything.
The legacy `+attack` request reaches the same world seam. The blocking MoviePlayer loading
screen is the other deliberate exception: it
must render with no UObjects while the game thread is inside `LoadMap`. Its post-load continuation
uses the root's runtime-loading layer like every ordinary player surface.
The heads-up layer is also withheld while the entity world has a `camera_track` or named scripted
camera shot. Those owners already span the authored `PlayAsCamera*`/`SetCamera` through
`RestoreCameraToPlayerControl`/`RemoveCamera` lifetime, so ambient choreography without a camera
does not accidentally suppress the HUD; fades, dialogue and future cutscene subtitles remain up.

### Notification delivery

Item and quest owners emit `FElysiumNotification` through `IElysiumPresenter`; they never call UI.
`UElysiumPresentationSubsystem` retains the semantic FIFO until the player surface and a listener
exist, preserving same-frame order through loading and early New Game setup. Both the publisher and
local-player intake are bounded at 64; overflow drops the newest entry and warns with its kind and
subject. Notifications are transient world-epoch presentation and are cleared on travel/controller
replacement rather than serialized.

The root's notification queue shows one card at a time. The active card pauses and hides under
loading, pause/system UI, cinematics, signs, dialogue and loot; it resumes with its remaining
lifetime when the player surface returns. It enters for 0.20 seconds, holds for 2.40, exits for 0.25,
and advances the FIFO on removal. The layout is a 520-unit ink card 48 units below the top safe zone,
with a blood rule, gold Spectral SC category and bone Spectral subject.

This is an explicit presentation divergence from `CHudInfoBar`: the card uses no retail icon or
sound asset, and every successful player item admission—including scripted New Game bootstrap and
otherwise HUD-hidden items—posts a card. Inventory reconstruction during restore remains silent.
Quest notifications follow the recovered transaction boundary: only a resolved changed row posts,
after awards/event handling; incomplete is Updated, success Completed, and failure/botch Failed.

## 2. The virtual canvas

Everything is authored in **VtMB's own 1024×768 space** and scaled once by `ScreenH / 768`
(`ElysiumUI::ScaleFor`), applied by an `SDPIScaler` at each screen's root. Consequences:

- The layout constants recovered from `CVMainMenu::PerformLayout` are the literal layout code.
- Width is *not* divided — the virtual width is `ScreenW·768/ScreenH`, so content reflows into real
  widescreen and ultrawide with no letterbox and no `//ws-fix` coordinate pairs.
- Signs (`CSignUI`), the menu and the HUD share one law.
- The supported acceptance range is Full HD through 4K: 1920×1080, 2560×1440 and 3840×2160.
  Anchors, safe-zone margins, type and icons scale under the same law at every point in that range.

This is deliberately **not** the engine's `UIScaleCurve`. The curve would restate the same ratio in
an ini and could then drift from the canvas the panels are authored against; `ScaleFor` is the one
definition.

## 3. The HUD's regions

Retail draws the HUD as two ornate vertical rails at the screen edges
(`docs/vtmb/vtmb-ui.md` § 3). The rails are chrome and go; **the assignment they encode stays**, and
it is the whole of what the re-skin keeps: life on the left with the area icon above it, blood on
the right with the selected Discipline at its foot. Rotating the two rails into the bottom corners
is the modernization — no classic mode, so the craft is replaced and the structure is not.

Margins are 38 virtual px. Regions collapse when unowned rather than drawing a placeholder.

```text
+--------------------------------------------------------------+
|                     [ notifications ]      MASQUERADE  # # x x |
|                                             HUMANITY  7        |
|                          (reticle)                             |
|                    +--------------------+                      |
|                    | SNEAKING  = = = -- |  stealth, crouch only|
|                    | SEARCHING   12m    |                      |
|                    +--------------------+                      |
|  < [icon] >   selection peek                                   |
|  [icon] Light Clothing            worn                         |
|  [icon] .38 REVOLVER  6/24        hand      (o) BLOODHEAL      |
|  (o) MASQUERADE AREA              zone                         |
|  LIFE  72/100                                                  |
|  ============--                            * * * * * o o vitae |
+--------------------------------------------------------------+
```

| Region | Anchor | Rule |
|---|---|---|
| Life + zone icon | bottom left | The zone glyph sits directly above the life bar, which is where retail puts it. Life is a continuous bar; the value is shown for accessibility. |
| Hand, worn, selection peek | bottom left, stacked above life | The three read as one column about what the body is carrying. The peek is directly above the readout it changes. |
| Vitae + Discipline | bottom right | Vitae is **discrete** — one droplet per banked blood point, grouped in fives. Discipline sits above it, as retail sets its dial at the blood rail's foot. |
| Standings | top right | Masquerade and Humanity — slow-moving character state, permanently legible, out of the action corners. |
| Stealth cluster | bottom centre | Situational: on screen only while crouched. |
| Feed bar | top centre | Only during a feed. |
| Reticle, fade | centre / full | — |

**Masquerade is drawn as five marks, struck.** The sheet slot counts violations up from zero and the
fifth ends the run (`docs/vtmb/player-entity.md` § "Law, Masquerade and world response"), so the
marks still held are `5 - level` and each loss reads as a loss rather than as a number changing.
This is a **divergence, and the owner call is explicit**: retail carries no Masquerade element on
the HUD at all — the five masks live on the character sheet's `cm_topbar`, and the HUD's mask glyph
is the *area* icon, a different fact. A resource whose exhaustion ends the run is kept permanently
visible here rather than requiring a trip to the sheet.

**Humanity is a value, not pips.** It is a 0–10 scale (`docs/vtmb/game_runtime.md`), and ten dots
read slower than a numeral; the colour carries the part that matters, warming as it falls, because
frenzy checks roll against it.

### The stealth cluster states its own validity

The cluster is raised by the body's settled posture — the `Ducked`/`Lowering` stance off the same
locomotion sample the animation graph reads, so "sneaking" has one producer. Its two halves declare
validity separately, because they have different owners and land at different times:

- **concealment** — five steps, mirroring the exported `lightgauge_0..4`;
- **the observer** — the nearest eligible hostile, its distance and its committed detection state.

Presentation runs no perception (`docs/vtmb/stealth.md` → "HUD observability is not authority"). An
unmeasured gauge draws its steps empty with an explicit `--`: *a gauge nobody has measured and a
gauge reading "fully lit" are different statements and must not look the same*. An absent observer
clears its line rather than reporting a distance. `elysium.hud.preview sneak` renders the measured
form, since the unmeasured one is what production already draws.

### The selector is one cursor with two commit rules

The inventory selector renders whichever category the substrate's cursor is in; the categories and
their order are `system/items.txt`'s (`docs/vtmb/inventory.md` § 4), and the heading is that file's
own section name. What a selection *means* is the section's: a wielded section commits through the
equip funnel and changes the hand, and every other section moves an item cursor and does not.

Cycling commits immediately — there is no open-then-confirm state — so the selector's visibility is
a **peek**: presentation raises it when the selection changes and lets it fall (1.5 s, then a 0.35 s
tail, on real time so it keeps fading while paused). The peek is a screen state presentation
derived; no gameplay state knows the selector exists.

### Item icons are joined by classname

No item record names its icon (`docs/vtmb/vtmb-ui.md` § 3), so the join is ours: the classname stem
against the exported art tree, plus a small alias table for the records the tree files under another
name, falling back to the section's category glyph. A fallback glyph is a readable icon rather than
a hole. Worn armour always takes the glyph — its portrait needs a clan/sex/tier join the item data
does not carry.

## 4. Design tokens

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

- `UElysiumUISettings::MenuScrim` (Project Settings -> Elysium -> UI, default `0.22`; a
  `Config = Elysium, DefaultConfig` `UDeveloperSettings` page since R4.5 — it was
  `elysium.MenuScrim`, a hardcoded cvar default with no editor home) — the **classic** layout's
  global dimmer, applied to the whole frame because a centred column can land on anything the
  camera framed.
- The **rail** layout's veil — a horizontal ramp reaching zero by mid-frame, so only the strip the
  type sits on is paid for and the lit half of the backdrop is untouched. `MenuScrim` does not
  reach it.

**Blood red marks selection; it is not the ground.** Menu items rest in `Bone` and arm in
`BloodLit`, and a drawn-but-dead row drops to `BoneDim` — so *off* reads as off rather than as a
second red. The recovered `0xc00000a8` is what *armed* means; the
classic layout keeps it as the resting colour, which is what makes the A/B worth having.

The in-world HUD needs a stronger contrast contract than screens with controlled grounds. Its
Nocturne glyphs and code-authored vitae droplets carry a one-virtual-pixel near-black outline; the
reticle carries two. Exported context icons receive a slightly enlarged dark silhouette, and the
two bottom meter clusters sit over shallow procedurally generated corner veils. These treatments
are HUD-local: applying them to menu and sheet type would muddy surfaces that already own a scrim.
The veils are static rather than scene-luminance adaptive, avoiding colour flips and flicker as the
camera crosses a bright edge.

## 5. Type

The **Nocturne** system: **Spectral SC** for small-caps labels,
**Spectral** for body copy, **Inter** for data and numerals. VtMB's small-caps-with-wide-tracking
signature is kept — it is an authored art decision, not a hardware constraint — while the 28 bitmap
`.fnt` atlases are not.

Faces reach the runtime as generated local **`UFontFace` assets** under `/Game/ElysiumGenerated/UI/Fonts`, imported by
`pipeline/unreal/make_ui_fonts.py` from `Content/Fonts` (placed by `pipeline/src/elysium_pipeline/devtools/fetch_ui_fonts.py`).
`FElysiumUIFontLibrary` composes them into one runtime `UFont` per role with the weights as named
typeface entries, because `FSlateFontInfo` resolves a composite font, not a bare face.

A fourth, single-purpose role sits beside Nocturne: **`Mono`, Terminus (TTF)** (Regular and Bold,
OFL 1.1 with Reserved Font Names, shipped unmodified), used only by the computer-terminal console
(`docs/architecture/computer-terminal-architecture.md` §6.4). It is the Linux console and xterm
face of the late 90s, chosen by the owner on 2026-09-07 for the retro terminal look; it never
appears in menus, HUD or signs.

Two constraints:

- **`make_ui_fonts.py` cannot run in the headless content commandlet.** Importing a
  `UFontFace` flushes Slate's font cache and `FSlateApplication::Get()` asserts in a
  commandlet (`-run=pythonscript` never creates a Slate application;
  `-AllowCommandletRendering` does not help). `uv run elysium export bundle policy`
  therefore coordinates a second, Slate-enabled editor pass for the font packages.
- Sizes in `ElysiumUI::Type` are virtual px. The sign panel still resolves the older Plex/Zilla set
  through `ElysiumSignFonts.cpp`; 8.8 migrates it onto this ramp.

## 6. The menu plate

The front end stays in the genuinely empty `/Game/ElysiumGenerated/Boot` boot world and draws the
plate `UElysiumUISettings::MenuWallpaper` names (Project Settings → Elysium → UI → Menu, a
`TSoftObjectPtr<UTexture2D>`; unset by default, in which case the rail stands on the boot world's
black — R6.6, §9) beneath the CommonUI menu. Cold boot therefore loads no VtMB map, creates no map
actor or entity substrate, and waits on no runtime activation barrier. New Game's story entry is
the process's first VtMB map load.

The plate is 3840×2160 and uses uniform cover scaling: no distortion or letterbox at other aspect
ratios, with the longer axis clipped. Its composition reserves a dark right-hand field for the rail.
The title, seal, labels, focus marker and captions remain live resolution-independent Slate rather
than being rasterised into the art. Pause and game-over modes do not draw the plate; they remain
overlays over the held play world.

The plate incorporates decoded clan sigils, so it is local, game-derived output under the
gitignored export root and is never tracked. If it is absent or invalid, the screen remains usable
over the boot world's black clear rather than attempting to load a fallback game map.

`UElysiumMapSubsystem::EnterFrontEnd` returns to the same empty shell from a run. It latches the
front-end predicate before `OpenLevel`, so `PostLogin` seats no pawn in the destination world.
`elysium.BootMenu 0` remains the direct-to-story A/B.

`elysium.BootMenu 0` boots straight into play for A/B; `-ElysiumMap=` bypasses the menu entirely.

## 7. Screenshots and the UI

`ElysiumScreenshot::Request` takes **`bShowUI`**, default false.

- The **regression harness** (`FElysiumShotRun`, `uv run elysium debug shots`) keeps `false`: a shot must not change
  because a Cog window happened to be open, and every existing baseline was captured that way.
- The **MCP `elysium_screenshot` tool** passes `true`, because its job is to show what the player
  sees and since 8.6 that includes the menu.

Signs, dialogue and the unified HUD root are Slate UI and obey `bShowUI`. A UI-free capture omits
all of them; HUD and modal regression captures must explicitly include UI.

UI regression captures must request `bShowUI`; tracker work for HUD/UI coverage lives in
`docs/project/roadmap.md` 8.9.

## 8. Screen inventory

Screen implementation status lives only in `docs/project/roadmap.md`. The entries below record the shared
design shape and constraints, not a second completion ledger.

- **Main menu** — **two layouts, one widget**, A/B'd live by `elysium.MenuLayout`. Labels resolve
  `VMainMenu_BTN_*` against the authored table with retail English as the fallback in both, which is
  what `CVMainMenu` itself does.

  **Rail (1, the default).** The menu stands in a right-hand rail: title lockup (the imported
  `interface/mainmenu/vtm_title`, §9) on the bottom edge of a fixed head block, then the
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

  **Selected ≠ executable.** A row whose destination is missing remains a focusable
  `UElysiumActionButton`, so keyboard, controller and hover can select it and expose its caption
  ("No saved games yet."). Execution is gated separately and the label colour reports the state.
  Selection persists when the pointer leaves the rail, so the marker reads as a cursor rather than
  a transient hover highlight.

  **Classic (0).** `CVMainMenu::PerformLayout` verbatim: every item sized to the widest label +
  `20×4` virtual px, `pitch = height + 2`, the column centred, blood red at rest, the whole frame
  behind it knocked back by `UElysiumUISettings::MenuScrim`. Kept so the divergence stays measurable rather than
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
  that share art and layout but not code (`docs/vtmb/vtmb-ui.md`); one shell is the same thing to the player
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
  the whole row is the hit target rather than the bubbles. The same row is a CommonUI action:
  Up/Down selects, Right or Accept buys, and Left or X/Square sells. LB/RB and Q/E change major
  tabs; horizontal base and hub rows cycle with Left/Right. Footer and Back paths are semantic
  actions in the same focus graph.

  **The Base tab is chargen's only extra body**: clan, gender and history as rows of selectable
  words with a framed write-up beside them, and a single `NEXT` in the footer. Retail draws three
  dropdowns; at chargen every list is short enough to show whole, and a list that is always open is
  one fewer state than a combo box — the UI has no classic mode (`docs/project/reconstruction-direction.md` axis 1).

  **Info is still a framed placeholder.**

  A tab or hub change swaps the strip, footer and body **in place** (`Refresh`), never through a
  teardown: the screen changes tab from inside its own key handler, and destroying the widget there
  would drop keyboard focus and churn the input scope for what is a content change.

  **The chrome is VtMB's own sheet art**, every piece guarded — the `T_` assets live in the
  gitignored `/ElysiumBaked` mount (§9), so each image degrades to a token-drawn equivalent and
  logs Verbose once rather than leaving a hole. The panel frames are 9-sliced from a **measured UV sub-rectangle**: each is a power-of-two
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
  actors in a pocket of whatever world is loaded — an `ACameraActor` the controller looks through,
  an unlit quad carrying
  `charactermaintenance/background` as a **fixed wallpaper**, and the body itself with collision off,
  standing the idle `UElysiumAnimSubsystem::PickIdleClip` would give an NPC. The mesh goes through
  the same world-free `ElysiumNpcVisual::LoadMesh` the game's own NPC bodies use, resolved by
  `FElysiumClanTable::PlayerBodyStem` — the one lookup `Elysium.Content.PlayerBodies` also asserts.

  It **remembers the previous view target and restores it on teardown**, before the input scope pops:
  the in-game screen has a camera to give back and chargen does not, and a null is what says so.
  Every piece degrades independently — no baked body, no body; no backdrop texture, no quad; no world, no
  stage — and the panels stand alone in each case.

- **The chargen popup** — `UElysiumChargenPopup`, one `charcreatewizard.txt` popup full-screen: the
  framed page art with the question at its top-left and the surviving answers as numbered lines
  below. The entry popup and every quiz question are the same widget; they differ only in the
  `FElysiumWizPopup` handed to it. The answers carry their own numbers in the data ("1. Male?"), so
  nothing is prefixed. The authored `Region`/`TextRegion` are read as **intent**, not as a runtime
  coordinate system: the page is centred in the virtual canvas and its text column takes the
  proportion the data asks for. The widget draws and reports which line was clicked; everything that
  decides what happens next is `ElysiumChargen::WizChoose`. Up/Down wraps, Accept chooses the
  focused answer, number keys 1–9 invoke those same action ids, and Back is consumed because the
  authored graph has no backward edge.

- **Dialogue** — one `UElysiumActionButton` per visible response, or one Continue action on a
  terminal turn. Stable `.dlg` row ids preserve selection across turn refreshes; a removed response
  repairs to the nearest surviving row. Up/Down wraps, Accept uses the focused response, number
  keys 1–9 invoke the same actions, and Back is consumed. A turn rebuild replaces the response
  controls before CommonUI refreshes focus, so focus never falls through to the screen wrapper.

  **2026-09-06 (D5, dialogue plan).** The view the box reads grew from a list of strings into a
  list of decided rows. `FElysiumDialogueView::Choices` is now `FElysiumDialogueChoiceView
  { Text, Label, bEnabled, Kind, Have, Required, BloodCost, LineId }`, beside `bNpcSpeaking`,
  `bCanSkip` and `bNoValidReply`; `ChoiceIds` stays as the durable row identity. The gate was
  evaluated once in the substrate (`FElysiumDlgDependency::Explain`) and the UI evaluates nothing:
  it draws `bEnabled` and a pre-formatted `Label`. The projection itself is a pure function,
  `ElysiumDialogueUI::FillTurn(conversation, view)`, so the whole turn rule — band, subtitle, the
  no-valid-reply substitution — is assertable with a conversation and no world; the publisher adds
  only what needs the entity world (speaker, owner, the two voice flags, `bTerminal`).

  Four named modernizations land in the widget. **M-REQ**: `ElysiumDialogueUI::FormatRequirementLabel`
  writes `[ PERSUASION 4/7 ]` — trait upper-cased, player rating over threshold — as a separate run
  before the sentence in the accent colour, on passing rows as well as failing ones, with
  `· N BLOOD` appended for a discipline; it replaces retail's font/colour encoding
  (`GetFontForFlagsDependency` `0x10053f10`) and its dot prefix (`CDialogDependency::ToStr`
  `0x100ea5e0`). **M-DISABLED**: a row failing only its skill front is drawn dimmed (45%) as plain
  Slate and is never given a `UElysiumActionButton`, which is what makes it unclickable,
  unhoverable and unreachable by focus in one stroke; numbering stays 1..N in author order across
  enabled and disabled rows, so a key never names a different line as the player's skills change,
  and focus repair walks outward to the nearest enabled row. **M-REVEAL**: the band is published
  with the line rather than withheld behind `ShowPlayerChoices`. **M-SKIP**: while the voice runs
  the box shows a "Space: skip" hint and Space is the hurry verb rather than Continue.

  Input verbs, in the order the screen tests them. The skip verb is claimed in
  `NativeOnPreviewKeyDown`, ahead of `UElysiumNavigableScreen`'s own SpaceBar rule (which activates
  the focused action) — otherwise the hurry key would pick a response instead of ending the voice:
  `ElysiumDialogueUI::IsSkipKey` (Space while `bNpcSpeaking && bCanSkip`) →
  `UElysiumDialogueScreen::OnSkip` →
  `UElysiumPresentationSubsystem::DialogueSkip` → `FElysiumEntityWorld::PlayerDialogSkip`; then
  `ChoiceForKey` (1-9 and the numpad, positional) filtered by `UElysiumDialogueScreen::AcceptsChoice`,
  which refuses a disabled row and refuses anything during a live automatic wait — a refused key is
  still consumed, because it belongs to the conversation. Continue (`-1`) covers the terminal turn,
  the no-valid-reply band and retail's forced visible response when Auto-End finds no audio.

  The dialogue reconcile key gained the voice flag: `ElysiumView::ReconcileDialogue` compares
  (conversation, revision, `bNpcSpeaking`). The voice ending is not a new turn but does change what
  is drawn, and it flips at most once per line, so this costs one extra in-place rebuild per line
  and never a per-frame one.

  **2026-09-06 (D5 review fixes).** Four corrections, each with the reason it was not a symptom.
  **The all-disabled band draws its rows.** `FElysiumDlgConversation::IsTerminalLine()` is defined
  as "no *enabled* choice", so `bTerminal` is raised by exactly the M-DISABLED band whose rows all
  fail their skill fronts; keying the Continue-only branch on it erased every requirement label at
  the one moment the modernization exists to show them. The Continue-only branch is now gated on an
  empty `Choices`, and a terminal band with rows draws the dimmed rows and then Continue.
  `ChoiceForKey` follows: on such a band the number keys keep naming their rows (the screen refuses
  them) and only Space/Enter advance, so key 1 can never silently mean Continue while row 1 is on
  screen. **The blood-short label names the pool**: `FormatRequirementLabel` writes
  `[ DOMINATE 3/2 · 1/2 BLOOD ]` (pool held / price) when the substrate sets
  `FElysiumDlgGateLabel::bBloodShort`, since a bare `2 BLOOD` beside a passing rating reads as an
  unexplained refusal; an affordable row keeps `· 2 BLOOD`. **The reconcile identity is a serial,
  not an address**: `FElysiumDialogueView::DialogSerial` carries
  `FElysiumEntityWorld::GetOpenDialogSerial()` and `ReconcileDialogue(ShownSerial, ShownRev, …)`
  compares it, because a one-turn conversation that closes and opens another in the same frame can
  put the successor on the freed allocation while every conversation restarts its revision at 1 —
  (address, revision) repeats and the box would keep the dead band up. `Conversation` remains in the
  view as content identity only. **The pick carries its line id**:
  `UElysiumDialogueScreen::OnChoice(index, lineId)` →
  `UElysiumPlayerUISubsystem::OnDialogueChoice` → `UElysiumPresentationSubsystem::DialogueChoose`
  → `FElysiumEntityWorld::PlayerDialogChoose(index, ExpectedLineId)`, which refuses a position whose
  id no longer matches. The id is resolved from `Dialogue.ChoiceIds` at pick time, not at button
  build time, so it is always the sentence the box is showing.

  **2026-09-06 (M-CAP and the keyboard).** Number keys cover rows 1-9 and stop there, while M-CAP
  removes retail's four-response wire cap and allows a band of any N. Rows 10 and beyond are drawn,
  numbered `10.`, `11.`, … in the same 1..N author order, and are reachable by mouse and by
  gamepad/arrow focus, which walks the whole band — they simply carry no keyboard shortcut. No key
  wraps onto them, so a number never names a different row as the band grows.

  Type comes from `ElysiumUIStyle`'s faces rather than `FCoreStyle` (M-UI), all sized in the
  768-high virtual canvas with the screen's one `SDPIScaler` applying `ElysiumUI::ScaleFor(ScreenH)`.

  **2026-09-07 (owner call, the dialogue band is one face).** The band drops the Nocturne
  three-role mix and draws entirely in **Inter** (the `Data` role) at 15/18/18/12/11 virtual px
  (speaker / line / choice / requirement label / skip hint), down from 22/20/18/15/12. Reason:
  conversation text is read at speed over a lit 3D scene, where Spectral's serifs close up at
  body sizes and the mixed serif/sans band reads as decoration rather than as speech; Inter's
  x-height and open apertures are the project's most fail-safe legibility choice and the face
  already ships as `FF_Inter_*`. Spectral SC remains the voice of the menus, the sheet and the
  HUD labels — it is not the voice of a conversation. The speaker's name keeps its small-caps
  silhouette by being drawn uppercase with 0.06 em tracking (`SpeakerTrackingEm`) rather than by
  a small-caps cut, which Inter does not ship. The emphasis order inverts with it: the spoken
  sentence is now the largest run in the band and the speaker's name is an attribution above it,
  asserted in `Elysium.Substrate.UI.ScalingAndDialogueInput`.

  **The sizes are retail's, measured.** A 1999x1124 client capture of Jack's tutorial conversation,
  divided by that client's 1124/768 canvas scale, gives retail's band directly:

  | | retail (vp) | port |
  |---|---|---|
  | panel width | 866 | 864 |
  | text column | ~791 | ~782 |
  | panel bottom inset | 21 | 24 |
  | font em, NPC line | 17.8 | 18 |
  | font em, choice | 17.8 | 18 |
  | baseline pitch | 27.7 | ~28 |
  | leading above the em | ~9.9 | 6 of widget padding + the face's own ascent/descent |

  Cap height measures 19 px and x-height 14 px (em ~26 px at that client), and the baseline pitch
  is a dead-constant 40.5 px across all seven drawn rows. Crucially the orange NPC line and the
  white choices **measure identically** — `CHudDialog` draws the band at one size and separates the
  two by colour alone — which is why `LineFontVirtualPixels == ChoiceFontVirtualPixels` and why the
  assertion is `>=` rather than `>`. The capture independently confirms `ResponsePanelWidth`:
  retail's box is 1267 px wide, or 866 vp against the shipped 864.

  The band's "wall of choices" feel is set by the **pitch, not the size**. The port's pre-measurement
  rows spent 16 vp per row on widget padding (`SBox` 6 vertical + a 2 vp slot gap, doubled) against
  retail's entire 9.9 vp leading — smaller type than retail on rows 25% taller than retail. The
  padding is now 2 vertical with a 1 vp gap (`ChoiceRowPaddingVertical`, `ChoiceRowGap`), and
  `ChoiceRowLeadingBudget < RetailBandLeadingVirtualPixels` is asserted so it cannot grow back.

  Measure is **not** a divergence: retail's line runs ~94 characters against the port's ~100, so
  narrowing `ResponsePanelWidth` would move away from retail, not toward it.

  **2026-09-07 (owner call, no row plates and a black slab).** Two consequences of the tight pitch,
  both fixed together:

  - **The panel was grey, and measurably so.** The slab tint was `FLinearColor(0.02, 0.02, 0.03)` —
    a *linear* colour, and linear 0.02 is sRGB byte **39**, not black. At its 0.86 alpha the panel
    composited to ~40/255 over a lit scene where retail's box measures **~15/255**: 2.7x too light.
    It is now pure `(0, 0, 0)` at **alpha 0.69**, retail's own, recovered from the Jack capture by
    solving `inside = scene * (1 - alpha)` over six channel samples above and below the box
    (range 0.65-0.71, mean 0.688). Zero and not a near-zero, because any lift is multiplied by the
    slab's area.
  - **A choice row carries no plate.** The per-row `SBorder` (white at 5% idle, blood at 55%
    selected) was legible at the old loose pitch, but at the retail pitch four adjacent filled
    rows read as ruled table cells — and four stacked 5% whites are themselves a grey wash over
    the slab. Rows are now bare text on the panel, as `CHudDialog` paints them, in an `SBox` that
    contributes padding only.

  Selection therefore has to live in the type, and it uses **two channels** so it does not rest on
  brightness alone: the sentence lifts bone -> white (`ColChoiceIdle` -> `ColChoiceSelected`) and
  the row's number takes the amber accent (`ColChoiceNumberIdle` -> `ColChoiceNumberSelected`). The
  four colours live in the `ElysiumDialogueUI` namespace rather than in either .cpp because the
  enabled row is built by the screen (a CommonUI action button) and the disabled row by the box
  (plain Slate), and the two paths must not drift. This highlight is the port's own: retail is
  keyboard-only and paints every row identically, so it has no equivalent to reproduce; the port
  needs one for mouse and gamepad focus.

  Baldur's Gate 3 — whose bracket shape M-REQ borrows — is the reference for that *shape* only.
  Its type is not: BG3 draws its UI in the PostAntiqua serif, ships no font-size setting, and its
  most-installed UI mods all *enlarge* the dialogue text, so its sizing is the trait players work
  around. No BG3 size table is recorded here or publicly published.

  **M-REFUSE** needs nothing here: the substrate posts a `Generic` `FElysiumNotification` with the
  subject "They won't talk right now." (`FElysiumNpc::BeginPlayerUse`), and
  `UElysiumNotificationScreen` already renders `Generic` under its "NOTICE" category label.

- **Tutorial/game signs** — `UElysiumSignScreen` is a game-modal, UI-only surface which does not
  request time control. A centered floating text box exposes one focusable Continue button. Enter,
  Space, Accept or a click on that button sends one dismiss request; before the dwell expires or
  when click-close is forbidden, the action remains selected but non-executable and the input
  cannot leak into gameplay. Back is consumed. The publisher exposes dismissibility for
  presentation, but the entity world repeats the check on every request so stale view state cannot
  close a sign.

- **Computer terminals** — `UElysiumTerminalScreen` leaves the computer and bezel in the world and
  projects only the modern character-grid panel onto screen metadata recovered from the model's
  `screen` material. It preserves authored strings and terminal rules without using VtMB's terminal
  font or VGUI styling. Keyboard line editing and the controller's semantic action list submit the
  same authoritative command; a nested CommonUI character grid handles the exceptional password or
  free-text value without requiring a physical keyboard. The widget never parses terminal content.
  Scope `ElysiumInput::Priority::Terminal` (42) is UI-only, above Dialogue and below Character. Camera,
  projection, controller, lifetime and acceptance contracts:
  `docs/architecture/computer-terminal-architecture.md`.

## 9. Art from assets (R6.6)

**Ruling (R6.6, `docs/project/seam_migration.md` → "Roadmap — one pipeline").** No screen reads an
image off the loose export root. Every piece of VtMB art the UI draws — HUD frames and icons, the
character sheet's chrome, the chargen popup pages, the sign backgrounds, the title lockup, the
clan sigils, the feed-vision mask — is the **`T_` asset the texture lane already publishes**
(`seam_map_texture.md` → "Import"), named by the same install path the screen always named:

```text
<dir>/<stem>   (a materials/ path, no extension; "hud/area_icons/area_icon_combat")
   -> /ElysiumBaked/Textures/<dir>/T_<safe stem>            FElysiumContentPaths::BakedTexture
   -> else the imported MI_ of the same path, its BaseTexture  (the material lane's own VMT join)
```

`ElysiumUI::ArtTexture(MaterialPath)` is that resolution (`Private/UI/ElysiumUiArt.h`). The
second step exists because a UI *material* path is not always its *texture* path: 317 of the
1,078 install materials under the UI trees name another texture in their VMT (`hud/disciplines/
bloodheal_hud` → `bloodheal_base`, every `_sel` variant, `general_items/flyer` →
`lillyonbeachphoto`), and the material lane already resolved that join once, on the `MI_`. For
every path a screen names by hand today the two steps agree (measured 2026-09-02; the HUD, sheet,
popup, menu and the 57 sign backgrounds are all identity), so the fallback is exactness for the
data-driven names — inventory art by item classname, sign `BackgroundImage`, chargen `Bkg_Image`
— not a second lane. Paths are normalised (lower-case, `\` → `/`, a leading `materials/` and a
trailing `.png` dropped) so authored data spells them as it always did. The Substrate tier pins the
fold (`Elysium.Substrate.UiArt`); the Content tier walks every name the runtime can form — the
literal tables, the 72 use icons and the ring, every `BackgroundImage` in the exported sign
definitions — and asserts each resolves (`Elysium.Content.UiArt`).

**What each site does now.** `FElysiumUiArtCache` (character screen), `UElysiumChargenPopup::Art`
and `UElysiumHUDWidget::HudArtBrush` keep their caches, brushes, UV sub-rectangles and 9-slices and
swap only the load. **The use-icon ring drops its composited atlas** (`UE_use_icons.py`,
`hud/use_icons.png/.json`; owner call, 2026-09-02): `ElysiumUseIconName(N)` is now the one table,
`ElysiumUI::UseIconArt(N)` folds it to `hud/context_icons/<name>` with the two on-disk aliases the
compositor carried (`Stakeable` → `stakable`, `valve` → `valvewheel`), and the widget holds 72
brushes on 72 `T_` assets plus the ring's (`context_icon_ring`) — the same 48-px cell, full UV.
**Sign backgrounds draw**: `FElysiumSignData::Background.ImageName` (already parsed) resolves to its
`T_` and becomes the panel's border image behind the body text, the panel's width and padding
unchanged; a sign with no background keeps the dark plate. The title lockup is
`interface/mainmenu/vtm_title`; the feed-vision mask is `effects/spotlight`; the character stage's
wallpaper quad is `interface/charactermaintenance/background`.

**The menu seal (named divergence, owner call filed in R7).** VtMB's front-end seals are the
particle scene's `particles/mm_<clan>.tga` sprites — loose TGAs, not `materials/` textures, so no
lane publishes them. The rail now draws the same clan's `interface/charactermaintenance/
cm_clan_symbol_<clan>` — the sigil the character sheet already flies for that clan — and, in the
front end where no character exists, **no seal** (the sect ankh `mm_cam` has no material twin).
The alternative is to admit `particles/*.tga` as texture units; that is the R7 question.

**The wallpaper** is not VtMB art and never was in the install: `UElysiumUISettings::
MenuWallpaper` is its home (§6), set in the editor, unset by default.

**Retired.** `ElysiumUI::LoadPngTexture` and the `ImageWrapper` decode behind it (the screenshot
writer keeps its own); `FElysiumContentPaths::UiArt/UiTitle/UiMenuDir/UiMenuSprite/
UiMenuWallpaper/UiFeedVisionMask/SignTexDir/SignBackgrounds`; the exporter outputs `ui/art/**`,
`ui/menu/**` (title, sprites, skybox faces, particle scripts), `ui/effects/`, `signs/tex/` +
`signs/backgrounds.json` and `hud/use_icons.*`, with `UE_use_icons.py` deleted and the `use-icons`
bundle gone from the profiles. `UE_extract_ui` keeps `resource/*.res`, `strings.json` and
`manifest.json`; `UE_extract_signs` keeps the definitions. `ui/strings.json` and `signs/*.txt` are
still loose reads (text, not art); their asset homes are R9.2's.
