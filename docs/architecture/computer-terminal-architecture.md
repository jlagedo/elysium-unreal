# Computer-terminal architecture

How Elysium turns a `prop_hacking` computer into an exclusive, interactive terminal session in
Unreal. Recovered VtMB behavior and the still-open retail questions live in
`docs/vtmb/computer-terminals.md`; implementation status lives only in
`docs/project/roadmap.md` 13.4.

The terminal reproduces the original gameplay contract — content, authored character grid, command
meaning, passwords, skill checks, scripts, outputs, email state and one-user authority — through a
modern presentation. It does not reuse VtMB's terminal font, VGUI styling or 512×512 character
texture. This is the explicit Presentation-layer divergence allowed by
`docs/project/remaster-direction.md`: the rules remain faithful while their renderer is replaced.

## 1. Design in one page

A terminal session has one authority and three projections:

| Layer | Owner | Responsibility |
|---|---|---|
| Gameplay | `FElysiumTerminal` / `FElysiumPropHacking` in `FElysiumEntityWorld` | use eligibility, exclusive user, parsed content, terminal state machine, command execution, skill attempts, email state, scripts, queued outputs and persistence |
| Presentation | `UElysiumPresentationSubsystem` | immutable, revisioned terminal view plus intent methods; no command interpretation |
| Local player | `UElysiumPlayerUISubsystem`, `UElysiumTerminalScreen`, camera and input services | project the terminal onto the physical screen, edit a local line, expose controller actions, route intents and restore the previous camera/focus |

The world owns the session. The widget never parses `TerminalDefinition`, changes an entity, fires
an output or decides whether a password is valid. Keyboard text and a controller-selected action
both become the same canonical `hackcmd` request, so controller support is another input method, not
a second terminal implementation.

The active presentation is Slate drawn into a render target bound to the computer model's exact
`screen` material slot. It is not a full-screen fake terminal and not a `UWidgetComponent`: the
model's own UVs put the pixels on the glass, so the screen participates in world depth, occlusion,
lighting and the physical bezel. A transparent CommonUI screen still owns focus, typing and
semantic actions, but it paints no second terminal into viewport space. Closing input ownership
leaves the material installed and replaces the live surface with the authored terminal screensaver.

## 2. Evidence and fidelity boundary

The implementation consumes the contract in `docs/vtmb/computer-terminals.md` rather than inferring
behavior from presentation. In particular:

- only one player owns a terminal at a time;
- the default logical screen is 36 columns by 24 rows, with the authored `textcolumns`/`textrows`
  values remaining authoritative for an instance;
- normal Enter sends `hackcmd <line>`, Escape sends `hackcmd quit`, Ctrl+C sends
  `hackcmd break`, raw-character mode sends `hackcmd %c`, and acknowledgement sends an empty command;
- input flags, maximum input and directory-key rules constrain the local editor;
- a Function resolves dependency → runtext → enqueue `OnTriggerN` → synchronous `runscript` →
  prompt, with ordinary target delivery following through the shared event queue;
- email state remains terminal-local unless `global_email` promotes it to player state.

Open retail questions stay open rather than acquiring guessed behavior. TERM4 now supplies the
server built-ins and state dispatch, and TERM5 supplies the terminal-specific difficulty,
password, attempt-counter and deterministic bypass join needed by the tutorial slice. TERM2 still
gates exact generic use-output ordering and any forced cancellation outside the confirmed active
alignment body. TERM7 and TERM8 add email and screen-saver detail without changing this
architecture.

## 3. Gameplay authority and session lifetime

`FElysiumTerminal : FElysiumSkillEntity` owns the shared terminal session. `FElysiumPropHacking`
adds the `TerminalDefinition` content and `prop_hacking` leaf state. The split follows
`docs/architecture/gameplay-systems-architecture.md` §5.7 and keeps entity identity plain C++;
Unreal actors, widgets and cameras are optional bodies over the entity handle.

The live state is explicit:

```text
FElysiumTerminalSession
    Owner terminal handle
    User player handle
    Session serial
    Current directory / function / email cursor
    Logical 36x24 character grid and cursor
    Input mode, flags, maximum input and directory-key policy
    Pending prompt / acknowledgement state
    Bounded command buffer required by the authority
```

Directory, function and email state are semantic values, not widget selection indices. Every state
change increments the session serial or view revision. A request carries both owner and serial; the
world rejects stale input from a closed or superseded session.

Entry uses the existing `+use` focus and explicit-use-session path:

1. the interaction query selects the entity and the world revalidates availability;
2. the terminal claims the one current user and begins an `EElysiumUseSessionKind::Explicit`
   session;
3. the world publishes terminal presentation state;
4. the local-player owner acquires the terminal input scope and `Focus` camera request;
5. the CommonUI screen activates only after it has a valid terminal owner and screen projection.

Normal quit, generic use cancellation, loss of eligibility, entity destruction, player destruction,
map teardown and travel all converge on `EndPlayerUseSession`. End is idempotent: the entity clears
its user, presentation closes, the UI releases its input scope, and the camera handle restores the
previous view. A terminal session is transient and blocks saving while active; persistent terminal
and email state serializes only after the session ends. TERM2 supplies any additional faithful
damage/distance cancellation rule before that rule is implemented.

## 4. Content and command execution

The runtime reads patch-first files from `$ELYSIUM_EXPORT_ROOT/vdata/hackterminals/` through the
shared rulebook/KeyValues path. The parsed representation preserves the authored order and raw
strings needed for rendering, while normalizing references into typed directories, functions,
emails, dependencies, passwords and output indices.

The entity world exposes one non-bindable, content-facing command identity:

```text
hackcmd <terminal command line>
```

It is registered on the existing command bus but is executable only for the player who owns the
active terminal session. The command adapter forwards to the terminal entity; it does not duplicate
the grammar in the player controller. An internal `SubmitTerminalCommand(owner, serial, text)`
entry reaches the same handler for UI and automation.

The terminal state machine owns built-ins, navigation, password validation, hacking attempts,
function and email actions. A successful Function executes its recovered transaction through the
one deterministic entity queue. The UI receives only the resulting new view revision; it cannot
optimistically fire a function or output.

## 5. Presentation contract

`FElysiumTerminalView` is an immutable projection embedded in `FElysiumViewState` or published
beside it by the same world-scoped presentation owner:

```text
FElysiumTerminalView
    bOpen
    Owner
    SessionSerial
    Revision
    ScreenSaverLabel
    Rows[24]                 // each clipped/padded to 36 logical cells
    CursorRow / CursorColumn
    InputMode                // Line, RawCharacter, Acknowledge
    MaxInput
    bAcceptsDirectoryKeys
    Actions[]                // semantic controller choices, already authorized
```

`FElysiumTerminalAction` contains a stable semantic id, display label, canonical command string,
enabled state and optional explanation. It never contains a direct entity pointer or callback.
Stable ids preserve controller selection when a new revision changes labels or surrounding output.
The widget's partially edited line is local-player state keyed by owner and session serial. It is
not serialized or republished as terminal gameplay state, and it is cleared only when the command
is accepted, the authority changes input mode, or that session ends.

Publishing is revision-based rather than rebuilt every frame. The player UI reconciles one terminal
screen for the active owner, updates it in place, and discards out-of-order revisions or a close for
the wrong owner/serial. Presentation intents are narrowly named:

- `TerminalSubmitCommand(Owner, SessionSerial, Command)`;
- `TerminalSubmitCharacter(Owner, SessionSerial, Character)`;
- `TerminalAcknowledge(Owner, SessionSerial)`;
- `TerminalQuit(Owner, SessionSerial)`;
- `TerminalBreak(Owner, SessionSerial)`.

Each resolves the current entity session again before acting.

## 6. Putting the console on the computer screen

### 6.1 Offline screen metadata

The current terminal models expose their display as a separate material named `screen`. During
model export, the pipeline gathers the triangles assigned to that material and derives a
model-local planar surface:

```text
FElysiumTerminalScreenSpec
    Model key
    Local centre
    Local normal
    Local right / up
    Half width / half height
    Fit error and source material
```

The basis is Unreal-native, in centimetres, and is read verbatim at runtime. The exporter rejects a
non-planar or degenerate fit instead of inventing axes. Generated metadata remains under
`$ELYSIUM_EXPORT_ROOT` or `/ElysiumBaked`; it is game-derived and is never tracked. A project-authored
terminal may provide the same fields from an authored socket/sidecar in the authored-content
namespace.

Content validation requires exactly one usable screen surface and one exact material slot named
`screen` for every model referenced by a current `prop_hacking`. The material slot is the active
pixel projection seam; the recovered surface basis is camera-framing metadata. A missing seam fails
the content test. Runtime emits a warning and closes the attempted session instead of exposing a
blind input mode or silently placing UI over the whole monitor.

### 6.2 Camera framing

Opening the session acquires one handle from `UElysiumCameraService` using the `Focus`/Inspect
request class. The request targets the transformed screen centre, faces the camera down the screen
normal, and uses the surface's up vector as camera up. Distance is solved from the screen extents,
camera FOV and a small safe-frame margin; the player pawn is never translated or rotated.

The terminal request is fixed framing, not free-orbit prop inspect. A camera-channel sweep validates
the pose. If the target is destroyed, the screen turns away, projection becomes invalid or the
camera cannot establish the required view, the terminal session closes through the ordinary end
path. Releasing the handle restores the exact prior player view even when another modal request was
released out of order.

The surface remains visibility-tested while active. The console is already part of the model's
material pass, so ordinary world depth naturally occludes it. If the framing visibility test fails,
the session ends through the ordinary path instead of allowing input to a screen the player cannot
see.

The request leaves the passive HUD visible around the physical monitor, preserving the in-world
composition of the VtMB interaction. The terminal game-modal layer is transparent and contains
only the focusable input shell. Fades, loading and system-modal screens continue to outrank it.

### 6.3 World-screen render target

`AElysiumMapActor` retains the rendered component that supplied each entity's use anchor, separately
from any query-only box created for use tracing. The presentation seam resolves that physical
component by terminal owner. `UElysiumTerminalScreen` requires its exact `screen` material slot,
allocates a 1024×768 render target, and binds it through a dynamic instance of Unreal's opaque
Slate pass-through material. It never selects a slot by substring or guesses a new plane.

The live terminal surface and local draft are retained Slate and are redrawn into that target when
the authoritative view revision, local text or semantic selection changes. The model's existing UVs
map the target to the glass. Camera motion, dynamic resolution and DPI scaling therefore cannot
separate the pixels from the monitor, and no viewport-space depth approximation is required.

The terminal panel uses a project-licensed vector monospace face, the shared type library and new
terminal-specific color/spacing tokens. It preserves the authored cell grid (36×24 for the
tutorial) and strings, but
does not reproduce VtMB's bitmap glyphs, phosphor palette, VGUI chrome or cursor style. Cell metrics
come from the fixed render surface, and camera framing is accepted at 1920×1080, 2560×1440 and
3840×2160 with whole-grid clipping, readable text and no bezel overlap.

On close the same target is redrawn with `TerminalDefinition`'s authored `"screen saver"` label.
The physical component retains the material instance and target after CommonUI releases its input
scope, so the monitor does not snap back to a stock texture. A later session replaces that inactive
surface with a fresh live target. The screensaver has no focus, command or state-machine authority.

## 7. Keyboard and mouse

Line mode uses one focused `SEditableText`-backed editor whose visible text is mirrored into the
authored panel grid. Editing is local and immediate; Enter submits once and waits for the authoritative
revision. Maximum input is enforced both locally and by the terminal entity. The editor handles
selection, repeat, Backspace/Delete/Home/End and paste without turning arbitrary UI keys into
gameplay commands.

The recovered protocol overrides ordinary text editing where required:

- Escape requests `quit` and is consumed;
- Ctrl+C requests `break` and is consumed;
- raw-character mode forwards each accepted character immediately and never accumulates a line;
- acknowledgement mode maps Enter/Accept to the empty command;
- directory keys are accepted only when the current state enables them.

The mouse may place the text caret or choose a visible semantic action, but clicking an action
submits the same command as keyboard/controller. Input remains captured until the terminal session
ends, so clicks and held keys cannot leak into movement, attack or world use.

## 8. Gamepad and joystick interaction

The controller does not emulate a keyboard for routine terminal use. The terminal authority builds
an action palette from the same currently visible and executable state that produced the screen:

- directories and back-navigation become selectable navigation actions;
- visible functions and email rows become selectable actions;
- acknowledged prompts become one Continue action;
- Quit and any recovered built-in action are explicit actions;
- arbitrary text or a password opens a controller text-entry overlay only when no semantic action
  can represent the input.

D-pad or left stick changes selection, Accept submits the selected action's canonical command, and
Back closes a nested text-entry overlay first, then requests terminal quit. A secondary face button
opens free text; shoulder buttons change sections only where the recovered state exposes such a
section. CommonInput glyphs label these actions and hot-switching between mouse/keyboard and pad
preserves the local line, terminal revision and selected semantic id.

`UElysiumTerminalTextEntryOverlay` is a CommonUI alphanumeric grid for the remaining text cases; it
does not assume a desktop platform keyboard is available. D-pad/left stick selects a glyph, Accept
appends it, the secondary face button deletes, a Done action submits, and Back cancels to the action
palette without ending the terminal session. Its available glyphs and maximum length come from the
current terminal input contract. A platform virtual keyboard may replace the grid on a platform
that supplies one, but both return the same text intent.

The action palette is not generated by scraping displayed characters. It comes from the parsed
terminal state after dependency evaluation, so it cannot reveal a dependency-hidden function,
guess a command, leak a password or bypass a skill gate. A password-protected destination exposes
an `Enter password` action and controller keyboard, not the password. If recovered Hacking behavior
later authorizes a bypass, the authority publishes that bypass only after the corresponding skill
state says it is available.

Keyboard text, a clicked action, controller selection, automation injection and any future virtual
keyboard all resolve to the same command string and the same session-serial check. Tests compare
their resulting terminal state and queued effects, not just their labels.

## 9. Input and modal composition

`EElysiumUIScreenKind::Terminal` uses a UI-only `FElysiumInputScope` at priority 42: above Dialogue
(40), below Character (45) and Menu (50). CommonUI owns focus, navigation, Accept and Back;
`UElysiumInputSubsystem` remains the sole input-mode and Enhanced Input context writer. Activating
the terminal removes both player mapping contexts with pressed-key suppression and `Auto` cursor
policy. A pause menu can cover it without ending it; on menu close, CommonUI restores the terminal's
selected semantic action or editor focus.

The terminal screen is a game-modal child of the unified UI root. Loading/system-modal layers
outrank it, debug remains independently claimable, and terminal close removes only the matching
owner/serial. Dialogue or another explicit world-use session cannot start while the terminal owns
the player use session.

## 10. Diagnostics and acceptance

One terminal diagnostic reports owner, user, serial, parsed file, current node, view revision,
input mode/flags/limit, selected action id, screen-surface fit, material slot/render target, camera handle,
pending skill attempt, and the last accepted/rejected command. It reports content and presentation
state read-only; it never provides a second execution path.

Automated contracts cover:

- parser ordering, patch-first resolution, malformed references and dependency filtering;
- exclusive entry, stale serial rejection, every exit reason and idempotent teardown;
- line/raw/ack transport, maximum input, directory-key rules and command grammar recovered by TERM4;
- Function execution order and queued active-user provenance;
- password and skill-attempt policy recovered by TERM5;
- per-terminal/global email serialization when TERM7 closes;
- keyboard, click and controller-action equivalence at the authoritative state/queue boundary;
- screen-metadata planarity, exact `screen` slots, render-target projection and bezel clipping;
- CommonUI focus, device hot-switch, nested text entry, menu cover/restore and held-key suppression;
- camera target loss, failed framing, out-of-order modal release and exact previous-view restoration.

Played acceptance uses the real `sp_tutorial_1` `tuthack` entity and queue. On keyboard, the player
focuses the physical monitor, enters `Safe`, supplies `chopshop` or uses the Hacking bypass, then
types `Unlock`. On a gamepad, the player reaches those same authoritative transitions and the same
Unlock function through semantic selection without typing the commands. Both paths must enqueue
`OnTrigger0`, unlock/reveal the safe through the authored map wires, and return control to the exact
previous camera. This is tested from real input in the Play tier; a console-injected state is
diagnostic evidence only.

## 11. Delivery order and research gates

The implementation lands in slices without splitting authority:

1. apply the recovered TERM4/TERM5 tutorial command/password/skill contract, and close the TERM2
   entry/forced-cancel cases required by the presented use session;
2. add the headless `TerminalDefinition` parser, terminal state machine, `hackcmd` adapter and
   revisioned view with Substrate tests;
3. prove the `tuthack` Function transaction on the real entity queue;
4. project the keyboard CommonUI surface through the model's `screen` material, then export
   screen-surface metadata and add fixed camera framing;
5. add the semantic action palette, controller navigation and virtual text entry;
6. run the keyboard and gamepad tutorial acceptance at all three target resolutions.

TERM7 email completion and the remaining TERM8 moving-screensaver behavior extend the same
state/view contracts after the tutorial slice. Neither introduces another widget-owned parser,
command path, camera owner or persistence model.
