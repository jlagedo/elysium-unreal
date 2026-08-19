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

Open retail questions stay open rather than acquiring guessed behavior. TERM4 supplies the server
built-ins and state dispatch; TERM5 supplies the terminal-specific difficulty, password,
attempt-counter and deterministic bypass join; TERM1 supplies the use dispatcher, its 80-unit cosine
cone and the `0.7` attachment-driven screen-facing gate; TERM3 supplies icon eligibility and the
stock fallback icon; TERM6 supplies the four sound cue sites; TERM7 supplies the complete email
state machine; TERM8 supplies the screensaver. What remains open is the identity of the generic use
outputs around entry and exit, whether death, damage or teardown reach the dispatcher's release
branch, and the meaning of the two player mode transitions — none of which change this
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

Eligibility reproduces the recovered gate. The interaction query uses an 80-unit reach inside a
cosine cone, and the terminal's own focus predicate additionally requires the planar dot product
between the player view and the model's `screen_axis` to exceed `0.7`, both read from the placed
model's `screen` and `screen_axis` attachments. One predicate serves focus, availability and use-icon
eligibility, so a terminal shows the stock use icon exactly when it can be used.

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
and email state serializes only after the session ends.

Retail's only recovered forced exit is the use dispatcher's release branch; it carries no distance
rule because the session pins the player to the terminal (see §6.2). Death, damage and teardown
exits are defensive additions on the Unreal side rather than reproductions, and stay that way until
the corresponding native paths are recovered.

Leaving a password prompt is a session transition, not a command. Retail's router compares `quit`
against the pending password like any other text, and the prompt is escaped through the dispatcher's
exit path instead. Escape and the Quit action therefore end the session directly and never enter the
command router, in any input mode, including while a skill attempt is in flight.

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

### 4.1 Email state

Email is authority state, not a widget mode. Each terminal carries 128 per-message flags as a
bitmask of read and deleted, plus the mail-area unlock and attempt count. Opening a message is one
transaction: render the body, and if it was unread run its `runscript` through the shared script
seam and then mark it read, so the script runs exactly once and never on list render. A message
whose dependency fails, or which the player deleted, is absent from the visible list rather than
shown disabled. The mail area asks for a password only when `email_password` is authored non-empty
and the terminal is not already unlocked; unlocking persists as saved state. `autodelete` is
authored in shipped content and read by nothing, so it is not implemented. A `global_email` terminal
overwrites its local flags from the player record on entry and writes them back on exit, keyed by
entity name; without `global_email` the flags stay entity-local. The evidence is
`docs/vtmb/computer-terminals.md` §9.

### 4.2 Terminal audio

The four `soundgroup` cues are authority-side and fire at the recovered sites: `access` once on
entry, `accept` on entering a directory whether by name or by a successful password or bypass,
`error` on both a rejected command and every password-prompt render, and `typing` on the
authority's character echo and on each bypass-buffer refresh. The function executor itself is
silent. The per-keystroke click is separate and local to the typing player, matching the recovered
split rather than merging keystroke feedback into the shared group.

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

### 6.1 The screen surface

A terminal model carries its display twice: as a material slot named exactly `screen`, and as the
`screen` and `screen_axis` attachments that the recovered eligibility gate reads. Both survive the
export and the bake — the placed-model skeleton keeps the two bones and the mesh keeps the slot — so
the runtime derives the surface basis from the model it already has:

```text
Screen basis (runtime, model-local, centimetres)
    Centre      from the screen attachment
    Normal      from the screen_axis attachment
    Right / up  from the axis basis
    Half width / half height  from the screen material section's bounds
```

No offline sidecar and no separate export stage participate. The basis is the same data the
`0.7` facing gate consumes, so eligibility and camera framing can never disagree about where the
screen is. A project-authored terminal supplies the same two attachments and the same slot name
from the authored-content namespace.

Content validation requires exactly one exact material slot named `screen` and both attachments for
every model referenced by a live `prop_hacking`. The material slot is the pixel projection seam; the
attachments are the geometry. A missing seam fails the content test. At runtime a missing seam is a
logged error that ends the attempted session, never a blind input mode and never UI placed over the
whole monitor — and the error names the entity, the model and which of the three parts is absent, so
the failure is distinguishable from an unavailable terminal.

### 6.2 Camera framing

Opening the session acquires one handle from `UElysiumCameraService` using the `Focus`/Inspect
request class. The request targets the transformed screen centre, faces the camera down the screen
normal, and uses the surface's up vector as camera up. Distance is solved from the screen extents,
camera FOV and a safe-frame margin chosen so the monitor's bezel and part of the physical prop stay
in frame — the console is read on an object in the world, not on a full-bleed panel.

The faithful behavior is different and is recorded here beside the divergence. Retail's active-use
maintenance body writes the player's origin and relinks the player at the terminal's aligned use
position on every tick, so a VtMB terminal session physically moves the pawn to the computer and
holds it there; that pinning is also why retail needs no distance-cancellation rule
(`docs/vtmb/computer-terminals.md` §6). This architecture instead leaves the pawn where the player
left it and moves only the camera. **The divergence is not yet adjudicated**: it stands as the
current design, and the owner call that would confirm or reverse it is outstanding. Reversing it
would replace the camera request with a pawn placement and would make the distance question moot in
the same way retail does.

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
component by terminal owner. The presentation requires the exact `screen` material slot, allocates a
1024×768 render target, and binds it through a dynamic instance of the project's CRT screen
material. It never selects a slot by substring or guesses a new plane.

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

The CRT look lives in the screen material, not in a post-process. A project-authored material
samples the Slate render target and applies scanlines, a phosphor tint and glow, edge vignetting and
a slight curvature in UV space, then drives the result as emissive. Keeping the effect inside the
material that the model's own UVs address means it is bounded to the glass, is lit and occluded like
the rest of the prop, and cannot leak onto the viewport when another screen is on top. The
presentation binds the render target through one texture parameter, so the effect is independent of
everything the terminal draws. This is a Presentation-layer modernization under
`docs/project/remaster-direction.md`: the monitor the fiction depicts is a CRT, so the treatment
serves the original direction rather than overriding an artist decision, and it carries no classic
mode.

### 6.4 Screensaver ownership

The screensaver owns the surface whenever no session does — including before the terminal's first
use, so an idle computer reads as a working machine rather than as set dressing. Projection
ownership therefore belongs to the terminal's presentation rather than to the session screen: the
material instance and render target are established for a live `prop_hacking` and outlive every
session, and CommonUI releasing its input scope hands the same target back to the screensaver
instead of destroying it.

The screensaver reproduces the recovered behavior. It reprints `TerminalDefinition`'s authored
`"screen saver"` label at a row chosen uniformly in `[1, textrows - 1]` and a column bounded by
`textcolumns - labelLength`, in one of two palette styles chosen at random, clearing the previous
placement each time. `ss_start` is the idle delay before the first placement and `ss_delay` the
interval between placements; entering a session cancels the schedule and leaving it re-arms the
first placement. The label teleports; it does not scroll or bounce. The screensaver has no focus,
command or state-machine authority, and its placement is authority-side state rather than a local
animation.

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
- password and skill-attempt policy, including that Escape and Quit end the session from every input
  mode and while a skill attempt is in flight;
- the `0.7` facing gate and the 80-unit reach, at the focus, availability and use-icon boundaries
  alike;
- email read/deleted flags, first-open script-once, dependency and deletion filtering, and
  per-terminal versus `global_email` serialization;
- screensaver placement bounds, the `ss_start`/`ss_delay` schedule, and cancellation on entry;
- the four sound cue sites and their silence inside the function executor;
- keyboard, click and controller-action equivalence at the authoritative state/queue boundary;
- exact `screen` slots and both screen attachments on every model a live `prop_hacking` references,
  render-target projection and bezel clipping;
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

1. the `TerminalDefinition` parser, terminal state machine, `hackcmd` adapter and revisioned view,
   proven headless, with the `tuthack` Function transaction on the real entity queue;
2. session escape: Escape and Quit end the session from every input mode and during a skill attempt,
   and a content-load failure is a named error rather than a permanently unusable prop;
3. the screen basis derived from the model's `screen` and `screen_axis` attachments, feeding both
   the `0.7` facing gate and the fixed `Focus` camera framing that keeps the bezel in shot;
4. screensaver ownership of the projection surface, so an unused terminal is live before its first
   session;
5. the CRT screen material and the monospace type tokens;
6. the four authority-side sound cues and the local keystroke click;
7. email state, including the `global_email` reconciliation;
8. the semantic action palette, controller navigation and virtual text entry;
9. keyboard and gamepad tutorial acceptance at all three target resolutions.

Slices 1 through 3 are what the `tuthack` safe transaction requires; the rest complete the terminal
without introducing another widget-owned parser, command path, camera owner or persistence model.
