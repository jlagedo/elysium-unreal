# Debug & development tooling architecture

Elysium-Unreal gameplay objects are built at runtime from `$ELYSIUM_EXPORT_ROOT` intermediates; the map's
baked look is adopted rather than authored as gameplay state. The editor's asset-centric tools
therefore cannot inspect or persist entity fields, queued I/O, script state, or generated
collision. Debug tooling lives in-process, in the running game, and reads the same structures
the game plays from. PIE remains a useful viewer, not the authoring surface for those systems.
Bake and runtime ownership are `docs/architecture/uasset-bake-spike.md`; work status is `docs/project/roadmap.md` P2.

The architecture is four layers, numbered 0–3. Everything is development-only (compiled out of or
disabled in Shipping); nothing here touches the bring-your-own-game posture.

**Interaction principle — F1-first (load-bearing).** The Cog F1 UI is *the* surface the developer
touches. Every debug capability must be reachable and operable from a Cog window with **nothing to
memorize** — a button, checkbox, or list entry, not a remembered command. The `elysium.*` console
verbs are a *thin scriptable layer over the same runtime state* (for `-ExecCmds` automation, cron/
headless runs, and Source-modder muscle memory) — never the only way to reach a feature, and never the
primary surface. New tooling ships its Cog controls first; a verb without a Cog equivalent is
incomplete. (Concretely: the P2.3 `ent_*` picker/overlays/breakpoint are driven point-and-click from
the Entity Inspector, and the verbs just flip the same `UElysiumEntityDebugSubsystem` state.)

Corollary — **a Cog window left open keeps live-updating while you play**, because Cog renders every
*visible* window each frame regardless of the F1 menu (only the menu bar is gated by input capture), and
it runs every window's `RenderTick` whether or not that window is visible. So a "what am I looking at"
readout does not need a separate HUD, and the tool that answers it does not need its own window open.

**Selection is by click.** With the F1 menu up, LMB anywhere over the world (i.e. not over an imgui
window) picks whatever is under the cursor, RMB clears, and the Entity Inspector highlights the result in
place. The game is **not** paused — it keeps running under the cursor; stop it from the Time Scale window
when you want it still. Because the pick lives in `RenderTick`, it works with the inspector closed: click
first, open the window after.

**A World Viz gizmo marker outranks everything it is drawn over.** The marker is a deliberate "select
me" handle, and for the ~1,000 bodiless entities on a map — lights, `ambient_generic`, logic — it is the
only clickable representation there is. Whether a marker counts as drawn follows the gizmo mode, so what
you see is what you click: *Visible* depth-tests the cubes, so one behind geometry does not pick;
*All* draws them x-ray, so any of them does; *Off* skips the source entirely. The pick tests the exact
28 cm cube — deliberately not a screen-space tolerance, so a dense cluster stays separable — and clicking
the same cluster again steps to the next marker behind the current one, wrapping. With gizmos on, the
`ent_*` picker's 2 m perpendicular fallback for bodiless entities is suppressed; it would undo that
precision. With gizmos off it is still the only way to reach one.

Failing a gizmo, the pick resolves three geometry sources and takes the nearest — brush entity bodies
(physics, so invisible trigger volumes are pickable), prop instances, and world/sky surfaces. The last
two are **CPU ray-casts against the real triangles**, not physics traces, because physics cannot answer
them: under the default
`elysium.BrushCollision 1` the world render mesh is built with collision off (the `.hulls` convex set is
the collider, and it carries no material and no face), and a solid prop's cooked collision is a single
convex hull of the whole model. A world pick highlights the whole BSP face — the exporter emits a fresh
vertex per face corner and `BuildMeshFromObj` remaps sections on the global index, so triangles of one
face share local indices and adjacent faces share none; flooding across shared edges stops at the face
boundary on its own.

The highlight is drawn with imgui (projected fill + outline + label), not scene geometry. That buys three
things: it costs no assets, it reaches things with no renderable mesh at all (trigger volumes), and it
keeps drawing when the world is time-scaled to a stop, since Cog's render tick is not the game tick.

---

## Layer 0 — engine built-ins, wired in (near-zero cost)

Things Unreal already does that only need hooking up:

- **Actor labels + Outliner folders on every spawn path.** `AActor::SetActorLabel` and
  `SetFolderPath` are editor-only symbols — wrap in `#if WITH_EDITOR`. With labels set
  (`Map:sp_tutorial_1`, `Props`, `LightRig`, later `ent:elevator_door_1 (func_door_rotating)`),
  the PIE World Outliner becomes a free scene browser: runtime-spawned actors appear live,
  their `UPROPERTY` state is inspectable in Details, **Eject (F8)** detaches into a free
  editor camera while the game runs, and pause + frame-step work. Caveats: edits there are
  ephemeral (fine — data comes from the pipeline), and none of this exists in `uv run elysium run play`
  standalone (`-game` has no Outliner) — which is why Layer 1 exists.
- **The `elysium.*` console surface stays the single tuning mechanism.** CVars
  (`TAutoConsoleVariable`) for every tunable, `FAutoConsoleCommandWithWorldAndArgs` for verbs.
  CVars are settable from console, ini, command line, and the Layer-1 UI alike. A
  `UCheatManager` subclass is the home for player-centric cheats (`UFUNCTION(Exec)`
  auto-registers with autocomplete).
- **`elysium.reload` — the data hot-loop.** Re-runs `Travel` on the current map. Combined
  with the pipeline (`UE_bsp_to_scene.py` → `$ELYSIUM_EXPORT_ROOT`), this is the whole recook-free
  iteration cycle: edit exporter → re-export → `elysium.reload` in the running game. No
  directory watcher — the `DirectoryWatcher` module is editor-only; if auto-reload is ever
  wanted, poll `IFileManager::GetTimeStamp` on the map's sidecars. Explicit reload is enough.
- **`DrawDebugHelpers` for in-world visualizers**, each gated by an `elysium.*` toggle
  (the existing `elysium.lights`/`elysium.props` pattern). Compiled out in Shipping via
  `ENABLE_DRAW_DEBUG`.
- **Visual Logger (`UE_VLOG*`) as the "rewind" answer.** Recording works in standalone
  (`.vlog` files), scrubbing happens later in the editor's Visual Logger tab: per-actor,
  per-category snapshots and shapes on a timeline. Once the entity layer fires events, every
  I/O dispatch gets a VLOG line — post-hoc scrubbable causality for a whole `uv run elysium run play`
  session. No engine surveyed does true time-rewind; recorded history is what actually ships.
- **Perf/introspection built-ins** (all work in `-game` dev builds): `stat unit/gpu/memory`,
  custom `DECLARE_CYCLE_STAT` groups for map-load phases, Unreal Insights via `-trace=`,
  `obj list class=`/`obj refs` for UObject-leak forensics, `ToggleDebugCamera` (free-fly with
  under-crosshair actor/material readout), Widget Reflector later for the VGUI menu work.

## Layer 1 — Cog: the in-game debug UI shell (the one dependency)

**Adopt [Cog](https://github.com/arnaud-jamin/Cog)** (MIT), vendored into `Plugins/` from
`main`. Rationale over alternatives:

- It is the strongest fit for a world with no editor representation: an ImGui **object
  browser + reflection-driven property grid** that finds and edits any live `UObject` —
  actors, components, MIDs — in the running game, PIE *and* `uv run elysium run play` standalone alike.
- ~40 stock windows we'd otherwise hand-write: inspector, console, output log with
  per-category filtering, collision viewer, plots/metrics, tweaks, spawn, time scale,
  scalability. The GAS/AI windows are ignorable dead weight.
- Dev-only posture is built in: works in packaged builds, disabled by default in Shipping.
- It ships its own ImGui integration (`CogImGui`) — no second plugin to choose; NetImgui
  remote debugging comes along for free if ever needed.
- Maintained (active through 2026), README pins "UE 5.5 or greater", no reported 5.7/5.8
  breakage; budget for minor compile fixes on 5.8 (known trivial patches: the ImPlot
  `INFINITY` MSVC error, issue #71). Integration is a `UWorldSubsystem` + `.Build.cs` deps.
- **Local behavioral patch (`CogImguiContext.cpp`, `SetEnableInput`):** when the F1 menu closes, fully
  restore mouselook capture — `.CaptureMouse().UseHighPrecisionMouseMovement().LockMouseToWidget()`,
  mirroring `FInputModeGameOnly`. Stock Cog restores only `CaptureMouse`, dropping the high-precision
  (raw/relative) mouse the game had; the cursor then reverts to absolute-position and either drifts out
  of the viewport or, once locked, clamps at the edge and turning stops. High-precision re-enables
  recentred relative deltas (infinite rotation) and implies the widget lock. This project is a
  `LockOnCapture` mouselook game (`Config/DefaultInput.ini`). Re-apply if Cog is updated.

Custom Cog windows grow with the runtime, reading Elysium's own data structures directly
(ImGui code is plain immediate-mode C++ — no reflection or UI assets needed, which matters
because tier-1 logic entities are plain C++ objects, not UObjects):

The launcher is task-based rather than a flat inventory: Elysium tools are grouped under Session,
World, Look, Audio, Characters, and Gameplay; engine tools under World, Diagnostics, Performance,
and Session. Menu rows are conventional click-to-open toggles — they never expand an entire live
window on hover. Within a tool, tabs separate current state from editing, inventories, and history;
low-frequency diagnostics and map-wide actions are collapsed. This keeps the F1-first rule without
making every capability compete for attention at once.

- **Maps** — map list, travel, `elysium.reload`, load-phase timings, and the player pose
  (metres/Source units/yaw), movement/skybox/light state, and FPS readout (the Canvas HUD
  itself keeps only the reticle, sign panels, and the `env_fade` screen fade).
- **Lights** — the rig's source list, per-style intensity curves, live scale tuning, and a
  per-light inspector. One source is selected at a time — from the list, or by clicking its marker
  in the world — and its intensity, colour, reach, falloff, cone, shadows and scattering are edited
  directly, with a `FCogDebug_Gizmo` on its transform and an isolate toggle that hides the rest.
  Editing marks the source overridden in the rig, which takes it out of both passes that would
  write back over it (the global calibration and the lightstyle animation). A per-source **Enabled**
  switch is the orthogonal axis: it changes no value, so a light comes back exactly as it was, and
  it outranks both the rig's master toggle and the isolate pass — the switch a fill-light survey is
  walked with. **Save** writes that survey out as JSON to `$ELYSIUM_EXPORT_ROOT/_lights/<map>.json` (one file
  per map, overwritten each save): the counts, the calibration the judgement was made under, and one
  record per edited source keyed by `.lights` line index, so the untouched complement joins back from
  the sidecar. Nothing else persists — the edits live in the running rig, so a map reload restores
  the sidecar's calibration.
  Lights carry no collision, so the world pick is a screen-space nearest-marker test rather than
  `ElysiumPick` — which is also what makes a light embedded in solid geometry selectable.
- **Entities** (M3) — browser over `FElysiumEntityWorld`'s registry: all 1,226 records
  including inert unhandled classnames, filter by classname/targetname, histogram.
- **Entity inspector** (M3) — one entity's keyvalues, outputs (all 7 fields), tier, spawn
  state, visibility; buttons to fire any input by hand. This is the B2 "primary test
  harness" window.
- **Event queue** (M3) — pending events with fire times, the I/O history ring buffer,
  pause/step controls (Layer 2).
- **World Viz** (P2.4) — the control panel for the map-wide in-world layers: entity gizmos
  (off/visible/all, class-color-keyed), wireframe trigger hulls (by class or state), and fading
  I/O beam arrows. Every control flips the same `Viz()` state the
  `elysium.ent_gizmos`/`showtriggers`/`ent_beams` verbs flip. The gizmos are a **retained** layer
  (`FElysiumGizmoLayer`): a GPU-instanced cube mesh (`M_Gizmo`/`M_Gizmo_XRay`, per-instance-custom-data
  colour) built once per map and updated per-instance only when an entity's state changes (via the
  `FElysiumEntityWorld::SetVisualChangedHook` event seam) — no per-frame draw-call round trip. Triggers,
  beams, and labels stay immediate-mode `DrawDebug` (bounded/near-only, so cheap).

The debug UI carries VtMB's skin (`ElysiumCogStyle`): near-black warm grounds, blood red as the one
accent, bone text, with candle amber / absinthe green / wound red reserved for the three data states
that must read at a glance. It is installed into ImGui's *global* style, so it covers the stock Cog
windows and the F1 menu bar too, and every window draws its state colours from the shared semantic
names rather than from literals. `elysium.CogTheme 0` restores stock ImGui dark for an A/B. Stock
Cog's `CogEngineWindow_ImGui` — the Dear ImGui / ImPlot demo, metrics, debug-log and style-editor
toggles — is deliberately not registered: it is ImGui's own showcase, not this project's debug
surface.

The Slate `UElysiumConsoleSubsystem` designed in `docs/architecture/map-architecture.md` is **superseded**:
UE's built-in console (already bound on `` ` ``/`'`) plus Cog's console/log windows cover it.
The one console upgrade worth doing later is dynamic autocomplete of targetnames for
`elysium.ent_fire` (subclass `UConsole::BuildRuntimeAutoCompleteList` via a custom
`UGameViewportClient`).

The Remote Control plugin (HTTP/WebSocket property access; works in `-game` with
`-RCWebControlEnable`) is a known **option, not part of the plan** — Cog in-process covers
the same need with less friction; revisit only if second-machine debugging becomes real.

## Layer 2 — Source-style entity debug verbs (M3, on the B2 chokepoints)

The Source SDK's `ent_*` toolset is the proven blueprint for debugging exactly the system
Track B rebuilds, and it is cheap **because the entity layer routes everything through two
chokepoints**: input dispatch (`AcceptInput`-equivalent, B2's name→member table) and the
event queue's `AddEvent`. Instrumenting those two functions once yields the whole toolset.
This is a design constraint on B2, adopted now: *no I/O side channels; every input delivery
and every queued output passes through the chokepoints.*

What lands with M3 (names mirror Source so the muscle memory transfers):

| Verb | Behavior |
|---|---|
| `elysium.ent_fire <target> [input] [param] [delay]` | Injects through the real event queue — same code path as game outputs, so manual tests are faithful. The M3 acceptance chain (elevator button) is testable one link at a time. |
| `elysium.ent_text` / `ent_bbox` / `ent_messages` | Per-entity debug **bitmask** + overhead text/box/timed I/O overlays via `DrawDebugString`/`DrawDebugBox`. `ent_messages` draws `(time) input caller ← / output → target,delay` lines anchored to the entity, fading after ~10 s. |
| *(no argument)* → **picker** | Every `ent_*` verb resolves its target by targetname, else classname, else **the entity under the crosshair** — Source's single highest-leverage debug design decision. |
| `elysium.ent_pause` / `ent_step [n]` | Freeze the event queue; release one queued event at a time. Pause also stops overlay fade (the evidence stays on screen). A single-stepper for causality bugs. |
| `elysium.trigger on` / `off` | Globally suspend or resume map-driven gameplay for exploration: proximity and use-trigger ingress, entity thinks/timers, and deferred I/O/Python execution. Work already queued is held and resumes on `on`; set it off before travel to hold map-load ignition. |
| `elysium.ent_dump <name>` / `ent_info <classname>` | Live keyvalue/state dump of an entity; schema dump (supported inputs/outputs) of a class. Driven by the entity records + input tables — no per-class code. |
| `elysium.showtriggers` | Render trigger volumes as wireframes, colored by classname (or enabled/disabled state); I/O beam lines (`DrawDebugDirectionalArrow` with duration) from caller to target on fire — exceeding Source, which only drew textual arrows. |
| `elysium.developer 2` equivalent | Every dispatch logs `(sim-time) input caller: target.Input(param) [python]` to a dedicated log category (the field-6 Python string is VtMB-specific and must be in the line). |
| I/O history ring buffer | Every dispatch is recorded into a bounded ring (Source's `env_debughistory`: 1000 lines), dumpable on demand and cheap enough to serialize into saves later — postmortem forensics without having had logging on. |
| `elysium.ent_break <target> [input]` | Auto-`ent_pause` when a matching input is delivered — breakpoint-on-entity-event; trivial given the chokepoint, and better than any surveyed prior art. |

Each dispatch also emits `UE_VLOG` events (Layer 0) for offline scrubbing.

## Layer 3 — the agent-facing surface (P2.7, MCP)

Layer 3 makes the third consumer of the `elysium.*` runtime state — after the human at Cog and
the script — first-class: an **AI agent** driving the game for QA and
tests. It reads and writes exactly the state Layers 1–2 expose, through structured **MCP tools**
instead of parsed console text — no new dispatch, no I/O side channel (the fire path is still
`FElysiumEntityWorld::EnqueueInput`, the same chokepoint the Inspector and `ent_fire` use).

**Transport.** UE 5.8 ships an experimental `ModelContextProtocol` plugin that embeds an MCP HTTP
server in the process; any MCP client (Claude Code, Cursor, the MCP Inspector) connects over
loopback. `UElysiumMcpSubsystem` (a `UEngineSubsystem`) registers the Elysium tools through
`IModelContextProtocolModule::AddTool()` — the **direct** path, deliberately not the plugin's
Toolset-Registry→MCP adapter, because that adapter is editor-only and this project's whole loop is
`-game`/cooked with no editor content loop. Direct registration serves the tools in editor, PIE,
`-game`, and (with an explicit `StartServer()` call) a cooked build alike. The subsystem is
engine-scoped and resolves the live world at **call** time, so one connected agent keeps a stable
tool list across map travel, PIE start/stop, and an idle editor.

**The tools mirror the Cog windows** (~20): map lifecycle (list/load/reload/new-game), player
(pose/teleport/noclip), entities (paged+filtered list with a class histogram, full detail = the
chain-walked fields + accepted inputs + 7-field outputs with live `times`, and `entity_fire`
through the real queue), the event queue (read/pause/step), the I/O history ring, `script_eval` +
the `G` dump, audio state, a viewport `screenshot` (overlay excluded), and two escape hatches —
`console_exec` (runs any `elysium.*`/engine verb, merging the exec output device with a delta of an
always-on log-tap ring so verbs that report through `UE_LOG` are captured) and `log_tail`. Anything
without a typed tool is still reachable through `console_exec`, so the surface is complete by
construction. Each tool returns structured JSON (`MakeStructuredContentResult`); handlers run on the
game thread (the server serializes tool calls there), so they touch the substrate directly.

**Posture.** The server is **on by default in dev builds** — it auto-starts whenever the plugin is
present, which is editor-target-only (the dependency is gated by `ELYSIUM_WITH_MCP`, defined `1`
only for the Editor target, so nothing self-starts in a Game/Shipping/Test build — every call site
compiles to an empty shell there). Commandlets and unattended automation register the tools without starting the HTTP
listener. `-NoElysiumMcp` opts out in an interactive process; `-ElysiumMcp=<port>` pins a port;
`elysium.mcp.start`/`stop` toggle it live. It binds `127.0.0.1` with no authentication (the plugin's
own posture): a local dev tool, nothing else. An agent connects via the repo-root `.mcp.json`
(server name `elysium`, `http://127.0.0.1:8000/mcp`) — launch the game, then run `claude` from the
repo root. Two stock engine
toolsets are enabled beside ours — `AutomationTestToolset` (the agent runs/reads automation tests
in-editor) and `LiveCodingToolset` (agent-triggered hot recompile); the ~28 actor/Niagara/PCG/etc.
toolsets are skipped, having nothing to act on in a project that builds no editor content.

**What the agent asserts against — not itself (P2.8).** The LLM is never the test oracle. It
*drives* Unreal's own automation framework, which is the oracle. Two tiers live in
`Source/ElysiumUE/Private/Tests/` (inside the module, because the plain-C++ substrate carries no
`ELYSIUMUE_API` exports for a separate test module to link): a **content-free** suite under
`-nullrhi` (variant/expr/KeyValues/queue/registry + one end-to-end `logic_relay→math_counter` I/O
chain on a bare entity world) that is the real regression net, and a **content-gated** suite that
parses the real exported `.ents` and **self-skips** when `$ELYSIUM_EXPORT_ROOT` is empty. `uv run elysium test` runs them
headless with a JSON+HTML report; the design and flags are in `docs/project/roadmap.md` P2.8.

**Closing the loop visually (P2.9).** `FElysiumShotRun` (`-ElysiumShots`, `uv run elysium debug shots`) is the
screenshot-regression sibling of the profiler — it visits the *same* fixed vantages
(`ElysiumVantages.h`, shared so a look regression and a cost regression are the same frame),
captures the viewport to the gitignored `$ELYSIUM_EXPORT_ROOT/_shots/` (baselines are game-derived), and shares
its capture path (`ElysiumScreenshot.{h,cpp}`) with the `elysium_screenshot` MCP tool — the
fire→screenshot→assert loop the agent runs live and the harness runs headless are one code path.

### Rendered skeletal green room

`uv run elysium debug greenroom <case> [map]` is the real-RHI gate between structural animation tests and an aggregate
story launch. It loads the production skeletal mesh/material/clip paths, seeks fixed absolute poses,
renders off-screen at 1920×1080 DX12/SM6, and writes PNGs plus `manifest.json` under
`$ELYSIUM_EXPORT_ROOT/_greenroom/<case>/`. Individual `player`, `sire`, `vampire1`, `vampire2`, `sheriff`, and
named prop cases isolate one model and clip; `opening` renders the five-body cast; `props` walks both
wineglass clips and all seven stake clips one at a time.

`embrace` runs on `sp_theatre`: it builds the player through the same NPC understudy/material path
used by the scene, places all five bodies at the authored scene origin through the shared skeletal
basis, parses both real camera chains, and composes the production position/target value shot. Capture
times are derived from every exact-zero cut in either stream, every positive-movement midpoint, and
every authored fade boundary. The six black fade windows are applied to the rendered viewport.

Every capture records root, bounds-center, extent, clip time, eight key-bone positions, normalized
camera coordinates and in-frame counts, plus camera segments/position/target/roll/FOV/fade alpha.
Non-finite values, zero or exploded bounds, missing key bones/assets/clips, a failed screenshot,
disagreeing camera clocks, or any cast member never entering frame outside an opaque fade makes `ok`
false and exits the batch with status 1. This separates model/clip/material faults from placement,
framing, cuts, and fades before `newgame_ttd` is allowed to be the aggregate integration test.

### The interactive green room

`uv run elysium gr <stem>` is the same stage driven by hand rather than by a case list: one body on
an empty map, with a Cog window (`ElysiumCogWindow_GreenRoom`) over the lab
(`FElysiumGreenRoomRun`) that stands it, filters its clips, arms layers over it, and steers a blend
grid's declared axes. It is the **primary verification surface for the skeletal animation
programme** — the composition rules it exercises are pose arithmetic whose failures are visible
before they are measurable, and the tiers underneath it assert transport and structure rather than
plausibility.

Two rules keep it honest. It drives the **production** bake and proxy paths, never a copy, so what
it stands is what a map stands; `LabRestand` exists because the mesh and clip caches are keyed by
stem and pin one for the map epoch, so a re-export is invisible without it. And it
**observes** the runtime's own selection rather than substituting for it: where gameplay selects an
animation, the lab's job is to display what was selected and let it be perturbed, so a panel reports
the selecting authority beside the result. A control that can only be reached from the lab is
scaffolding, and the phase that gives it a real caller retires it.

**Two things the panel does not tell you.** A body standing green says nothing about which sequence
is playing — the readout reports the mesh, and the clip loader answers null silently when the mount
carries no such asset. And root motion is a per-clip property rather than a body one:
`ScanRootMotion` is the check for a clip outside locomotion carrying a moving root, which a motor
already consuming that cell's displacement metadata would then double-move.

`uv run elysium debug modelroom <mesh-stem> <clip> [map]` is the human-form review surface over the same production
loader. For a cinematic bank, append `<anim-set-model> <bone-root> [map]`. It isolates exactly one
body and clip, freezes it at 0/25/50/75/99 percent, captures front, three-quarter, profile, and back
views, then assembles and opens `$ELYSIUM_EXPORT_ROOT/_greenroom/review/review_sheet.png`. The sheet is the
visual acceptance surface: bounds and matrix checks may reject broken data, but they cannot certify
an anatomically plausible lean, seated posture, silhouette, cloth deformation, or facing direction.
Those observations are recorded from the owner before a pose hypothesis is enabled in the runtime.

### The movement and camera harness

`uv run elysium debug move [course] [hz]` is the player-feel equivalent of the shot run: it arms
`-ElysiumMove`, replays a fixed command stream over a table of named courses, records it, and diffs
it against a baseline. The engine is pinned to the same rate as the stream, so a run at 60, 120 and
240 Hz differs only in integration and the *intent* is identical — which is what makes frame-rate
dependence measurable rather than anecdotal.

A course is authored as **segments** — hold this intent for this long — rather than as frames, and
expanded at run time. The mover's state is reset between courses, because a course that inherits the
previous one's velocity measures the wrong thing, and the body then settles for a few frames before
its first command: `Duck` runs ahead of `CategorizePosition`, so a course whose first frame crouches
would get the *airborne* duck and start two units off the floor. Position and velocity are emitted in
**Source units**, so a row reads directly against `docs/vtmb/source_movement.md`.

**A course that has to press a button on an exact frame runs twice.** Most recipes saturate — they
hold an intent long enough that *when* the body arrives cannot matter — but the leniency brackets
place exactly one jump press, and it has to land a chosen number of frames from a **body event**
rather than from the clock, because the walk to a lip moves with the gait and the offset does not.
So the harness drives the course once as a **probe** with no press, watching only the ground state
to find the frame it flips on, then re-seats the body and replays the same course with the press at
that frame plus the lane's offset. Only the second pass touches the recorder, which keeps the
recorder's one-open-one-write invariant intact. `Expand` stays pure by taking the resolved frame as
an argument, so resolving the event is the engine's job and building the stream is still a function
of the course, the step and that one number — and passing `INDEX_NONE` is what the probe pass uses,
so both passes drive streams of identical length. The pass is sound because a refused press is a
provable no-op: `CheckJumpButton` returns on `!bOnGround` without touching velocity, gravity scale,
the hold window or the button latch, so nothing before the press can differ between the two runs.
A course with no event-timed press skips the probe entirely.

**Two hosts, and they answer different questions.** `--gym` builds the generated gym in an empty
stage world, where each riser, roof, ramp and aperture is derived from the movement constants and
brackets the threshold it tests — so a run locates a cliff rather than confirming that one particular
staircase still works. `--sited` replays on `sp_tutorial_1`, which is the only thing a retail capture
could ever be compared against, because retail will not load geometry we authored. Both run by
default. The gym's derivation and its speed-invariant/speed-dependent split are
`docs/architecture/movement-architecture.md`.

### The recorder and the differ

Every run writes through one **named-channel recorder**: a CSV of the frame channels it declared and
a `.channels.json` manifest beside it under `$ELYSIUM_EXPORT_ROOT/_move/`. The manifest carries each
channel's comparison rule, the constants the geometry was derived from, any per-course tuning
override, and the run channels. `ElysiumChannels::Defs()` is the registry, and a name that is not in
it cannot be written at all — registering a channel *is* registering its comparison, which is what
closes the old comparator's silent pass. A camera or animation producer joins by declaring rows there
and writing them; there is no second format and the differ does not change.

`pipeline/src/elysium_pipeline/validation/channel_diff.py` is the comparator, chained into
`debug move` so its verdict is the command's exit code. It reads both manifests rather than one, and
**refuses** rather than partly accepting: an undeclared CSV column, a declared channel with no
column, a numeric channel with no usable tolerance, a channel the baseline has and the run does not
(or the reverse), and a rule that changed between the two — because a run that reads its own
tolerance could widen its own bar. `--promote` makes the current runs the baseline; `--hz` does the
cross-rate comparison by elapsed time rather than by frame index.

**One baseline root, `$ELYSIUM_EXPORT_ROOT/_move/baseline/`, and it is gitignored.** Gym runs happen
in an empty stage world on geometry derived from this repository's own constants, but the body
standing on that geometry is a real baked one — `-MoveBody=<stem>` picks it — because a gym driving a
body with no animation measures the constants fallback instead of the speed authority. That makes the
recordings game-derived like the sited ones. What a gym baseline compares is still the **run**
channels — how far the body got, how high it stood or reached — which are written to saturate and are
therefore the same at any gait; a per-frame trace is not, and is deferred. Sited runs are compared in
full, frame rows included.

The command stream is deterministic and frame-pinned, which is what makes this the acceptance surface
for the whole player-feel vertical rather than for movement alone: **camera and animation channels
ride the same runs** — boom length, clip state, damper position and solved angles beside `move_yaw`
and the state machine's state — so a camera regression becomes the same diff as a movement one.

`elysium.playerpos` sites a course the way `elysium.campos` sites a vantage, and logs the **feet**
rather than the view — the pawn's own origin is its box centre, so a coordinate read off either the
camera or the actor is half a hull out.

### The cast harness, and the one trace both producers write

`uv run elysium debug cast` is the movement harness's other half: the same body trace, recorded from
the cast instead of from the player. It exists because a body trace with one producer proves nothing
about two — the player's locomotion and the cast's are claimed to be one system, and a claim that
only one of them is ever recorded is untested.

**One schema and one writer.** `ElysiumLocomotionTrace` owns the columns and the row: it takes the
`FElysiumLocomotionSample` a moving body published and the `FElysiumAnimationSelection` its driver
resolved from it, and nothing else — no mover, no controller, no entity, so a recording cannot
disagree with what ran. The movement harness declares those columns **plus** what only a driven
player body can measure (the replayed command's sequence number, the standing hull's headroom, the
ground's friction scale, the camera's solve); the cast harness declares them verbatim. That split is
asserted with no world by `Elysium.Substrate.LocomotionTrace`, which also holds the writer to filling
every column it declares — the recorder refuses a frame with a hole in it, so a clean frame is the
proof.

**The arena, not the gym.** A cast course runs in the stage world over the combat arena, for the
reason the gym runs there: the floor is derived from this repository's own values. It cannot be the
gym, because the gym deliberately builds no navigation and a cast body is an `ACharacter` following a
Recast path. The arena's `intersting_place` anchors are deliberately not created — one standing in
the room would let an ambient claim take a body off its course — and the stage world's frozen pawn is
moved to the arena's player mark rather than left standing in the middle of the room that was just
built around it.

**A course is an authored order, not a command stream**, because nothing about the cast takes input:
a patrol route of `info_node_patrol_point` records armed by `FollowPatrolPath`, or a
`scripted_sequence` marker armed by `BeginSequence`, delivered through the entity input table a
level script uses. Nothing reaches into the mind, the schedule or the motor, so what is recorded is
the whole chain from the order to the pose. The recording window is a fixed number of seconds rather
than "until it arrives", so the frame count is the same at any gait and a slower body records a body
that got less far rather than a course of a different length.

Its runs live under `$ELYSIUM_EXPORT_ROOT/_cast/` with their own baseline root, which is not
fastidiousness: the comparator fails a stem a baseline carries and a run does not, so sharing `_move`
would make a player-only run report every cast course as missing. `-CastBody=<stem>` picks the body;
with none named the run takes the first baked stem and records which one in the manifest, because a
recording made on a different body is a different recording.

## Tracking

Implementation sequence and status live only in `docs/project/roadmap.md` — P2, with the movement
and camera harness under its CCC slice and the cast harness under LIFE;
`docs/architecture/engine-core.md` owns the entity substrate design it observes.

## Prior art / sources

- Source SDK 2013 (`baseentity.cpp` ent_* commands, `eventqueue.h`, `env_debughistory.cpp`) —
  the Layer-2 blueprint; semantics verified in code.
- Recreation projects (OpenMW, Daggerfall Unity, Half-Life Unified SDK, Xash3D, OpenRA):
  the recurring debug features across all of them are the picker, live property dump,
  console event injection, paged entity listing, and volume visualization — all present
  above.
- UE 5.8: PIE outliner/eject workflow, Visual Logger, Gameplay Debugger (an optional
  per-actor category framework — redundant with Cog for now, revisit if a
  crosshair-centric HUD view earns its keep), Remote Control (optional), `DirectoryWatcher`
  (editor-only, hence polling/explicit reload).
- Cog: https://github.com/arnaud-jamin/Cog (MIT; "UE 5.5 or greater"; integration via
  `UWorldSubsystem`).
