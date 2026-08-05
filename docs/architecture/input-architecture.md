# Input architecture (Unreal)

The design of Elysium's input path: how VtMB's bind model is reproduced on Enhanced Input, how
the player remaps keys and buttons, how gamepads are supported first-party, and how the
development layer is kept structurally incapable of colliding with a player binding.

Engine-neutral VtMB facts — the bindable-command inventory, the key-name tables, the default
bind sets, the options-dialog layout, the joystick cvars — live in **`docs/vtmb/controls.md`**, which owns
the binding layer as it exists in the original. The tuning numbers behind look and movement live
in `docs/vtmb/source_movement.md`. Build status and task breakdown live in **`docs/project/roadmap.md` 10.6**.

## Four planes

Input arrives on four separate channels that never share a key.

| Plane | Owner | Mechanism | Ships |
|---|---|---|---|
| **0 — System** | engine | `ConsoleKeys` (viewport client), `bF11TogglesFullscreen`, Alt+Enter | yes |
| **1 — Dev/debug** | Cog + `elysium.*` | Slate `SCogImguiInputCatcherWidget`, `UPlayerInput::DebugExecBindings`, `UEnhancedInputComponent::BindDebugKey` | no (`!UE_BUILD_SHIPPING`) |
| **2 — Player** | Enhanced Input | `UInputAction` + `UInputMappingContext` + `UEnhancedInputUserSettings` | yes |
| **3 — Command bus** | `FElysiumCommands` + `FElysiumConsole` | registered command → alias expand → cvar set → Python fallthrough | yes |

Plane 1 matters as much as plane 2: `BindDebugKey` takes an `FInputChord` directly and never
enters a mapping context, so a debug key is not an action, cannot appear in the remapping screen,
and cannot be bound by a player. The separation is the engine's own, not a convention.

## The action model

VtMB's command-string binding model and bindable whitelist are `docs/vtmb/controls.md`. The Unreal path
preserves that identity while mapping it onto Enhanced Input.

**The strings already exist.** `FElysiumCommands` (roadmap 11.6, `docs/architecture/runtime-architecture.md` §8.2)
declares the whole inventory by name with its `+`/`-` pairs, and `ElysiumBinds::Defaults()` carries
VtMB's own default bind set as `FKey` -> console line. Enhanced Input replaces the **front** of that
path — where a key comes from — and nothing behind it: an action still resolves to a command string,
the string still goes through the bus, and `FElysiumUserCmd` is still what movement and the camera
read. The CSV below is therefore a *projection* of the same inventory, not a second source of truth
for what a verb is.

**One `UInputAction` per bindable command.** Each carries a `UPlayerMappableKeySettings` whose
`Name` is a **stable id** (`Move_Forward`, `Hotkey_1`, `Discipline_Last`). That FName is the save
key in the player's key profile and never changes once shipped. The VtMB command string
(`+forward`, `vhotkey #1`, `vdiscipline_last`) rides beside it in the action table — `#` and
spaces make a poor persistence key.

Two tiers:

- **Analog / first-class** — `IA_Move` (Axis2D), `IA_Look` (Axis2D), `IA_MoveVertical`,
  `IA_CameraDolly`. Bound natively in C++ to `UElysiumInputRouter`, which folds them into the
  frame's `FElysiumUserCmd`. They carry real axis values and
  per-device modifier stacks; routing them through a string bus would discard both.
- **Command actions** — everything else, bound generically off the action table:

  ```cpp
  for (const FElysiumActionDef& Def : ActionTable)
  {
      Input->BindAction(Def.Action, ETriggerEvent::Started,   this,
                        &UElysiumInputRouter::OnCommandDown, Def.Command);
      if (Def.bIsButtonPair)   // VtMB's +cmd / -cmd
          Input->BindAction(Def.Action, ETriggerEvent::Completed, this,
                            &UElysiumInputRouter::OnCommandUp, Def.Command);
  }
  ```

  `OnCommandDown` executes `+cmd` (or the bare command) through `FElysiumConsole::Execute`.
  `UEnhancedInputComponent::BindAction`'s variadic `VarTypes...` payload carries the command name,
  so the whole inventory is one loop.

### Triggers, and the `Canceled` hazard

Modifiers shape a value; **triggers decide when an action fires**. The gamepad layout needs four:

| Trigger | Used for |
|---|---|
| `UInputTriggerPressed` + `ActuationThreshold` | an analog `Gamepad_*TriggerAxis` acting as a button |
| `UInputTriggerTap` | the short press of a dual-purpose button (`LB` tap = cast) |
| `UInputTriggerHold` | the long press of the same button (`LB` hold = the radial) |
| `UInputTriggerChordAction` | any modifier layer |

**A Hold or Tap trigger breaks the `+`/`-` pairing unless `Canceled` is bound.** The loop above
binds `Started` → `+cmd` and `Completed` → `-cmd`. A `Hold` released before its threshold fires
**`ETriggerEvent::Canceled`, not `Completed`** — the `-cmd` never runs, and the button stays
latched in `FElysiumUserCmd` for the rest of the session. Every `bIsButtonPair` action therefore
binds `Canceled` to the same `OnCommandUp` handler as `Completed`, and the pairing test covers the
cancelled path as well as the released one.

**Command actions route through the console because the patch's vocabulary is aliases.** `f` →
`vm_feed` → `checkFeed()` in Python. An action bound to a compiled verb and one bound to a user
alias must be indistinguishable, as they are in VtMB. It also means `-ExecCmds`, the MCP tools and
a level script can fire any player action by name with no extra seam.

**Keyboard movement keeps its VtMB identity.** `IA_Move` is one action, but its four WASD mappings
each set `SettingBehavior = OverrideSettings` with their own `UPlayerMappableKeySettings`
(`Move_Forward` / `Move_Back` / `Strafe_Left` / `Strafe_Right`). The remapping screen shows four
rows the way `kb_act.lst` does; the runtime sees one Axis2D, and the gamepad stick maps to the same
action through a different modifier stack.

**Slots carry the primary/alternate contract.** `EPlayerMappableKeySlot` offers seven; three are
used:

| Slot | Options-screen column | VtMB equivalent |
|---|---|---|
| `First` | Key/Button | `kb_act.lst` col 2 |
| `Second` | Alternate | `kb_act.lst` col 3 |
| `Third` | Gamepad | *(new — no original)* |

## Where the assets come from

`UInputAction` and `UInputMappingContext` are `.uasset`s. They are hand-authored and
game-agnostic, so they live in `Content/` under the same rule as the master materials:
**generated by `pipeline/unreal/build_content.py`** from a committed plain-text table.

- Source of truth: `Config/ElysiumInputActions.csv` — `Id, Command, Label, Group, ValueType,
  Pair, DefaultPrimary, DefaultAlt, DefaultPad`.
- The table is **hand-authored** against the inventory documented in `docs/vtmb/controls.md`, *not*
  generated from the user's `kb_act.lst`. That file is game-derived and cannot be committed;
  bring-your-own-game requires the shipped action list to stand alone.
- `build_content.py` emits `Content/Input/Actions/IA_*.uasset` and `Content/Input/IMC_*.uasset`,
  so `uv run elysium export bundle policy` keeps assets and table in lockstep and the CSV is the spec.

## Mapping contexts are the client modes

Contexts replace VtMB's `CClientMode*` split. `FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease`
(default **true**) means a key held across a context swap does not bleed into the new context until
it is physically released — which is the definitive answer to `docs/vtmb/controls.md` § "Open: what
conversation does to held input" on our side of the port.

| Context | Priority | Applied when |
|---|---|---|
| `IMC_Player_KBM` | 0 | in world |
| `IMC_Player_Gamepad` | 0 | in world (always — a pad key cannot collide with a keyboard key) |
| `IMC_Dialogue` | 10 | conversation open — advance / choose / history / skip, movement removed |
| `IMC_Menu` | 20 | pause, options, chargen |
| `IMC_Cinematic` | 30 | `scripted_sequence`, scripted cameras |

Both device contexts stay applied together so the remapping screen's Keyboard and Gamepad columns
are independent and each device carries its own modifier stack.

**Cursor visibility is device-dependent, and a scope alone cannot decide it.**
`FElysiumInputScope::bShowCursor` is a fixed value per scope, which is right for a mouse and wrong
for a pad: a `GameAndUI` screen showing a cursor leaves a gamepad player holding a pointer they
cannot move, over a screen with nothing focused. The scope's request is therefore filtered by the
live device — `UCommonInputSubsystem::GetCurrentInputType`, with `OnInputMethodChanged`
re-resolving the stack when the player switches device mid-screen. What a pad drives is **focus**,
not the cursor, so every activatable screen names a default focus widget.

## Gamepad

**`GameInputWindows`** (engine plugin, beta, `EnabledByDefault: false`) is the device layer — one
interface replacing XInput and RawInput. Xbox pads arrive as `GameInputFamilyXboxOne` with correct
`Gamepad_*` keys and need no configuration.

PlayStation pads arrive as `GameInputFamilyHid`, so each needs an `FGameInputDeviceConfiguration`
in `Config/DefaultGameInput.ini`:

- `DeviceIdentifier` — VID `054C`, PID `05C4`/`09CC` (DualShock 4 v1/v2), `0CE6`/`0DF2`
  (DualSense / DualSense Edge)
- `ControllerButtonMappingData` / `ControllerAxisMappingData` → standard `Gamepad_*` `FName`s, so
  **one IMC serves every pad** and nothing downstream distinguishes them
- `bOverrideHardwareDeviceIdString` + `OverriddenHardwareDeviceId = "DualSense"` — the string
  `FInputDeviceScope` publishes, which is what button-glyph swapping keys off

Two constraints:

- **`GameInputRedist.msi` ships with the game** (Windows 10 19H1 minimum) — part of 10.5's
  packaging story.
- **DualSense adaptive triggers and haptics are out of scope.** The seam when they are wanted is
  the plugin's `GameInputHapticAudioDevice` / `GameInputHapticEndpointFactory`, which reuses
  WinDualShock's `UEndpointSubmix` content model.

**`WinDualShock` is not an option.** Its `Build.cs` reflects on `LibScePad` and compiles to
`DUALSHOCK4_SUPPORT=0` without the licensed Sony platform extension.

Gamepad defaults are new work with no original to reproduce (`docs/vtmb/controls.md` records that VtMB ships
raw joystick cvars, no UI, no default binds, and a `joystick.cfg` that does not exist). They sit on
the Feel axis: A/B-able, one delta at a time by owner call.

### The layout

VtMB's ~40 player verbs do not fit 16 digital inputs. The layout follows one allocation rule:
**anything needed while the right stick is moving lives on a shoulder, a trigger or a stick click,
never on a face button** — that is attack, block, `+use` and the discipline cast.

| Input | Action | Note |
|---|---|---|
| `LS` | `IA_Move` | magnitude carries the walk↔run gait, so `+speed` needs no button |
| `LS` click | `autospeed` | the patch's own walk/run toggle alias |
| `RS` | `IA_Look` | in third person this *is* the orbit, so the `cam_*` verbs need no binds |
| `RS` click | `togglecamera` | |
| `RT` | `+attack` | `Pressed` with an actuation threshold |
| `LT` | **block** (melee/unarmed) · **zoom** (ranged) | contextual by weapon class |
| `LB` | tap → `vdiscipline_last` · hold → the quickbar radial | the two-stage cast as one button |
| `RB` | `+use` | |
| `A` / `B` / `X` / `Y` | `+jump` / `+duck` / `+reload` / `+feed` | |
| D-pad ← / → | `invprev` / `invnext` | |
| D-pad ↑ / ↓ | `+wpn_secondaryatk` / `holster` | |
| `Start` | `cancelselect` | |
| `Back` | the character screen | the quest log is a tab on it |

Everything absent from that table resolves to a **context** rather than a binding: `slot1`–`slot6`,
`lastinv` and `dropitem` are operations inside the character screen (`IMC_Menu`); `skip` is any
face button under `IMC_Cinematic`; `save quick` / `load quick` are pause-menu items; the dialogue
verbs belong to `IMC_Dialogue`.

**`LT` is contextual because the game already is.** `camera_prefs` forces third person for weapon
class 1 and first person for classes 2 and 4 (`docs/vtmb/camera-view-modes.md`), so branching a
binding on weapon class uses the original's own arbitration rather than inventing a mode; unarmed
has no zoom, so the two meanings never collide. It requires the weapon-class bitmask to be
readable by the input layer, which is a seam the substrate does not expose today.

**The radial subsumes three things.** It *is* `showhotkeys` — VtMB's own `VHotkeysUI`, so the
surface is not an invention; it removes the need for `toggleuiside`, because the D-pad cycles
weapons and the radial owns powers, leaving the wheel no mode to switch; and it fires selection
and cast as one action instead of reproducing the one-frame `vhotkey` deferral.

Three **divergences**, all additive, each pending an explicit owner call. The faithful behaviour
is `docs/vtmb/controls.md`:

- **`+duck` is a toggle on gamepad**, not a hold. Crouch is VtMB's stealth mode and Obfuscate
  breaks on moving while standing, so it is held for minutes at a time and a hold binding is not
  playable on a pad. Keyboard keeps the hold.
- **`toggleuiside` is unbound on gamepad** — it has nothing left to switch.
- **The one-frame `vhotkey` deferral is not reproduced.** It is the defect the community's
  `wait 1` idiom exists to work around, not a behaviour worth carrying.

Two entries are **blocked on RE rather than on design**: `LT`'s melee half needs the block verb
identified, and D-pad ↑ needs `+wpn_secondaryatk`'s semantics. Both are open questions in
`docs/vtmb/controls.md`.

### Melee combos need the stick quantised

`+attack` selects one of four melee moves from **the movement direction held with it**, and VtMB
reads that from discrete direction keys. An analog stick has to be quantised to the same four
directions with a **combat deadzone of its own** — the locomotion deadzone is tuned for a smooth
gait and is far too permissive to select a combo reliably. `IA_Move` therefore delivers its raw
vector into `FElysiumUserCmd` and the quantisation happens where the move set is resolved, so a
keyboard and a pad present the same four inputs to the same code.

### Modifier stacks

| Mapping | Stack |
|---|---|
| Mouse → `IA_Look` | `UElysiumMouseSensitivity` → `Negate` on Y when `m_pitch` < 0. **No `ScaleByDeltaTime`** — mouse input is already a delta |
| Stick → `IA_Look` | `DeadZone` (radial) → `ResponseCurveExponential` → `Scalar` → **`ScaleByDeltaTime`** → `FOVScaling` |
| Stick → `IA_Move` | `DeadZone` (radial) |
| WASD → `IA_Move` | `Negate` + `SwizzleAxis` per key |

Frame-rate-scaling mouse look is the classic failure of this system; per-device contexts make the
two stacks physically separate, so it cannot be applied to both.

`UElysiumMouseSensitivity` reads `sensitivity`, `m_pitch`, `m_yaw` and `m_filter` **from
`FElysiumConsole`**, reproducing VtMB's 0.066°/count (`docs/vtmb/source_movement.md`). The options slider
writes the cvar and the cvar drives the modifier — one settings truth, and it is the one VtMB
already had.

## Remapping and persistence

`UElysiumInputUserSettings : UEnhancedInputUserSettings` holds the key profile plus sensitivity,
invert-Y, `m_filter` and auto-aim (`sv_aim`, default off), and is where 8.10's accessibility
settings land. `bEnableUserSettings` is on by engine default.

Rebind flow: `QueryMapKeyInActiveContextSet` for conflict detection → confirm or clear the loser →
`MapPlayerKey(FMapPlayerKeyArgs{MappingName, Slot, NewKey})` → `ApplySettings()` →
`AsyncSaveSettings()`. "Use Defaults" is `ResetAllPlayerKeysInRow` per profile.

**`config.cfg` is a projection, not the model.** The user settings profile is authoritative. On
every `ApplySettings`, `FElysiumConfigWriter` emits Valve-format text to `$ELYSIUM_EXPORT_ROOT/cfg/config.cfg`
— `unbindall`, then `bind "<KEY>" "<command>"` per mapping in slot order, then the archived cvars —
mirroring `Host_WriteConfiguration`. The CPython VM's `nt.getcwd` redirect (9.3b,
`docs/vtmb/python_bridge.md`) is what makes `vamputil.py`'s `FixKeyBindings` resolve that file. The projection
needs the `FKey` ↔ VtMB-keyname table from `docs/vtmb/controls.md` § "Key names and keynums"
(`EKeys::LeftMouseButton` ↔ `MOUSE1`, …), which is one static map.

**Slot `Third` is excluded from the projection.** A gamepad binding has no VtMB keyname —
`JOY1`–`JOY4` and `AUX1`–`AUX32` exist in the engine's table but map onto nothing a modern pad
reports — so emitting gamepad rows would produce a `config.cfg` VtMB itself could never write,
which is precisely the file `FixKeyBindings` then parses. Only `First` and `Second` are written.

**The projection is write-mostly, not write-only: a runtime `bind` must reach the profile.** The
patch does not merely read `config.cfg` — `FixKeyBindings` reads it and then issues
`bind <KEY> "vm_discipline"` through the console (`docs/vtmb/controls.md` § "Bindings are rewritten
at runtime, from Python"). A `bind` executed by the running game therefore resolves to
`MapPlayerKey` on the profile rather than being discarded, or the patch's re-routing of discipline
and feed silently does nothing the moment a player moves either off its default key. `unbindall`
and `exec` follow the same rule. Everything else stays one-way (profile → text), and on first run
only, an existing `config.cfg` with no profile beside it is imported as the initial profile.

## Reserved keys — the dev/player guarantee

The development layer occupies no bare key a player can bind.

- The console is `` ` `` **and `F7`** (`ConsoleKeys` takes a list), and it is one console: UE cvars,
  `elysium.*`, VtMB aliases and Python fallthrough all arrive through the 9.3b bridge. `` ` ``
  matches VtMB's own `toggleconsole` bind; `F7` is the layout-independent second key, because
  `` ` `` is not on every physical keyboard — an ABNT2 puts `'`/`"` left of `1`, which UE resolves
  to `EKeys::Apostrophe`, a bind rather than the console. F7 is the one function key neither
  `ElysiumBinds::Defaults()` nor VtMB's `default.cfg` claims (F11 is UE's fullscreen toggle). These
  two are the only **bare** keys the dev layer holds, and `ElysiumBinds::ReservedKeys()` is that set.
- Cog's shell shortcuts are chords — `Ctrl+F1` toggle input, `Ctrl+F2`–`Ctrl+F4` layouts.
  `FCogInputChord` derives from `FInputChord`, so this is configuration.
- Elysium's own dev toggles are chords for the same reason: `Ctrl+V` runs `noclip` and `Ctrl+T` runs
  `elysium.togglesky`, because `v` is `+movedown` and `t` is `toggleuiside` in VtMB's default set. A dev verb is an `elysium.*` engine command and never enters the VtMB
  command bus — the two planes do not share a name.
- Every other dev key uses `UEnhancedInputComponent::BindDebugKey(FInputChord, IE_Pressed, …,
  bExecuteWhenPaused)` under `#if !UE_BUILD_SHIPPING`.

**`toggleconsole` and `cancelselect` are non-rebindable, which is what VtMB does** — neither appears
in `kb_act.lst` in retail or the patch, so reserving `` ` `` and `ESCAPE` costs the player nothing
and matches the original's contract. `F7` costs nothing either — `default.cfg` leaves it unbound.

**The guarantee is a test, not a convention.** `FElysiumReservedKeys` defines the set once, and
three things consume it:

1. the rebinding widget's key filter rejects them;
2. an automation test (Substrate tier, `-nullrhi`) walks every default mapping and asserts none —
   bare or chorded — lands on a reserved key. `Elysium.Substrate.Commands` does this today over
   `ElysiumBinds::Defaults()`; at 10.6 it walks the generated IMCs instead;
3. a dev-build startup check logs any collision between `DebugExecBindings` / Cog shortcuts and the
   live key profile. `UElysiumInputRouter::Setup` already refuses to install a default bind that
   lands on a reserved key, and says so.

An action added to the CSV with `DefaultPrimary=F1` therefore fails `uv run elysium test` rather than silently
shadowing the debug menu. `elysium.input.ReserveDebugKeys 0` (dev builds only) unlocks the set for
an A/B against retail muscle memory.

Cog's input mode also removes the player mapping contexts on enter and re-adds them on exit under
the default `bIgnoreAllPressedKeysUntilRelease`, so a held key cannot survive the transition. That is
the `Debug` scope's `Contexts` set doing nothing: contexts are declared on `FElysiumInputScope`
(11.5) and applied here, so the arbiter that already pushes and pops the scope is what adds and
removes the mapping.

## Verification

- **Automation** (Substrate tier): the reserved-key assertion; `+`/`-` pairing (two keys on one
  action stay Triggered while either is held and fire Completed only on the last release, matching
  VtMB's one-key-owns-the-press rule); the `config.cfg` writer round-trip; rebind → save → load →
  rebuild.
- **Injection**: `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` drives scripted input in
  tests, and backs an `elysium_input_*` MCP tool so an agent can drive the tutorial end to end
  (`docs/architecture/debug-tooling.md` Layer 3).
- **Live inspection**: the `PlayerInputDebugger` plugin (`showdebug enhancedinput`) plus a Cog
  **Input** window — active contexts, per-action trigger state, current `FInputDeviceScope`, live
  modifier output — under the F1-first rule every other capability follows.

## Open

**Button glyphs.** CommonUI and CommonInput are adopted: `UElysiumCommonUIInputData` supplies the
keyboard and generic-gamepad Accept/Back actions natively, and CommonUI owns focus and Back
routing while the input-scope stack stays the sole input-mode writer
(`docs/architecture/ui-architecture.md`). What is **not** authored is
`CommonInputBaseControllerData`, which turns the `"DualSense"` hardware id above into the right
button art; without it every pad draws generic glyphs.

**Aim assist and the look curve on a stick.** `sv_aim` defaults to off, but VtMB is a mouse game:
ranged combat on a stick with no assist, over a `+use` trace radius tuned for a mouse cursor, has
no original to reproduce and no measured baseline. A Feel-axis decision with no owner yet.

**The weapon-class seam.** `LT`'s melee/ranged split and the `camera_prefs` arbitration both read
the weapon-class bitmask, which the substrate does not currently expose to the input layer.
