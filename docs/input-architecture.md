# Input architecture (Unreal)

The design of Elysium's input path: how VtMB's bind model is reproduced on Enhanced Input, how
the player remaps keys and buttons, how gamepads are supported first-party, and how the
development layer is kept structurally incapable of colliding with a player binding.

Engine-neutral VtMB facts — the bindable-command inventory, the key-name tables, the default
bind sets, the options-dialog layout, the joystick cvars — live in **`controls.md`**, which owns
the binding layer as it exists in the original. The tuning numbers behind look and movement live
in `source_movement.md`. Build status and task breakdown live in **`roadmap.md` 10.6**.

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

VtMB has no action abstraction — an action *is* a console command string, and `kb_act.lst` is the
whitelist of which strings the options UI may bind (`controls.md` § "What is bindable"). That maps
onto Enhanced Input directly, and the mapping is what keeps ~64 actions tractable.

**The strings already exist.** `FElysiumCommands` (roadmap 11.6, `runtime-architecture.md` §8.2)
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
**generated by `tools/build_content.py`** from a committed plain-text table.

- Source of truth: `Config/ElysiumInputActions.csv` — `Id, Command, Label, Group, ValueType,
  Pair, DefaultPrimary, DefaultAlt, DefaultPad`.
- The table is **hand-authored** against the inventory documented in `controls.md`, *not*
  generated from the user's `kb_act.lst`. That file is game-derived and cannot be committed;
  bring-your-own-game requires the shipped action list to stand alone.
- `build_content.py` emits `Content/Input/Actions/IA_*.uasset` and `Content/Input/IMC_*.uasset`,
  so `content.bat` keeps assets and table in lockstep and the CSV is the spec.

## Mapping contexts are the client modes

Contexts replace VtMB's `CClientMode*` split. `FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease`
(default **true**) means a key held across a context swap does not bleed into the new context until
it is physically released — which is the definitive answer to `controls.md` § "Open: what
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

Two constraints that belong in the plan, not in a later surprise:

- **`GameInputRedist.msi` ships with the game** (Windows 10 19H1 minimum) — part of 10.5's
  packaging story.
- **DualSense adaptive triggers and haptics are out of scope.** The seam when they are wanted is
  the plugin's `GameInputHapticAudioDevice` / `GameInputHapticEndpointFactory`, which reuses
  WinDualShock's `UEndpointSubmix` content model.

**`WinDualShock` is not an option.** Its `Build.cs` reflects on `LibScePad` and compiles to
`DUALSHOCK4_SUPPORT=0` without the licensed Sony platform extension.

Gamepad defaults are new work with no original to reproduce (`controls.md` records that VtMB ships
raw joystick cvars, no UI, no default binds, and a `joystick.cfg` that does not exist). They sit on
the Feel axis: A/B-able, one delta at a time by owner call.

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
`FElysiumConsole`**, reproducing VtMB's 0.066°/count (`source_movement.md`). The options slider
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
every `ApplySettings`, `FElysiumConfigWriter` emits Valve-format text to `tools/out/cfg/config.cfg`
— `unbindall`, then `bind "<KEY>" "<command>"` per mapping in slot order, then the archived cvars —
mirroring `Host_WriteConfiguration`. The CPython VM's `nt.getcwd` redirect (9.3b,
`python_bridge.md`) is what makes `vamputil.py`'s `FixKeyBindings` resolve that file. The projection
needs the `FKey` ↔ VtMB-keyname table from `controls.md` § "Key names and keynums"
(`EKeys::LeftMouseButton` ↔ `MOUSE1`, …), which is one static map.

The direction is **one-way** (profile → text). On first run only, an existing `config.cfg` with no
profile beside it is imported as the initial profile.

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
  `elysium.togglesky`, because `v` is `+movedown` and `t` is `toggleuiside` in VtMB's default set
  (`decisions.md` 2026-07-27). A dev verb is an `elysium.*` engine command and never enters the VtMB
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

An action added to the CSV with `DefaultPrimary=F1` therefore fails `test.bat` rather than silently
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
  (`debug-tooling.md` Layer 3).
- **Live inspection**: the `PlayerInputDebugger` plugin (`showdebug enhancedinput`) plus a Cog
  **Input** window — active contexts, per-action trigger state, current `FInputDeviceScope`, live
  modifier output — under the F1-first rule every other capability follows.

## Open

**CommonUI / CommonInput adoption.** Gamepad *UI* navigation — focus, back-button routing, and
Xbox/PlayStation glyph swapping — is CommonUI's remit, and `CommonInputBaseControllerData` is what
turns the `"DualSense"` hardware id above into the right button art. It is Epic's first-party answer
and Lyra's, and it is also opinionated (activatable widget stacks, input action domains): it shapes
the UI foundation rather than bolting onto it, so adopting it after 8.6 lands costs materially more
than adopting it with 8.6. The call belongs to **8.6**, not to 10.6.
