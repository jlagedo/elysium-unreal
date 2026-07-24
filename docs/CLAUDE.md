# Documentation (`docs/`)

The reverse-engineering reference this project builds on: **engine-neutral VtMB facts**, valid
regardless of target engine. The per-doc index is in the repo-root `CLAUDE.md`; this file is
the maintenance contract.

## Where a given fact belongs

- **Status, plans, task breakdowns, dates, "what landed"** → the roadmap set. `roadmap.md`
  is the single source of truth work tracker (phases P0–P10, per-task status, the pipeline
  (PL*) and RE backlogs, the risk register); `roadmap-archive.md` holds the full as-built
  record of every completed task (moved there, verbatim, when a task lands — the roadmap
  keeps a short summary); `decisions.md` is the append-only **decision log**. Never mirror
  status into another doc — link to the task instead. Other docs' plan sections point here.
- **Strategy** (tracks, milestone vocabulary, sidecar contracts, per-system design targets) →
  `rebuild-strategy.md`.
- **What may be modernized vs must be reproduced** → `remaster-direction.md`. A behavioural
  divergence needs the faithful behaviour RE'd and recorded *plus* a dated entry in
  `decisions.md`.
- **A new VtMB format/behaviour finding** → the topic doc that owns it (`entity_io.md`,
  `python_bridge.md`, `audio_pipeline.md`, `mdl_v2531.md`, `source_movement.md`,
  `level_transitions.md`, `animation_and_movers.md`, `game_runtime.md`). Cite where it came
  from — decompiled address, `tools/ghidra/run.ps1 -Script DumpGrep` output, or the data file.
- **Design intent for an Unreal system** → `engine-core.md` (entity object model),
  `debug-tooling.md` (debug layers), `map-architecture.md` (map lifecycle),
  `rendering-perf.md` (render path + tuning), `asset-enhancement.md` (offline surface track).
- **What exists in code right now** → the directory `CLAUDE.md` next to that code
  (`Source/ElysiumUE/CLAUDE.md`, `tools/CLAUDE.md`, `Content/CLAUDE.md`), not a doc here.

## House rules

- **Facts, present tense.** These docs describe how VtMB works and what the design is — not
  how a doc changed, not what a decision replaced. Migration narrative and rationale-for-a-
  past-decision belong in `decisions.md`, if anywhere.
- **Mark confidence.** An unverified reconstruction says so in the doc (see
  `recovered/dice-system.md`, which still needs a golden test against the running game).
- **Correct in place.** When RE contradicts a doc, fix the doc in the same pass and note the
  correction in the roadmap task that found it.

## Godot-prototype reference docs

`lighting.md`, `entity_visuals.md`, `color_gamma.md`, `m0_menu_build.md`, and the "Mapping to
Godot" sections of `audio_pipeline.md` / `source_movement.md` / `animation_and_movers.md`
carry a banner: they describe the **read-only Godot prototype's** implementation, not
Elysium-Unreal. The VtMB facts inside remain valid; the C#/Godot detail is porting reference
only. Keep the banner when editing.

`m0_menu_build.md` carries a second caveat: the UI is **not** ported from VGUI. That doc is
the record of what the original UI contains and why (the `GameUI.dll` findings, scheme/`.res`
semantics, font roles) — the design intent the modern re-skin is checked against, not a port
target.

## Not in this repo

`tools/ghidra/README.md` documents the headless-Ghidra RE workspace. The whole
`tools/ghidra*/` and `tools/re/` trees are gitignored, local-only RE references — cite their
findings here, never commit their contents.
