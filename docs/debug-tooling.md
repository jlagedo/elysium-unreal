# Debug & development tooling architecture

**Status: adopted design; the foundation layer is a pre-M3 work item.** This doc defines how
Elysium-Unreal gets live inspection, visualization, and iteration tooling given its central
constraint: **every engine object is built at runtime from `tools/out` intermediates**. There
are no per-entity `.uasset`s, so the Unreal editor's asset-centric tooling (Content Browser,
level editing, Details-panel authoring) has nothing to author — and any value tweaked in the
editor would be overwritten by the next pipeline export anyway. Debug tooling therefore lives
**in-process, in the running game**, reading the same runtime data structures the game plays
from. The editor is a *viewer* we get for free during PIE, never an authoring surface.

The architecture is three layers. Everything is development-only (compiled out of or disabled
in Shipping); nothing here touches the bring-your-own-game posture.

---

## Layer 0 — engine built-ins, wired in (near-zero cost)

Things Unreal already does that only need hooking up:

- **Actor labels + Outliner folders on every spawn path.** `AActor::SetActorLabel` and
  `SetFolderPath` are editor-only symbols — wrap in `#if WITH_EDITOR`. With labels set
  (`Map:sp_tutorial_1`, `Props`, `LightRig`, later `ent:elevator_door_1 (func_door_rotating)`),
  the PIE World Outliner becomes a free scene browser: runtime-spawned actors appear live,
  their `UPROPERTY` state is inspectable in Details, **Eject (F8)** detaches into a free
  editor camera while the game runs, and pause + frame-step work. Caveats: edits there are
  ephemeral (fine — data comes from the pipeline), and none of this exists in `play.bat`
  standalone (`-game` has no Outliner) — which is why Layer 1 exists.
- **The `elysium.*` console surface stays the single tuning mechanism.** CVars
  (`TAutoConsoleVariable`) for every tunable, `FAutoConsoleCommandWithWorldAndArgs` for verbs.
  CVars are settable from console, ini, command line, and the Layer-1 UI alike. A
  `UCheatManager` subclass is the home for player-centric cheats (`UFUNCTION(Exec)`
  auto-registers with autocomplete).
- **`elysium.reload` — the data hot-loop.** Re-runs `Travel` on the current map. Combined
  with the pipeline (`UE_bsp_to_scene.py` → `tools/out`), this is the whole recook-free
  iteration cycle: edit exporter → re-export → `elysium.reload` in the running game. No
  directory watcher — the `DirectoryWatcher` module is editor-only; if auto-reload is ever
  wanted, poll `IFileManager::GetTimeStamp` on the map's sidecars. Explicit reload is enough.
- **`DrawDebugHelpers` for in-world visualizers**, each gated by an `elysium.*` toggle
  (the existing `elysium.lights`/`elysium.props` pattern). Compiled out in Shipping via
  `ENABLE_DRAW_DEBUG`.
- **Visual Logger (`UE_VLOG*`) as the "rewind" answer.** Recording works in standalone
  (`.vlog` files), scrubbing happens later in the editor's Visual Logger tab: per-actor,
  per-category snapshots and shapes on a timeline. Once the entity layer fires events, every
  I/O dispatch gets a VLOG line — post-hoc scrubbable causality for a whole `play.bat`
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
  actors, components, MIDs — in the running game, PIE *and* `play.bat` standalone alike.
- ~40 stock windows we'd otherwise hand-write: inspector, console, output log with
  per-category filtering, collision viewer, plots/metrics, tweaks, spawn, time scale,
  scalability. The GAS/AI windows are ignorable dead weight.
- Dev-only posture is built in: works in packaged builds, disabled by default in Shipping.
- It ships its own ImGui integration (`CogImGui`) — no second plugin to choose; NetImgui
  remote debugging comes along for free if ever needed.
- Maintained (active through 2026), README pins "UE 5.5 or greater", no reported 5.7/5.8
  breakage; budget for minor compile fixes on 5.8 (known trivial patches: the ImPlot
  `INFINITY` MSVC error, issue #71). Integration is a `UWorldSubsystem` + `.Build.cs` deps.

Custom Cog windows grow with the runtime, reading Elysium's own data structures directly
(ImGui code is plain immediate-mode C++ — no reflection or UI assets needed, which matters
because tier-1 logic entities are plain C++ objects, not UObjects):

- **Maps** — map list, travel, `elysium.reload`, load-phase timings (subsumes most of the
  Canvas HUD; the HUD keeps only the always-available FPS/position overlay).
- **Lights** — the rig's source list, per-style intensity curves, live scale tuning.
- **Entities** (M3) — browser over `FElysiumEntityWorld`'s registry: all 1,226 records
  including inert unhandled classnames, filter by classname/targetname, histogram.
- **Entity inspector** (M3) — one entity's keyvalues, outputs (all 7 fields), tier, spawn
  state, visibility; buttons to fire any input by hand. This is the B2 "primary test
  harness" window.
- **Event queue** (M3) — pending events with fire times, the I/O history ring buffer,
  pause/step controls (Layer 2).

The Slate `UElysiumConsoleSubsystem` designed in `map-architecture.md` is **superseded**:
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
| `elysium.ent_dump <name>` / `ent_info <classname>` | Live keyvalue/state dump of an entity; schema dump (supported inputs/outputs) of a class. Driven by the entity records + input tables — no per-class code. |
| `elysium.showtriggers` | Render trigger volumes as wireframes, colored by classname (or enabled/disabled state); I/O beam lines (`DrawDebugDirectionalArrow` with duration) from caller to target on fire — exceeding Source, which only drew textual arrows. |
| `elysium.developer 2` equivalent | Every dispatch logs `(sim-time) input caller: target.Input(param) [python]` to a dedicated log category (the field-6 Python string is VtMB-specific and must be in the line). |
| I/O history ring buffer | Every dispatch is *always* recorded into a bounded ring (Source's `env_debughistory`: 1000 lines), dumpable on demand and cheap enough to serialize into saves later — postmortem forensics without having had logging on. |
| `elysium.ent_break <target> [input]` | Auto-`ent_pause` when a matching input is delivered — breakpoint-on-entity-event; trivial given the chokepoint, and better than any surveyed prior art. |

Each dispatch also emits `UE_VLOG` events (Layer 0) for offline scrubbing.

## Build order

Work tracking lives in **`docs/roadmap.md`** (the single source of truth): the entity
substrate is roadmap **P1**, this doc's Layers 1–2 are roadmap **P2** (Cog shell, entity/queue
windows, `ent_*` verbs, trigger/gizmo visualization, Maps/Lights windows, `elysium.reload`,
labels/folders, the `UCheatManager` subclass). The design detail behind P1 is
`docs/engine-core.md`.

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
