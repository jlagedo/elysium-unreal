# Characters & UI plan — open-task specifications

Specifications for **open** P8 tasks. Status lives solely in `docs/project/roadmap.md`; a task
that lands is deleted here. No status marks in this file. Design:
`docs/architecture/ui-architecture.md`; UI RE reference: `docs/vtmb/vtmb-ui.md`.

### 8.4a `prop_dynamic` divergences — remaining halves

The animation half is done. Open: (f) **`solid` is unread** — 699 of 903 shipped `prop_dynamic`
placements build static collision from the model's `.phy` in retail and are walk-through here;
(g) **`disableshadows` is unread** — 849 props cast shadows the author switched off.
→ `docs/vtmb/entity_io.md`, `docs/vtmb/phy_vphysics.md`, `docs/vtmb/entity_visuals.md`.

### 8.6 UI foundation — remaining

The unified player UI, main menu and navigation slice are landed. **Remaining:** live
mouse/keyboard, Xbox-style and DualSense navigation acceptance across the screen set,
including hot device switching, focus loss and reconnect; the New Game click path untested end
to end (the seam is wired; the console equivalent works). **Open risk:** `uv run elysium debug
shots` cannot see the UI layer, so 8.9's HUD needs UI-inclusive vantages or its regressions go
unwatched. *Acceptance:* main and pause menus legible and correctly proportioned at 1080p,
1440p, 4K and 21:9; New Game enters `sp_tutorial_1` through the seam.

### 8.8 Sign / popup panels — remaining

The Canvas path is retired and the CommonUI sign screen landed. **Remaining:** the sign format
and presentation features — the `TrackerScheme.res` font roles mapped onto the type ramp,
`Label` justification vs `TextBlock` word-wrap, `TextRGBA`/`BackgroundRGBA`, `Tiled`
backgrounds, `Image` sub-blocks, the `NewspaperData` root (30 files) and its `Columns`, panel
keys `CloseOnLeftClick`/`MinShowTime`/`ClientCommand` — plus physical-device/resolution
acceptance. The authored layout is honoured as proportion and grouping and re-set with vector
type; per-resolution `Font_640`…`Font_1600` overrides are dropped. *Deps:* 8.6, 4.10.

### 8.9 HUD on the UI foundation — remaining

The stable HUD model, reticle/use icons, health, vitae droplets, Humanity/Masquerade, fade,
cutscene suppression, contrast treatments, and queued item/quest notifications are landed.
**Remaining:** equipment, disciplines and inventory selectors need authoritative gameplay data and
command wiring; the PP4 stealth readout slot needs a generation-checked observer handle, distance
and committed detection state without running perception in presentation; subtitles remain open.
The HUD reads `FElysiumViewState` and discrete notifications from the same publisher only. PP4 owns
the snapshot consumer and cleared/absent state; 13.1 supplies the authoritative gameplay snapshot.
Faithful observability: `docs/vtmb/stealth.md`. *Deps:* 8.6, 4.4, 11.8.

### 8.10 Accessibility & options backing

Additive only — changes what the player can configure and perceive, never what the game does.
Full key/button remapping + gamepad navigation across the 8.6 component set; UI text scaling;
subtitle size/background; colourblind-safe status colours + high contrast; FOV control; real
graphics/audio options screens backing settings the engine already exposes. The remapping
screen drives `UElysiumInputUserSettings` (`QueryMapKeyInActiveContextSet` for conflicts,
`MapPlayerKey` per slot, `ResetAllPlayerKeysInRow`) over three columns, filtering against
`FElysiumReservedKeys` (`docs/architecture/input-architecture.md`). Landed partial: current
surfaces have keyboard/gamepad focus navigation and programmatic labels; everything else open;
physical-device acceptance unrecorded. *Deps:* 8.6, 10.6.
