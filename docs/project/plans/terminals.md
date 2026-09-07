# Computer terminals (13.4)

Open-task specification for roadmap **13.4 Computer terminals & tutorial hacking**. No status
marks live here; status is `docs/project/roadmap.md` only. Design:
`docs/architecture/computer-terminal-architecture.md`; behaviour: `docs/vtmb/computer-terminals.md`;
exploration reports and retail captures: `$ELYSIUM_WORK_ROOT/_terminal_explore/`.

## Scope

Landed headless: the `TerminalDefinition` parser, `FElysiumTerminal`'s exclusive explicit-use
session, the non-bindable `hackcmd` path, the timed Hacking bypass and the `tuthack` Function
transaction on the one queue, plus a first render-target projection through the model's exact
`screen` slot. The state machine is a line list, not retail's screen; nothing below the entity is
faithful yet. The 2026-09-06 exploration (audit, content facts, projection research under
`$ELYSIUM_WORK_ROOT/_terminal_explore/`) rebuilds the subsystem in the slices below. Keyboard only;
gamepad is out of scope by owner call. *Design:* `docs/architecture/computer-terminal-architecture.md`;
behavior: `docs/vtmb/computer-terminals.md` (TERM15 §8.3 closes the client screen semantics).

**Slice A0 — the tutorial map-slice fixture (headless, first deliverable).** A test helper,
`FElysiumMapSlice`, that opens the real `$ELYSIUM_EXPORT_ROOT/sp_tutorial_1/sp_tutorial_1.ents`,
keeps a named set of entities plus everything their output rows target (transitively), parses
them through `FElysiumEntityDefs`, and spawns them into a headless `FElysiumEntityWorld` with no
bodies. The terminal slice is `tuthack`, `tutsafelock`, `tutsafe`, `trig_popup_safe`,
`trig_popup_note`, `item_k_tutorial_chopshop_stairs_key`, `tutdoordknob`, `tutdoordknob-wesp`,
`tutchopdoord` and the popups and logic they reach. The fixture is skipped, not failed, without the
export root, like the other content tests. It replaces the hand-wired `math_counter` in
`Elysium.Substrate.TerminalSession` for the transaction: the authored rows are what the game
loads, so a map/port mismatch is caught here and nowhere later. An entity in the slice that needs a
body, a view direction or a Python namespace to complete its arm is a named seam in the fixture
log, never a silent pass. The helper is generic — map name plus targetnames — so every other
tutorial beat (elevator, lockpicks, fan, Jack's teleports) reuses it.

The same slice has a second host, the **terminal gym**: the slice spawned *with* bodies at its
authored transforms into the empty stage world the movement gym already builds, plus the player
pawn. It carries the real monitor, padlock, safe and door models and nothing else — no NPCs, no
logic_autos, no popups, no ambient triggers — so it is a controlled surface for everything that
needs a body: the screen cone, the pawn pin, the camera shot, the projection, the font on the
glass, the keyboard screen. Every slice from B on is accepted here through the Play tier's beat
script (`do`/`wait`/`assert`/`shot`) and its shot baseline. **No acceptance in this plan runs on
`sp_tutorial_1` or any live map** (owner call 2026-09-07: live maps are event-infested and never
a usable acceptance surface); the tutorial completion is PP6's own Play run. *Acceptance:*
`Elysium.Substrate.TutorialTerminalSlice` begins use of `tuthack`, submits `Safe`, `chopshop`,
`Unlock`, Enter, `quit`, ticks the queue, and asserts `tutsafelock` unlocked then hidden at +0.5 s,
`trig_popup_safe` enabled and `trig_popup_note` disabled, the safe's `equip0` keycard transferable
to the player, `G.Tut_Key` written, `trig_jack_teleport_3` enabled, and both door knobs accepting
the keycard; a second run submits `Lock` and asserts the reverse rows. Every later slice adds its
assertions to this one test rather than to a new hand-built world.

**Slice A — the screen buffer and the retail router (headless).** Replace `ScreenLines` with
`FElysiumTerminalScreenBuffer` (put-char with wrap, newline to margin, scroll at `rows-1`,
backspace, style bit, cursor, clear) and re-express every draw in retail's order: logon/title box
with the one-cell margin, directory draw (`Home menu` / `Menu`, `Available menus` in `brackets`,
`Available commands`, the `Type menu or command:` prompt), password prompt (`Password required` /
`PASSWORD FAILED`, notify line through the same `"\n%s %c%s%c\n\n"` C formatting so the patch's empty
`brackets` NUL-truncate it exactly as retail, `[Press "ENTER" to go back]` on retry, `Password:` on
the last row), help, invalid command
(`Invalid command` + `Type "list"…` + `Type "help"…`), the Function executor's title + runtext and
the ack prompt `[Press "ENTER" to continue]` on row `rows-1`, and the cracking line. All copy from
`Hacking_Strings` via `FElysiumStrings::At("hacking_strings", i)` (the exported group key carries the
underscore) with the compiled key names as fallback. Router by mode
as retail `AcceptCmd`: cracking buffer swallows lines; password mode `quit`/empty cancels to the
directory; directory `quit` releases; `home`, `help`, `list`, `email`; acknowledge mode consumes
only the empty command; raw-character mode exists as a mode even though no computer content raises
it. Session struct with input flags, max input (0 = none; the 16-byte router cap is separate) and
digits-only. Same-player re-entry allowed. Content-load failure is a named error on the entity, not a
dead prop. The `InfoCtrl` HUD hint (`docs/vtmb/computer-terminals.md` §8.4) is a presentation
value on the terminal view — hidden, "Press CTRL-C to use the Hacking feat", "Making hack attempt
at skill N", "Skill too low to make hack attempt at difficulty N" — set at the recovered sites and
drawn bottom-centre by the HUD in the project's own type. Retail captures for every screen are
under `$ELYSIUM_WORK_ROOT/_terminal_explore/retail-shots/` (§8.5 joins them to the listings).
Before coding, decompile and record in `docs/vtmb/computer-terminals.md` the bodies §8.5 names but
does not yet list line by line: `FUN_1021aaa0` (builtins), `FUN_1021b030` (help), `FUN_1021c3c0`
(invalid command), `FUN_1021c890` (enter directory / mail), `FUN_1021c6d0` (executor),
`FUN_1021b2b0` / `FUN_1021b330` (rule and framed rows), `FUN_10217d60` (cracking stepper print),
and the format strings at `0x105a4a70`, `0x105b063c`, `0x105b0630`, `0x105a4944`, `0x105a1eec`,
`0x105b0628`, `0x105b064c`, `0x105b065c` (`vtmb_string`). *Acceptance:* buffer tests (wrap/scroll/margin/backspace/style); router-by-mode tests;
the `tuthack` transaction now ends in acknowledge mode with the prompt on row 23 and `Enter`
redraws the `Safe` menu; `quit` at the `Safe` password prompt returns to root without ending the
session; the root draw reproduces `01-home-menu.png` cell for cell (five-row title box, `safe`
under menus, `help`/`quit` under commands, prompt on row 22, bare input row 23) and the retry
prompt reproduces `04-password-failed.png` including the truncation; the HUD hint value is 3 on
every password-prompt render and cleared on the next accepted line.

**Slice B — the screen cone, immobilize, the retail camera.** `CanPlayerFocus` reads the placed
model's `screen` / `screen_axis` sockets and applies `normalize(eye.xy − screen.xy) ·
normalize(screen_axis − screen).xy > 0.7`; the same predicate answers availability and use-icon
eligibility. Entry immobilizes the player through `SetImmobilized`, and the active-use tick reproduces
`CBaseTerminal 0x10218320`: sweep the player's collision hull from the pawn toward the terminal
origin (retail mask `0x201400b`), set the pawn at the contact point, relink — every tick while the
session is live, so the player is held against the computer and stands there on exit. Camera: a named-shot loader for multi-shot
files (`ElysiumCameraShots::LoadNamed("special-case", "Hacking")`) and `FElysiumCameraDirector::Push`
on entry / `Pop` on exit, so the camera sits on `screen_axis` looking at `screen` at FOV 75 with the
HUD shown and the viewmodel hidden; a shot that cannot resolve refuses the session with the named
error. Exposure clamped for the handle's lifetime. *Acceptance:* cone tests at the focus,
availability and icon boundaries from the real attachment transforms; a session refuses on a model
without the attachments; a pawn placed 60 units off is at hull contact with the terminal after one
session tick and stays there; camera tests assert the pushed shot's origin/look-at equal the two socket
positions and that the previous view returns on every exit reason.

**Slice C — world-lifetime projection and the screensaver.** Move `FWidgetRenderer`, the
1024×768 render target and the material instance out of `UElysiumTerminalScreen` into a
per-terminal projection owned by `UElysiumPresentationSubsystem`, created when the prop body is
registered, re-applied after a model rebuild, destroyed with the body. The entity gains the
screensaver think: first tick `RandomFloat(0,1)` after spawn, `ss_start` after exit, `ss_delay`
(floored at 2.0) thereafter; each tick clears, picks row `[1, rows-1]`, column
`[1, max(0, columns-len)]`, one of two styles, prints the `screen saver` label, resets the style;
entry cancels it. The world publishes idle views for every live terminal with a body; the
presentation redraws a projection only when its revision changes and the body rendered recently.
*Acceptance:* screensaver schedule and bounds tests; a projection exists before the first session,
survives it and keeps redrawing after `quit`; the widget tree of the session screen holds no
render-target ownership.

**Slice D — the modern terminal render.** `M_ElysiumTerminalScreen` in `Content/ElysiumAuthored/`
(unlit, emissive from one texture parameter, scalar strength, faint scanlines, vignette, slight
curvature in UV space, `FlipU`/`FlipV`/`Rotate90`, a calibration-pattern debug draw) replacing the
engine pass-through. Terminus (TTF) (already in `Content/Fonts`, `FF_TerminusTTF_Regular`/`_Bold`
via `make_ui_fonts.py`) as a `Mono` role in the UI font library, MSDF with the distance-field ppem
raised for its pixel-derived outlines; four terminal palettes keyed by `colorscheme` in the UI style tokens. A leaf cell-painter
widget at fixed cell metrics with the style bit and a blinking block cursor while editing; the local
draft is mirrored into the grid at the cursor. Clear colour set before the target initializes.
*Acceptance:* projection tests on cell metrics and palette selection; Play-tier `shot`
baselines on the terminal gym at 1080p and 4K showing the 36×24 grid crisp and inside the bezel,
compared against `retail-shots/01-home-menu.png` for layout; UV orientation recorded per model
from the calibration pattern read back off the render target.

**Slice E — the keyboard screen.** Replace `UEditableText` with a custom focused Slate widget:
`OnKeyDown` for Enter, Escape (`quit`), Backspace, Ctrl+C (`break`); `OnKeyChar` for `0x20..0x7e`
under max input and digits-only; arrows/Home/End ignored; backtick unhandled. Intents
`SubmitCommand`, `SubmitCharacter`, `Acknowledge`, `Quit`, `Break` on the presentation subsystem,
each resolving owner and serial again. Projection failure no longer submits `quit`; the session
refuses to open instead. *Acceptance:* key-classification tests through the Slate application; the
draft clears on accept, on mode change and on session end; a pause menu covers and restores the
terminal.

**Slice F — the four cues.** Resolve the entity's `soundgroup` through the exported
`computers/<group>` manifest the way movers do and play `access` on entry, `accept` on every
directory or mail change, `error` on invalid command and on every password-prompt render, `typing`
at cracking start and flush; the executor stays silent. AUD2 owns the exported group; this slice
raises the cue sites. *Acceptance:* cue-site tests including silence inside the executor.

**Slice G — email.** The 128-flag bitmask, mail unlock and attempt count, `email_password` gate,
the visible index table rebuilt on every list draw (not deleted, dependency passes), ten rows per
page, hotkeys `n`/`p`/`d`/`m`/`q` matched against the localized words' first character, open =
render + first-open `runscript` once + mark read, `global_email` reconciliation keyed by entity
name, and serialization. *Acceptance:* the §12 invariants as tests; `haven_pc` content loads and
lists.

**Slice H — diagnostics and the gym beat script.** One read-only terminal diagnostic (owner, user,
serial, file, directory, pending, mode, flags, revision, projection, camera handle, attempt, last
command). Content test: `screen` slot and both attachments on every model a `prop_hacking`
references across the export. The Play-tier beat script on the terminal gym, from injected real
input: the screensaver label is on the glass before the first approach (`shot`); the pawn walks
into the cone and presses E; the camera settles on `screen_axis` looking at `screen`; the logon
box reads "Welcome, Jack."; `Safe`, `chopshop` (and a second run with Ctrl+C), `Unlock`, Enter,
`quit`; the padlock unlocked then hidden by the authored rows; the safe opened and the keycard
taken; the door knob opened with it; the previous view restored and the screensaver back on the
glass. Each step is an `assert` on authority state or a `shot` against the baseline.

**Working method (owner call 2026-09-07).** One writer in `Source/ElysiumUE` at a time, one build
at a time, orchestrated from the session: subagents only for read-only work — the retail
decompiles at the start of a slice and a review against `docs/vtmb/computer-terminals.md` at its
end (every arm ported, no invented strings, no seam left silently). No workflow fan-out and no
per-agent worktrees: the module, the build and the tree are one shared resource. Build order A0,
A, B, C, E, D, F, G, H — E before D so the whole loop is proven before editor time is spent on the
look. Slice D is the only slice that needs the editor open (the material under `ElysiumAuthored`
and the font faces through the `elysium` MCP server). One commit per slice, the roadmap line
updated, the landed part deleted from this file. The audit of the code as it stood on
2026-09-06 (file:line for every arm) is `$ELYSIUM_WORK_ROOT/_terminal_explore/audit-current-implementation.md`;
the projection research with the API names and gotchas (clear colour before the resource
initializes, gamma flag matching the renderer's, virtual-window focus as the alternative to a
viewport proxy, MSDF through `ISlate3DRenderer` unverified, expected screen UV span `V 0..0.75`)
is `projection-research.md` beside it.

*Deps:* 4.11, AUD2, 9.6, 11.4–11.8, and **11.10 for the gym beat script**. Until the Play tier
lands, slices B–H are accepted on the terminal gym through a native automation test that spawns
the gym world, drives the same steps through the command bus and the intents, asserts authority
state and reads the projection's render target back for the layout checks; the beat script
replaces that driver when 11.10 lands, with the same assertions. The terminal gym builds on the
movement gym's empty stage world (`docs/architecture/debug-tooling.md`, `--gym`). A0 first; A and B
are independent; C depends on A; D and E depend on C; F, G on A; H last. Every acceptance is
headless (the map slice) or on the terminal gym; nothing in this plan is accepted on a live map.
