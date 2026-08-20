# Input plan — open-task specifications

Specification for the **open** remainder of 10.6. Status lives solely in
`docs/project/roadmap.md`; landed halves are deleted here as they land. No status marks in this
file. Design: `docs/architecture/input-architecture.md`; VtMB facts: `docs/vtmb/controls.md`.

### 10.6 Input path — Enhanced Input, remapping, first-party gamepad

Retire the legacy `DefaultInput.ini` axis/action block for the four-plane model: **Enhanced
Input is the driver, the VtMB console command string stays the action's identity.** One
`UInputAction` per bindable command from the `kb_act.lst` inventory,
`UPlayerMappableKeySettings.Name` a stable id, the command string executed through
`FElysiumConsole` on `Started`/`Completed` (so patch aliases bind exactly like compiled verbs).
`EPlayerMappableKeySlot` First/Second/Third = VtMB's Key/Alternate + Gamepad.

**Remaining sub-steps:**

- **b (rest).** `SetAnalogUp`, keyboard movement and the remaining keyboard/mouse binds onto
  the Enhanced Input path; every button-pair action binds `ETriggerEvent::Canceled` alongside
  `Completed` — a Hold/Tap released early otherwise leaves the button latched.
- **c. Reserved keys** — console on `` ` `` plus `F7`, Cog shell shortcuts on
  `Ctrl+F1`–`Ctrl+F4`, dev keys on `BindDebugKey`; enforced by a Substrate-tier test over every
  generated IMC, `elysium.input.ReserveDebugKeys 0` to A/B in dev builds.
- **e (rest).** Additional DS4/Edge `FGameInputDeviceConfiguration` and glyph entries; LB hold →
  quickbar radial; melee stick quantised to four directions
  on a combat deadzone; adaptive triggers/haptics deferred; `GameInputRedist.msi` joins 10.5's
  packaging story.
- **f. `UElysiumInputUserSettings` + `config.cfg` projection** — the key profile is
  authoritative; `FElysiumConfigWriter` emits Valve-format text so `vamputil.py`'s
  `FixKeyBindings` reads a faithful view (imported once on first run; slot Third excluded).
  The projection is not write-only: `FixKeyBindings` issues `bind <KEY> "vm_discipline"`, so a
  runtime `bind` must resolve to `MapPlayerKey` instead of being dropped, or the patch's
  re-routing of discipline and feed silently dies off default keys. Declare `execonsole`,
  `player_immobilize`, `player_mobilize`. Rebinding works headlessly before any UI exists.
- **g. Remapping screen** — lands with 8.10 on the 8.6 stack, not here.

**Acceptance:** the tutorial is playable start to finish on keyboard+mouse and on an Xbox *and*
a DualSense pad with no third-party driver; every action rebindable to
primary/alternate/gamepad and surviving a restart; the reserved-key test green. Defaults are
the Patch 11.5 set. *Deps:* 11.5, 11.6; 8.6/8.10 for the screen only. Physical Xbox acceptance
remains open.
