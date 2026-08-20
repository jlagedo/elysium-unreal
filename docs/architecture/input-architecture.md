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

- **Analog / first-class** — `IA_Move` (Axis2D), `IA_Look` (Axis2D stick deflection),
  `IA_MouseLook` (Axis2D mouse displacement), `IA_MoveVertical`, `IA_CameraDolly`. Bound natively
  in C++ to `UElysiumInputRouter`, which folds them into the frame's `FElysiumUserCmd`. They carry real axis values and
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

`UInputAction` and `UInputMappingContext` are `.uasset`s. They are data-defined and
game-agnostic, so they live in `Content/` under the same rule as the master materials:
**generated by `pipeline/unreal/build_content.py`** from a committed plain-text table.

- Source of truth: `Config/ElysiumInputActions.csv` — `Id, Command, Label, Group, ValueType,
  Pair, DefaultPrimary, DefaultAlt, DefaultPad`.
- The table is **hand-authored** against the inventory documented in `docs/vtmb/controls.md`, *not*
  generated from the user's `kb_act.lst`. That file is game-derived and cannot be committed;
  bring-your-own-game requires the shipped action list to stand alone.
- `build_content.py` emits `Content/Input/Actions/IA_*.uasset`, `Content/Input/IMC_*.uasset`, and
  `DA_ElysiumInputActions`, the runtime projection that pairs each action asset with its command and
  press/release shape. `uv run elysium export bundle policy` keeps all three in lockstep with the CSV.

## Mapping contexts are the client modes

Enhanced Input contexts are gameplay contexts, not UI navigation modes. With no modal scope,
`IMC_Player_KBM` and `IMC_Player_Gamepad` are applied together so device switching is immediate and
the remapping screen's Keyboard and Gamepad columns remain independent. A UI-only screen scope
contains no gameplay contexts, so opening a menu, character screen, dialogue, computer terminal,
chargen prompt or sign removes both. CommonUI/CommonInput then owns arrows/WASD, D-pad/left stick,
Accept and Back;
there are no duplicate `IMC_Menu` or `IMC_Dialogue` bindings.

`FModifyContextOptions::bIgnoreAllPressedKeysUntilRelease` and the router's held-input clear make a
key held across a scope transition inert until release. Context removal is only the front door:
UI-only capture may stop controller sampling while the movement component still retains the last
published command. On every scope transition the router therefore clears its builder and, when the
resolved state has no player context, immediately replaces the body's retained command with neutral
intent. It also gates every sampled or replayed command before recording and publication. Sequence
identity and frame timing survive the gate; movement, look and buttons do not. This is the definitive
answer to `docs/vtmb/controls.md` § "Open: what conversation does to held input" on our side of the port.

Screen policy is centralized: sign 10, chargen 30, dialogue 40, terminal 42, character 45 and menu
50. Every interactive screen is UI-only with `Auto` cursor policy. Loading creates no interactive scope;
cinematic and debug claims remain separate owners. Scope priority controls input arbitration only:
simulation pause/hold remains with the existing time-control owners.

Player-camera actions — look, first person, third person, cycle view, recenter, shoulder swap and
inspect — are ordinary commands in the two player contexts. A winning dialogue, inspect, or
cinematic camera request publishes its desired control policy, but the request owner asks this
subsystem for the corresponding handle-based scope; the camera manager never calls `SetInputMode`,
adds a mapping context, or reads a key. Aim remains gameplay state inside the selected player view,
not a mapping-context swap. Full ownership and restoration rules:
`docs/architecture/camera-architecture.md`.

**Cursor visibility is device-dependent.** `FElysiumInputScope::CursorPolicy` is `Never`, `Auto` or
`Always`. Interactive screens use `Auto`: mouse/keyboard shows the cursor and gamepad hides it.
`UCommonInputSubsystem::OnInputMethodChangedNative` re-resolves the active scope when the player
switches device without changing the screen. What a pad drives is **focus**, not the cursor, and
every navigable screen names its selected action button as the desired focus target.

A computer terminal is a UI-only modal, not a gameplay mapping context. Its D-pad/left-stick
navigation and Accept/Back routing come from CommonUI; the screen presents semantic actions already
authorized by the terminal state and sends their canonical `hackcmd` strings through the same
world command path as typed input. Free text opens controller text entry only for values such as a
password. The complete no-secret-leak and device-hot-switch contract lives in
`docs/architecture/computer-terminal-architecture.md`.

## Gamepad

**`GameInputWindows`** (engine plugin, beta, `EnabledByDefault: false`) is the device layer — one
interface replacing XInput and RawInput. `DefaultInput.ini` selects `GameInput` as the preferred
Windows input API, so the XInput module does not create a duplicate Xbox path. Xbox pads arrive as
`GameInputFamilyXboxOne` with correct `Gamepad_*` keys and need no device configuration.

PlayStation pads arrive as `GameInputFamilyHid`, so each needs an `FGameInputDeviceConfiguration`
under `/Script/GameInputBase.GameInputDeveloperSettings` in `Config/DefaultInput.ini`; the
per-object `[GameInputPlatformSettings_Windows GameInputPlatformSettings]` section enables the
Gamepad and Controller processors while leaving GameInput keyboard, mouse, raw reports and sensors off:

- `DeviceIdentifier` — VID `054C`, PID `05C4`/`09CC` (DualShock 4 v1/v2), `0CE6`/`0DF2`
  (DualSense / DualSense Edge)
- the native Gamepad processor → the standard `Gamepad_*` buttons, sticks, triggers and D-pad, so
  **one IMC serves every pad** and nothing downstream distinguishes them
- `ControllerButtonMappingData` → only buttons absent from that standard projection; controller
  axes and switches stay disabled so two processors never publish the same stick or D-pad key
- `bOverrideHardwareDeviceIdString` + `OverriddenHardwareDeviceId = "DualSense"` — the string
  `FInputDeviceScope` publishes, which is what button-glyph swapping keys off

The first slice configures the standard DualSense only (`054C:0CE6`). GameInput 3 reports it with
both Gamepad and generic Controller capabilities; UE 5.8 installs a processor for each enabled
capability. The Gamepad processor owns the two sticks, two triggers, D-pad, face/shoulder buttons,
stick clicks and Options. The configured Controller processor has axes and switches disabled and
names only the separately reported Create, PS, touchpad click and Mute buttons. Touchpad click keeps
the standard `Gamepad_Special_Left`; Create, PS and Mute have distinct registered keys named
`Elysium_DualSense_Create`, `Elysium_DualSense_PS`, and `Elysium_DualSense_Mute`.

**Accept every control** means every stick, trigger, switch and button exposed by GameInput produces
an Unreal key/axis value, while each standard control has one publisher. Only LS, RS and Cross are
mapped to gameplay in this slice. The single-publisher rule is also a focus-safety rule: one
processor owns each analog state and clears it when the application is deactivated. Touch
coordinates, motion sensors and output features are not input controls in that contract.

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
the Feel axis: one delta at a time by owner call.

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

Everything absent from that table resolves to a **surface** rather than a gameplay binding:
`slot1`–`slot6`, `lastinv` and `dropitem` are semantic actions inside the CommonUI character
screen; `skip` is a cinematic action; `save quick` / `load quick` are pause-menu actions; and
dialogue responses are CommonUI actions. Those UI actions are not duplicated in Enhanced Input.

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

The input RE gate for `LT` and D-pad ↑ is cleared. Retail `+wpn_secondaryatk` is a held composite:
it asserts the dedicated block bit and forwards into ordinary `+attack2`; releasing it clears both.
The server accepts the block bit only while grounded with an eligible active weapon. The mapping
above is therefore a design choice about presenting one faithful composite through contextual
gamepad actions, not a guess about which retail verb blocks.

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
| Mouse2D → `IA_MouseLook` | `Smooth` |
| Stick → `IA_Look` | `Negate` Y, and nothing else |
| Stick → `IA_Move` | **empty** |
| L3 → `IA_Duck`, R3 → `IA_Camera` | empty; each fires a console line (`+duck`, `togglecamera`) |
| WASD → `IA_Move` | not a mapping either — each key fires a `+cmd` console line through the command bus |

The mouse and the stick use separate actions because their values mean different things. Mouse2D is
a displacement already made during this frame; `UInputModifierSmooth` regularizes uneven sample
buckets before `OnMouseLook` scales the counts and adds them to the command. `UInputModifierSmoothDelta`
is deliberately absent: in UE 5.8 it normalizes the direction of `NewValue - OldValue`, discarding
the mouse delta's magnitude. Legacy `bEnableMouseSmoothing` is off and Mouse2D's AxisConfig
sensitivity is 1.0, so the Enhanced Input modifier is the one normalization owner.

**The gamepad mapping carries device-frame corrections only; its feel lives at the command seam.**
`Gamepad_Right2D` delivers physical stick-up on negative Y while Elysium adds the action's Y
directly to Unreal pitch, where positive is look-up — so the Y-only `Negate` is a statement about
the device's frame, which is why it is the one modifier that belongs in the asset. The dead zone,
the saturation, the response curve, the filter and the sustained-turn ramp are all
`ElysiumInput::ShapeStickLook` / `ShapeStickMove` (`## Feel` → "Stick response").

That placement is forced rather than stylistic. The filter is a half-life and the ramp is a charge,
so both need the frame's **clamped, dilation-normalised** delta, and an Enhanced Input modifier only
ever sees Enhanced Input's raw one — `ScaleByDeltaTime` would hand the pad a level-load stall as a
full turn and break replay across frame rates. The dead zone, saturation and curve all key on the
deflection's **magnitude**, so they are one radial gesture, while Enhanced Input applies its stack
per component. And a value living half in a `.uasset` and half in a function has two owners, only
one of which can be asserted. The Content tier therefore asserts the **absence** of `DeadZone`,
`Scalar`, `ScaleByDeltaTime`, `FOVScaling`, `ResponseCurveExponential` and `Smooth` in both stick
mappings, as firmly as it asserts the one modifier that is there.

`Gamepad_Left2D` is `(right, up)` and `FElysiumUserCmd.Move` is `(forward, right)`; `Build` applies
the shaping in the stick's own frame **before** the swizzle, because an asymmetric zone applied
after it would rotate with the axes.

Frame-rate-scaling mouse look is the classic failure of this system. The stick is shaped into a held
*rate* and multiplied by the clamped, dilation-normalised delta inside
`FElysiumUserCmdBuilder::Build`; `IA_MouseLook` delivers smoothed counts as a finished displacement
and the router never multiplies them by frame time.

`ElysiumInput::FElysiumLookTuning` reads `sensitivity`, `m_pitch` and `m_yaw` **from
`FElysiumConsole`**. The options slider writes the cvar and the cvar drives the per-count scale — one
settings truth, retaining VtMB's names without constraining the modern mouse feel to its response.
`m_filter` is read by nothing: sample normalization is the `Smooth` modifier in `IMC_Player_KBM`.

## Feel

The tuning between a hand and the view. Three questions live here, and only one of them has a
recovered answer.

### Look response

The faithful path is a **linear scale**: `sensitivity` 3 × `m_yaw`/`m_pitch` 0.022 = 0.066 degrees
per mouse count, no smoothing (`m_filter` 0), pitch clamped to ±89, keyboard look at
`cl_yawspeed` 210 / `cl_pitchspeed` 225. Those numbers are recovered and owned by
`docs/vtmb/source_movement.md` § View / camera. **The code path is not recovered** — there is no
decompile of `client.dll`'s `CInput` mouse handling, so what is known is the ConVar surface and its
defaults, not the arithmetic between the device and the angle. Whether retail applies anything
beyond the multiply is an open RE question, not a settled fact.

The **shipped divergence, by explicit owner call**, is modern Enhanced Input mouse response rather
than preservation of VtMB's mouse feel. `Mouse2D → IA_MouseLook` carries `UInputModifierSmooth`, so
high-polling samples are normalized before they rotate the third-person view and rendered body.
Legacy `UPlayerInput` smoothing stays off; removing the mapping modifier is the direct A/B.

`ElysiumInput::ShapeMouseLook` remains an optional acceleration stage after the mapping,
`look_curve` defaulting to `0`, at which its gain is exactly `1.0`. Enabling that second, distinct
response term is an owner call not yet made. `FElysiumLookTuning::IsRetailLinear()` names whether
that optional curve is engaged, and the router logs a warning the first frame it goes false.

Three properties are structural rather than conventional:

- **The curve applies to the mouse contribution alone.** The turn keys and the stick are summed into
  `FElysiumUserCmd::LookDelta` as rates × delta *before* the shaping; a magnitude-keyed curve over
  the total would silently curve a held `+left` too. `Elysium.Substrate.UserCmd` asserts the
  separation directly, so moving the call after the merge turns a test red.
- **It shapes the whole 2D delta, never one axis.** Per-axis shaping would make a diagonal flick
  curve differently from its components.
- **It is one pure function, not an Enhanced Input modifier asset.** That is what lets it be asserted
  with no device and no world (`Elysium.Substrate.LookCurve`) and A/B-ed against the decompile when
  one exists. `IA_Look` carries no `ResponseCurveExponential` for the same reason — two owners of one
  feel, and only one of them assertable.

### Stick response

**A stick is not a mouse and cannot be shaped like one.** A mouse reports a *displacement the hand
already made*; IA_MouseLook's `Smooth` modifier only regularizes how its samples reach frames. A
stick reports a *held deflection* the game
integrates into a turn — and integrates the device's noise along with it.

**Gamepad feel has no original to reproduce** (§ Gamepad: VtMB ships raw joystick cvars, no UI, no
default binds and a `joystick.cfg` that does not exist), so nothing here is a divergence from
retail. It sits on the Feel axis outright.

The device was **measured rather than assumed**, with `elysium.LookProbe` — a dev verb that logs the
raw `Gamepad_Right2D` value beside the degrees that reached the view, counting down *deflected*
frames so the window survives the gap between arming it and a hand reaching the pad. On the
reference pad, at a stable 110 fps:

- the axes quantise to `1/127` — 8-bit sticks;
- the resting centre sits about **0.04** off zero, peaking near 0.05;
- a steady hold swings **±0.2 on Y** between consecutive frames while X holds to ±0.04;
- a hard diagonal reports a magnitude above 1, because the axes saturate independently.

`ElysiumInput::ShapeStickLook` answers each of those, and `ShapeStickMove` answers the movement
stick's smaller version of the same problem. Every term is one row of the `joy_*` console surface,
declared into the VtMB store like `look_curve` so a live run tunes with `elysium.cmd` and a
`config.cfg` keeps the answer:

| Term | Answers |
|---|---|
| `joy_deadzone` / `joy_move_deadzone` | the resting offset — a **scaled radial** zone, so the shaped value leaves zero continuously rather than stepping off a threshold |
| `joy_saturation` | the top of the travel, where noise rides a value that was going to clamp anyway |
| `joy_response_look` | the fine-control region the dead zone costs; squared by default |
| `joy_yawsensitivity` / `joy_pitchsensitivity` | the rate at full deflection, pitch slower — the shorter gesture, and the noisier axis |
| `joy_smoothing` | the frame-to-frame swing, as a **half-life** so it is exact at any step |
| `joy_accelscale` / `joy_acceltime` / `joy_accelenter` | a sustained-turn ramp; `1` is no ramp, and that is the shipped default |

Three properties are structural rather than conventional, and mirror the mouse curve's:

- **It shapes the whole 2D deflection, never one axis.** The dead zone, the saturation and the curve
  all key on magnitude, so a diagonal push shapes as one gesture. Per-axis shaping is what makes a
  stick feel square.
- **A centred stick is taken, not filtered toward.** The dead zone has already decided there is no
  input; letting the filter approach zero over its half-life coasts the view about ten degrees past
  where the thumb let go. Snapping costs nothing because the scaled band reaches zero continuously.
  The filter stays symmetric for every non-zero deflection — an attack/release split would bias a
  steady hold's mean downward, turning noise into a turn quietly slower than the stick.
- **The movement stick is deliberately curve-free, filter-free and ramp-free.** The mover already
  owns acceleration, so a second ramp would be two owners of one feel, and a curve would move the
  walk/run threshold away from where the stick says it is.

`Elysium.Substrate.StickLook` asserts all of it with no device and no world, including the
frame-rate property: the filter settles the same amount per second of real time at 60 and at 240 Hz.

### Leniency: there is none, and it is measured

VtMB has **no input buffer and no coyote time** (`docs/vtmb/source_movement.md` → "The jump is not
stock Source's"). Neither is implemented here, and adding either would be a Feel divergence rather
than a defect fix — so the decision is set up to be measured instead of argued.

The gym carries two five-rung brackets whose recordings are committed
(`docs/architecture/movement-architecture.md` → "The gym"). Each walks the body off a lip and places
**exactly one** jump press at a frame offset from a body event — never from the clock, because the
time to reach a lip moves with the gait and the offset does not:

| Bracket | Lanes | Press placed |
|---|---|---|
| coyote | `ledge_m1`, `ledge_0`, `ledge_p1`, `ledge_p2`, `ledge_p4` | K frames from the frame the ground is lost |
| buffer | `land_p1`, `land_0`, `land_m1`, `land_m2`, `land_m4` | K frames from the frame the ground returns |

The measurement is the run channel `jumps_taken`, which is 0 or 1 at any gait. As recorded, the
cliff is exactly one decision wide at both 60 and 120 Hz: `ledge_m1` and `ledge_0` jump and
`ledge_p1` does not; `land_p1` jumps and `land_0` does not. Adding coyote time moves
`ledge_p1`'s committed number; adding a buffer moves `land_m1`'s. Either becomes a red diff against
a before-picture rather than a matter of recollection.

## Remapping and persistence

`UElysiumInputUserSettings : UEnhancedInputUserSettings` holds the key profile plus sensitivity,
invert-Y and auto-aim (`sv_aim`, default off), and is where 8.10's accessibility settings land. `bEnableUserSettings` is on by engine default.

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

- **Automation** (Substrate tier): user-command analog/digital composition and stick-axis swizzle;
  gameplay-context arbitration; UI-only context removal; device-resolved cursor policy; CommonUI
  default focus, stable-id restoration, list wrap, dynamic-list repair, duplicate suppression and
  modal restoration; terminal typed/selected command equivalence; the reserved-key assertion;
  `+`/`-` pairing (two keys on one
  action stay Triggered while either is held and fire Completed only on the last release, matching
  VtMB's one-key-owns-the-press rule); the `config.cfg` writer round-trip; rebind → save → load →
  rebuild.
- **Generated content** (Content tier): the three action types and keys, exact modifier order and
  values, command-pair metadata, registered DualSense keys, and the `054C:0CE6` single-publisher
  device configuration.
- **Injection**: `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction` drives scripted input in
  tests, and backs an `elysium_input_*` MCP tool so an agent can drive the tutorial end to end
  (`docs/architecture/debug-tooling.md` Layer 3).
- **Live inspection**: the `PlayerInputDebugger` plugin (`showdebug enhancedinput`) plus a Cog
  **Input** window — active contexts, per-action trigger state, current `FInputDeviceScope`, live
  modifier output — under the F1-first rule every other capability follows.

## Open

**Button glyphs.** CommonUI and CommonInput are adopted: `UElysiumCommonUIInputData` supplies the
keyboard and generic-gamepad Accept/Back/Use actions natively, and CommonUI owns focus and Back
routing while the input-scope stack stays the sole input-mode writer
(`docs/architecture/ui-architecture.md`). What is **not** authored is
`CommonInputBaseControllerData`, which turns the `"DualSense"` hardware id above into the right
button art; the world-interaction prompt therefore shows the CommonInput icon when one resolves and
falls back to `E`/`RB` text when it does not.

**Combat aim assist.** `sv_aim` defaults to off and ranged-combat assist is not implemented. VtMB is
a mouse game, so ranged combat on a stick has no original pad baseline to reproduce; that remains a
Feel-axis decision with no owner yet. World `+use` selection is separate and owner-called: its
exact-first camera/body query and restrained fallback cone are specified in
`docs/vtmb/entity_io.md`. The look curve itself is no longer among the open questions — it exists,
defaults to retail's linear path, and is described in `## Feel`.

**Retail's mouse arithmetic is unrecovered.** `docs/vtmb/source_movement.md` owns the ConVar surface
and its defaults; nothing has decompiled `client.dll`'s `CInput` mouse handling, so "retail is
linear" is an inference from the cvar set rather than a read. `m_customaccel*` has never been
scanned for. The ConVar-enumeration technique that doc already describes would settle it cheaply,
and until it does, `## Feel`'s curve stays off by default.

**The weapon-class seam.** `LT`'s melee/ranged split and the `camera_prefs` arbitration both read
the weapon-class bitmask, which the substrate does not currently expose to the input layer.
