# 0016 input-pads — the tutorial is playable on keyboard, Xbox and DualSense, and every action rebinds

## Witness
`sp_tutorial_1` played start to finish on keyboard+mouse and on an Xbox *and* a DualSense pad
with no third-party driver; every action rebindable to primary / alternate / gamepad and
surviving a restart; the reserved keys held. Defaults are the Patch 11.5 set. Physical Xbox
acceptance remains open.

## Scope
The gameplay pad remainder of the input path (was 10.6): Enhanced Input is the driver, the VtMB
console command string stays the action's identity; one `UInputAction` per `kb_act.lst`
command; `UPlayerMappableKeySettings.Name` a stable id; the command executed through
`FElysiumConsole` on `Started` / `Completed`; `EPlayerMappableKeySlot` First / Second / Third =
Key / Alternate / Gamepad. Owned elsewhere: the remapping screen (10.6g) — the UI foundation
(8.10 / 8.6); `GameInputRedist.msi` — the packaging story (10.5).

## Sources
- Oracle: `docs/vtmb/controls.md`.
- Authored data: `kb_act.lst`, `config.cfg`, `vamputil.py`'s `FixKeyBindings`.

## Witness data
- Landed: the gameplay pad layout and generated Xbox/DualSense glyph switching.
- `FixKeyBindings` issues `bind <KEY> "vm_discipline"`, so a runtime `bind` must resolve through
  `MapPlayerKey` instead of being dropped, or the patch's discipline/feed re-routing silently
  dies off default keys; `execonsole`, `player_immobilize`, `player_mobilize` are declared.
- Every button-pair action binds `ETriggerEvent::Canceled` alongside `Completed`, or a Hold/Tap
  released early leaves the button latched.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The pad layout and glyphs** (was 10.6a/e, landed).
- [ ] **2. The keyboard and mouse remainder** (was 10.6b).
  Job: `SetAnalogUp`, keyboard movement and the remaining binds onto Enhanced Input; `Canceled`
  bound on every button-pair action.
  Size: S. Effort: Sonnet / medium.
- [ ] **3. Reserved keys** (was 10.6c).
  Job: console on `` ` `` plus `F7`, Cog shortcuts on `Ctrl+F1`–`Ctrl+F4`, dev keys on
  `BindDebugKey`; `elysium.input.ReserveDebugKeys 0` to A/B in dev builds.
  Size: XS. Effort: Sonnet / low.
- [ ] **4. DS4/Edge and the combat stick** (was 10.6e).
  Job: DS4/Edge `FGameInputDeviceConfiguration` and glyph entries; LB hold → quickbar radial;
  the melee stick quantised to four directions on a combat deadzone; adaptive triggers/haptics
  deferred.
  Size: S. Effort: Sonnet / medium.
- [ ] **5. User settings and `config.cfg`** (was 10.6f).
  Job: `UElysiumInputUserSettings` authoritative; `FElysiumConfigWriter` emits Valve-format text
  so `FixKeyBindings` reads a faithful view (imported once on first run, slot Third excluded);
  `bind` → `MapPlayerKey`; rebinding works headlessly before any UI exists.
  Oracle: `controls.md`.
  Size: M. Effort: Sonnet / high.

## Seams
- Provides: the pad play of every tutorial beat.
- Consumes: 11.5 / 11.6 (input defaults and the command registry, landed).
- Open: physical Xbox and DualSense acceptance.
