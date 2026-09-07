# Computer terminals (13.4)

Open-task specification for roadmap **13.4 Computer terminals & tutorial hacking**. No status
marks live here; status is `docs/project/roadmap.md` only. Design:
`docs/architecture/computer-terminal-architecture.md`; behaviour: `docs/vtmb/computer-terminals.md`;
exploration reports and retail captures: `$ELYSIUM_WORK_ROOT/_terminal_explore/`.

## Scope

Landed headless (2026-09-07): the `TerminalDefinition` parser, `FElysiumTerminal`'s exclusive
explicit-use session, the non-bindable `hackcmd` path, the timed Hacking bypass, the
`FElysiumTerminalScreenBuffer` cell screen with every draw body in retail's order, the retail
router by mode, the `Hacking_Strings` copy, the `InfoCtrl` hint value, the four cue sites, and
the `FElysiumMapSlice` fixture that proves the `tuthack` transaction on the real `sp_tutorial_1`
rows, and (slice B) the `0.7` screen cone off the baked `screen`/`screen_axis` sockets, the
immobilize, the far-arm hull-sweep pin and near-arm view snap, the named `Hacking` shot pushed on
entry and dropped on every exit, and the terminal gym host with its three `Elysium.Content.
TerminalGym*`/`TerminalAttachments` tests, and (slice C) the screensaver think on the authority's
think clock, the idle terminal views, and the world-lifetime projection owned by the presentation
subsystem (`UElysiumTerminalProjection`, registered with the use anchor), and (slice F) the four `soundgroup` cues proven on the slice through the
authored `old_computer` group, the executor silent, exit stopping every sound on the terminal, and (slice E) the focused keyboard leaf reproducing the client key-down and
char-insert bodies (TERM20), the five intents, the local draft composed onto the glass with retail's
fit guard, and the `InfoCtrl` hint on the HUD model (its drawn slot is Play-tier: the HUD widget
cannot be built without a local player), and (slice G) email: the 128-flag
bitmask, the visible-index table, ten rows per page, both input regimes and the raw single-key
open state, the first-open `runscript`, both retail off-by-ones, and the `global_email`
reconciliation on the player record (save schema 29), proven on a 12-mail definition and on
`haven_pc.txt` (`Elysium.Substrate.TutorialTerminalSlice`, `Elysium.Substrate.TerminalRouter`,
`Elysium.Substrate.TerminalScreenBuffer`). Nothing below the entity is faithful yet: the body,
the cone, the camera, the projection and the keyboard are the slices below. The 2026-09-06
exploration (audit, content facts, projection research, the slice-A decompiles and the B/C design
under `$ELYSIUM_WORK_ROOT/_terminal_explore/`) is the reference. Keyboard only; gamepad is out of
scope by owner call. *Design:* `docs/architecture/computer-terminal-architecture.md`; behavior:
`docs/vtmb/computer-terminals.md` (TERM15 §8.3 closes the client screen semantics, TERM17/18
§8.6 the draw bodies).

**The terminal gym (host for every slice below).** The `sp_tutorial_1` terminal slice
(`tuthack`, `tutsafelock`, `tutsafe`, the popups, the knobs and the door) spawned *with* bodies
at its authored transforms into the empty stage world the movement gym already builds, plus the
player pawn. It carries the real monitor, padlock, safe and door models and nothing else — no NPCs, no
logic_autos, no popups, no ambient triggers — so it is a controlled surface for everything that
needs a body: the screen cone, the pawn pin, the camera shot, the projection, the font on the
glass, the keyboard screen. Every slice from B on is accepted here through the Play tier's beat
script (`do`/`wait`/`assert`/`shot`) and its shot baseline. **No acceptance in this plan runs on
`sp_tutorial_1` or any live map** (owner call 2026-09-07: live maps are event-infested and never
a usable acceptance surface); the tutorial completion is PP6's own Play run. `Elysium.Substrate.TutorialTerminalSlice` is the headless host: Every later slice adds its
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

**Slice D — the picture (Play-tier remainder).** Landed (2026-09-07): the palettes, the `Mono`
role on the Terminus faces (imported with distance-field tiers 48/56/64), `SElysiumTerminalCells`
painting the composed grid with inverse video, the blinking cursor and the calibration pattern,
`UElysiumTerminalScreenTuning`, and the authored `/Game/ElysiumAuthored/UI/M_ElysiumTerminalScreen`
(unlit emissive from `Screen`, curvature, scanlines, vignette, bounds mask, `FlipU`/`FlipV`/
`Rotate90`, `UVDebug`) bound by the projection. Remaining: the Play-tier `shot` baselines on the
terminal gym at 1080p and 4K (grid crisp inside the bezel, layout against
`retail-shots/01-home-menu.png`, UV orientation per model read back from the calibration pattern
into `DA_ElysiumTerminalScreenTuning`, Terminus seating in the 28×32 cell) — `-nullrhi` allocates
no render target, so the picture is owner-piloted until 11.10.

**Slice H — the gym beat script (remaining half).** Landed: the read-only terminal diagnostic
(`GetDebugState`, the `elysium_entity_get` `terminal_projection` block, the Cog inspector section),
the content test on every `prop_hacking` model, and the native gym driver
`Elysium.Content.TerminalGymBeat` (screensaver on the glass, the use edge into the session, the
camera on the sockets, `Welcome, Jack.`, `Safe`/`chopshop`/`Unlock`/Enter/`quit` and the Ctrl+C
run, padlock, keycard, knob, camera dropped, screensaver back). Remaining: the Play-tier beat
script from injected real input with `shot` steps, when 11.10 lands, with the same assertions.

*Deps:* 4.11, AUD2, 9.6, 11.4–11.8, and **11.10 for the gym beat script**. Until the Play tier
lands, slices B–H are accepted on the terminal gym through a native automation test that spawns
the gym world, drives the same steps through the command bus and the intents, asserts authority
state and reads the projection's render target back for the layout checks; the beat script
replaces that driver when 11.10 lands, with the same assertions. The terminal gym builds on the
movement gym's empty stage world (`docs/architecture/debug-tooling.md`, `--gym`). A0, A, B and C have
landed (2026-09-07); only the Play-tier halves of D and H remain. Every acceptance is
headless (the map slice) or on the terminal gym; nothing in this plan is accepted on a live map.
