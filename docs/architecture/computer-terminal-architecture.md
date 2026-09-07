# Computer-terminal architecture

How Elysium turns a `prop_hacking` computer into an exclusive, interactive terminal session in
Unreal. Recovered VtMB behavior lives in `docs/vtmb/computer-terminals.md`; implementation status
lives only in `docs/project/roadmap.md` 13.4; the open-task specification is
`docs/project/plans/terminals.md`.

The terminal reproduces the original gameplay contract — content, authored character grid, the
screen buffer semantics, command meaning, passwords, skill checks, scripts, outputs, email state,
the screensaver, the four sound cues and one-user authority — through a modern presentation. It
does not reuse VtMB's 14×16 bitmap glyphs, its four palettes, its blended scanline or its 512×512
character texture. This is the explicit Presentation-layer divergence allowed by
`docs/project/reconstruction-direction.md`: the rules remain faithful while their renderer is
replaced.

**Scope call (owner, 2026-09-06).** VtMB is a PC game and the terminal has no gamepad
implementation; the port targets the keyboard only. The controller action palette and virtual text
entry that an earlier revision of this document designed are out of scope and are not built.

## 1. Design in one page

A terminal session has one authority and two projections:

| Layer | Owner | Responsibility |
|---|---|---|
| Gameplay | `FElysiumTerminal` / `FElysiumPropHacking` in `FElysiumEntityWorld` | use eligibility (the `0.7` screen cone), exclusive user, parsed content, **the cell screen buffer**, the state machine, command execution, skill attempts, email state, scripts, queued outputs, the screensaver think, the four cues and persistence |
| Presentation | `UElysiumPresentationSubsystem` | one world-lifetime **projection** per live terminal (render target + screen material on the model's `screen` slot), redrawn on revision; no command interpretation |
| Local player | `UElysiumPlayerUISubsystem`, `UElysiumTerminalScreen`, the camera director and input scope | own the keyboard while a session is live, keep the local draft line, route intents, push and pop retail's `Hacking` camera shot |

The world owns the session **and the screen**. The authority prints into a `TextColumns × TextRows`
cell grid exactly as retail's server prints into the client's (`docs/vtmb/computer-terminals.md`
§8.2–§8.3); the widget rasterizes cells and never parses `TerminalDefinition`, changes an entity,
fires an output or decides whether a password is valid. The screensaver is the same authority
writing into the same grid while nobody is logged in, so the projection is alive from the moment
the prop has a body.

## 2. Evidence and fidelity boundary

The implementation consumes the contract in `docs/vtmb/computer-terminals.md`. In particular:

- only one player owns a terminal at a time, and the same player may re-enter (§7.2);
- the logical screen is the authored `textcolumns × textrows`, clamped to `[4,36] × [2,24]`;
- the screen buffer is put-char with wrap at `columns`, newline to the margin, scroll-up at
  `rows - 1`, backspace, and one style bit per cell (§8.3);
- Enter sends `hackcmd <line>`, Escape sends `hackcmd quit`, Ctrl+C sends `hackcmd break`,
  raw-character mode sends `hackcmd %c`, acknowledgement sends the empty command (§8.1);
- the router is retail's: a live cracking buffer swallows input; password mode treats empty or
  `quit` as *cancel to the directory*, not as a guess and not as an exit; directory-mode `quit`
  releases the session; every completed Function, first unlock, mail entry and invalid command
  ends in acknowledge mode with `[Press "ENTER" to continue]` on row `rows - 1` (§9–§11);
- a Function resolves dependency → title/runtext → enqueue `OnTriggerN` → synchronous `runscript`
  → ack prompt; target delivery follows through the shared event queue (§11);
- the four `soundgroup` cues fire at the recovered sites and the executor is silent (§14);
- the screensaver is one label reprinted at a random cell in one of two styles on
  `ss_start` / `ss_delay` (§13);
- email state is terminal-local unless `global_email` promotes it to player state (§12);
- all rendered copy comes from the exported `Hacking_Strings` table (§16), never from literals,
  and is formatted with C semantics so the patch's empty `brackets` truncate exactly as retail
  (§8.5);
- the `InfoCtrl` HUD hint (§8.4) is reproduced as a value on the terminal view and drawn by the
  HUD bottom-centre in the project's type: "Press CTRL-C to use the Hacking feat" for the whole
  password prompt, the skill line while cracking, the too-low line on a failed bypass, hidden on
  the next accepted line and on exit.

Divergences are named in §6 (presentation) and §3.3 (pawn pinning). Nothing else diverges.

## 3. Gameplay authority and session lifetime

`FElysiumTerminal : FElysiumSkillEntity` owns the shared terminal session and the screen buffer.
`FElysiumPropHacking` adds the `TerminalDefinition` content and `prop_hacking` leaf state. Entity
identity stays plain C++; actors, widgets and cameras are optional bodies over the handle.

### 3.1 The screen buffer

```text
FElysiumTerminalScreenBuffer            // plain C++, testable without a world
    Columns, Rows                       // authored, clamped
    Cells[Rows][Columns] { Char, Style } // Style: Normal | Alternate (retail bit 0x80)
    CursorRow, CursorColumn, LeftMargin
    CurrentStyle
    PutChar(c)     // wrap / newline / scroll / backspace exactly as §8.3
    Print(text)    // PutChar per byte
    SetCursor(r,c) // type 1
    Clear()        // types 4 / 7
    SetStyle(s)    // types 5 / 6
    Revision       // bumped by every mutation
```

Every server-side draw in the recovered bodies (directory draw, title box with the one-cell
margin, password prompt, help, ack prompt at `rows - 1`, the cracking line, the screensaver label)
is a sequence of these calls. The buffer is the only thing the presentation reads.

### 3.2 Session state

```text
FElysiumTerminalSession
    Owner terminal handle, user player handle, session serial
    Current directory (-1 root, -2 mail, >= 0), pending password target (-1 / -2 / >= 0)
    Input mode: Line | RawCharacter | Acknowledge (retail m_HackFlags 0x4 / 0x1)
    Digits-only flag (0x2), max input (0 = none), directory-key policy (always off on computers)
    Cracking buffer m_szHackPWD and the typed-versus-skill flag
    Mail cursor, open message, visible index table, page
```

Every state change bumps the screen revision. A request carries owner and serial; the world rejects
stale input from a closed or superseded session.

### 3.3 Eligibility, entry, exit

Eligibility reproduces the recovered gate: the generic use query (80-unit reach, cosine cone) and
the terminal's own predicate `normalize(eye.xy − screen.xy) · normalize(screen_axis − screen).xy
> 0.7`, both attachments read from the placed model's skeleton. One predicate serves focus,
availability and use-icon eligibility, so the stock use icon shows exactly when the terminal can be
used. A model missing either attachment or the `screen` slot fails the content test and, at
runtime, logs a named error that refuses the session (entity, model, which part is missing).

Entry (retail §7.3, in this order): reset session flags; `OnUseBegin` and the Hacking skill
attach; play `access`; immobilize the player; mark in use; reconcile global email; cancel the
screensaver think; draw the directory and prompt; push the `Hacking` camera shot.

Exit converges on `EndPlayerUseSession` from: directory-mode `quit`, a failed use gate on the next
use tick (`Disable`, lost eligibility), damage to the player, dialogue start, teleport, entity
removal, map travel. Exit: save global email; clear the client edit state; release immobilize;
mark not in use; `OnUseEnd` and skill detach; re-arm the screensaver at `ss_start`; draw the idle
title; reset directory and pending; pop the camera. End is idempotent. A live session blocks
saving; persistent terminal and email state serializes only after it ends.

**Pawn pinning (reproduced).** Retail immobilizes the player (button mask stripped, wish velocity
zeroed) and its active-use maintenance body `CBaseTerminal 0x10218320` sweeps the player's
collision hull from the pawn toward the terminal origin (mask `0x201400b`), writes the pawn to the
contact point and relinks it, every tick of the session. The port does the same: immobilize
through the player entity's `SetImmobilized`, and a per-tick capsule sweep that holds the pawn
against the computer, so distance can never end a session and the player stands at the monitor on
exit. Owner call 2026-09-07: reproduce, not a seam.

## 4. Content and command execution

The runtime reads patch-first `vdata/hackterminals/*.txt` through the shared rulebook path and
`Hacking_Strings` through the shared strings table (`FElysiumStrings::At("hacking_strings", i)`
with the compiled-in key names as fallback, as retail's `FUN_10219400` does).

The entity world exposes one non-bindable command identity, `hackcmd <line>`, executable only for
the player who owns the active session, plus `SubmitTerminalCommand(owner, serial, text)` for UI
and automation. The state machine owns built-ins, navigation, password validation, the timed
cracking bypass, function and email actions. A successful Function executes its recovered
transaction on the one deterministic entity queue; the UI only ever receives the resulting
revision.

### 4.1 Email state

Authority state, not a widget mode: 128 per-message flags (read `0x1`, deleted `0x2`), mail unlock
and attempt count. Opening a message renders the body and, if unread, runs its `runscript` once
and marks it read. Dependency-failed and deleted messages are absent from the list. The mail area
asks for `email_password` only when authored non-empty and not already unlocked. `autodelete` is
inert and not implemented. `global_email` overwrites local flags from the player record on entry
and writes them back on exit, keyed by entity name. Evidence: `docs/vtmb/computer-terminals.md`
§12.

### 4.2 Terminal audio

Authority-side, resolved from the entity's `soundgroup` through the exported
`sound/usable/soundgroups.json` (`computers/<group>/{accept,access,error,typing}`) the same way
movers resolve theirs: `access` on entry, `accept` on every directory or mail change, `error` on an
invalid command and on **every** render of the password prompt including the first, `typing` at
cracking start and at the cracking flush. The executor is silent. The client-side keystroke click
is silent on computers in retail and stays silent here.

## 5. Presentation contract

`FElysiumTerminalView` is the immutable projection of one terminal's screen:

```text
FElysiumTerminalView
    Owner, SessionSerial (0 while idle), Revision
    Columns, Rows
    Cells[]              // Rows × Columns of { Char, Style }
    CursorRow, CursorColumn, bCursorVisible   // visible only while a session edits a line
    InputMode, MaxInput, bDigitsOnly
    ColorScheme          // authored 0..3, selects one of four project palettes
    HudHint              // None | PressHackKey | HackAttempt(rating) | SkillTooLow(difficulty)
```

Two publications exist. **Idle** views are published for every live terminal with a body, so the
screensaver reaches the glass; the presentation redraws a projection only when its revision changes
and only if the body was rendered recently. **Session** view is the same struct for the terminal
the local player is using, and additionally drives the keyboard screen.

Intents: `TerminalSubmitCommand(owner, serial, line)`, `TerminalSubmitCharacter(owner, serial,
c)` (raw mode), `TerminalAcknowledge(owner, serial)` (the empty command), `TerminalQuit` and
`TerminalBreak` (the literal `quit` / `break` lines, which the router interprets by mode exactly as
retail's `AcceptCmd`). Each resolves the current session again before acting.

The local draft line is player-side state keyed by owner and serial: it is mirrored into the
rendered grid at the cursor (retail's client inserts into `+0xed8` and echoes through put-char),
cleared on accept, on a mode change and on session end, and never republished as gameplay state.

## 6. Putting the console on the computer screen

### 6.1 The screen surface

A terminal model carries its display twice: a material slot named exactly `screen` and the
`screen` / `screen_axis` attachments. Both survive export and bake (the placed-model skeleton keeps
the two bones, the mesh keeps the slot; verified for nine of the ten `prop_hacking` models). Content
validation requires all three on every model a live `prop_hacking` references.

### 6.2 Projection ownership

The projection — `FWidgetRenderer`, a square 1024×1024 `UTextureRenderTarget2D` (2× retail's 512×512 client texture, so the models' authored `screen` UVs land) and a dynamic instance of
the project's screen material bound through one texture parameter to the `screen` slot — is
created by the presentation subsystem when a terminal's body is registered and lives until the body
goes away. Sessions borrow it; nothing about it is created or destroyed by the CommonUI screen. The
render target is a transient `UPROPERTY` on the world-lifetime owner, the widget renderer a
`TUniquePtr` destroyed before it; construction is gated on `FApp::CanEverRender()`. The clear colour
is set before the resource is initialized. A model rebuild that swaps the skeletal mesh re-applies
the material override.

### 6.3 Camera

Retail's own shot: `Hacking` from `vdata/camerashots/special-case.txt` — camera on attachment
`screen_axis`, look-at attachment `screen`, both following, FOV 75, `ShowHud 1`,
`DrawViewmodel 0`. The port pushes it through the existing legacy director
(`FElysiumCameraDirector::Push`) with a named-shot loader for multi-shot files, and pops it on exit
so the exact previous view returns. No distance solve and no bezel margin are computed; the
authored attachment *is* the framing. A model without the attachments refuses the session with
the named error above; a shot that cannot resolve for any other reason (no camera component, a
missing shot block) is warned once by name and the session continues cameraless, as retail's
`FUN_10070470` NULL path does (2026-09-07). Readability at 16:9 is judged on the terminal gym's
shot baseline; a dolly along the screen normal would be the named Feel modernization if needed.

### 6.4 The rendered terminal (presentation modernization)

The glass shows a clean modern terminal, not VtMB's raster:

- **Cell painter.** A leaf Slate widget paints the view's cells at fixed metrics derived from the
  render target — retail's fixed 14×16 cell at 2× (28×32), the block centred as the retail rasterizer centres it (`docs/vtmb/computer-terminals.md` §8.3, TERM21) — one glyph per cell, so the authored grid is
  exact. Text is **Terminus (TTF)** — the Linux console and xterm face of the late 90s, OFL 1.1
  with Reserved Font Names, shipped unmodified as `Content/Fonts/TerminusTTF-{Regular,Bold}.ttf`
  and imported as `FF_TerminusTTF_*` faces under the `Mono` role (owner call 2026-09-07). Its
  outlines are pixel-derived, so the role imports as an MSDF face with the distance-field ppem
  raised to 48–64 to keep the stair corners, and it is drawn at scale 1 so one Slate unit is one
  texel. At the 28×32 px cell the glyph is 16 px wide and sits letter-spaced in its cell, which is
  how a 36-column console looked. No `STextBlock` of joined rows, no scale box.
- **Styles.** The cell style bit maps to normal versus alternate (inverse) rendering. The authored
  `colorscheme` 0–3 selects one of four project palettes (background, foreground, alternate pair,
  cursor) defined as terminal tokens in the UI style; the retail palettes are not copied.
- **Cursor.** A block cursor at the authority cursor while a line is being edited, with a slow
  blink that is local presentation.
- **Screen material.** `M_ElysiumTerminalScreen` under `Content/ElysiumAuthored/`: unlit, the
  render target driving emissive through a scalar strength, with restrained CRT treatment in UV
  space — faint scanlines, edge vignette, slight curvature — so the monitor still reads as grimy
  2004 hardware while the text stays crisp. `FlipU` / `FlipV` / `Rotate90` parameters and a
  calibration pattern resolve each model's UV orientation once. It is not a V2 master instance.
- **Exposure.** The shot fills the frame with a bright emissive; exposure is clamped for the
  lifetime of the camera handle.

Presentation test: a modern face on a monitor is presentation; the grid, the copy, the prompts and
the scroll are logic and stay retail.

## 7. Keyboard

Keyboard-only. The CommonUI terminal screen owns focus with a UI-only input scope at priority 42
(above Dialogue, below Character and Menu); both player mapping contexts are removed with
pressed-key suppression so the `E` that opened the terminal does not leak. The focused editor is a
custom Slate widget (not `SEditableText`): `OnKeyDown` handles Enter, Escape, Backspace and
Ctrl+C; `OnKeyChar` inserts printable ASCII `0x20..0x7e` subject to max input and the digits-only
flag; arrows, Home and End are ignored because computers never allow directory keys; backtick is
left unhandled for the console. In raw-character mode each printable is forwarded immediately and
nothing accumulates. In acknowledge mode only Enter (the empty command) is forwarded. Escape
always sends `quit`; what `quit` does is the router's decision by mode. A pause menu may cover the
terminal without ending it.

## 8. Diagnostics and acceptance

One diagnostic reports owner, user, serial, parsed file, current directory and pending target,
input mode and flags, screen revision, projection state (slot, render target, material, UV
parameters), camera handle, pending skill attempt and the last accepted or rejected command. It is
read-only.

Automated contracts run against two worlds: hand-built entities for unit rules, and the
**tutorial map slice** — the real `sp_tutorial_1.ents` filtered to the terminal, padlock, safe,
popup triggers, keycard and door, spawned headless through the ordinary entity-defs path — for the
transaction, so the authored wires and the real content file are what the tests exercise.

- screen buffer: wrap, newline to margin, scroll at `rows - 1`, backspace across the margin,
  style bit, clear, cursor;
- parser ordering, patch-first resolution, dependency filtering, `Hacking_Strings` resolution and
  fallback;
- exclusive entry and same-player re-entry, stale serial rejection, every exit reason, idempotent
  teardown, save block;
- the router by mode: cracking swallows input; password `quit`/empty cancels; directory `quit`
  releases; acknowledge mode; invalid command; `home`, `help`, `list`, `email`;
- Function order, `OnTriggerN` enqueue with activator provenance, the ack prompt on row `rows - 1`;
- password and bypass policy, the timed cycle and its typed-versus-skill fail arms;
- the `0.7` cone at focus, availability and icon boundaries, from the real attachments;
- email flags, first-open script-once, filtering, per-terminal versus `global_email`;
- screensaver: first tick, `ss_start` re-arm, `ss_delay` floor at 2.0, row `[1, rows-1]` and
  column `[1, columns-len]` bounds, two styles, cancel on entry;
- the four cue sites and executor silence;
- content: `screen` slot and both attachments on every referenced model;
- presentation: projection lives before the first session and survives it; redraw only on
  revision; the keyboard widget's key classification.

Retail reference captures of every tutorial screen (home menu, typing, password prompt, failed
retry, help, screensaver) live under `$ELYSIUM_WORK_ROOT/_terminal_explore/retail-shots/` with a
README joining each to its draw body; the headless tests compare the cell grid against them and
the gym shot baseline compares framing and the HUD hint.

Played acceptance runs on the **terminal gym** — the tutorial map slice spawned with bodies into
the empty stage world, nothing else in it — through the Play tier's beat script from injected real
input: approach the monitor inside the cone, press E, the camera settles on `screen_axis`, the
logon line reads, `Safe`, `chopshop` (and a second run with Ctrl+C), `Unlock`, `Safe doors
unlocked.`, Enter, `quit`, the previous view returns, the padlock is unlocked and hidden, the safe
gives the keycard, the door opens with it. The idle screensaver must be on the glass before the
first approach and after leaving. No acceptance for this system is planned on a live map (owner
call 2026-09-07); the tutorial's own completion is PP6's Play run.
