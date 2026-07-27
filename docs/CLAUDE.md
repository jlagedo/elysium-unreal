# Documentation (`docs/`)

The reverse-engineering reference this project builds on: **engine-neutral VtMB facts**, valid
regardless of target engine. The per-doc index is in the repo-root `CLAUDE.md`; this file is
the maintenance contract.

## Where a given fact belongs

- **Status, plans, task breakdowns, dates, "what landed"** → the roadmap set. `roadmap.md`
  is the single source of truth work tracker (the playable-path ladder PP0–PP6, phases P0–P13,
  per-task status, the pipeline (PL*) and RE backlogs, the risk register); `roadmap-archive.md` holds the full as-built
  record of every completed task (moved there, verbatim, when a task lands — the roadmap
  keeps a short summary); `decisions.md` is the append-only **decision log**. Never mirror
  status into another doc — link to the task instead. Other docs' plan sections point here.
- **Strategy** (tracks, milestone vocabulary, sidecar contracts, per-system design targets) →
  `rebuild-strategy.md`.
- **What may be modernized vs must be reproduced** → `remaster-direction.md`. A behavioural
  divergence needs the faithful behaviour RE'd and recorded *plus* a dated entry in
  `decisions.md`.
- **A new VtMB format/behaviour finding** → the topic doc that owns it (`entity_io.md`,
  `python_bridge.md`, `audio_pipeline.md`, `mdl_v2531.md`, `facial_animation.md`,
  `source_movement.md`, `controls.md`,
  `level_transitions.md`, `animation_and_movers.md`, `game_runtime.md`). Cite where it came
  from — decompiled address, `tools/ghidra/run.ps1 -Script DumpGrep` output, or the data file.
  `python_bridge.md` ↔ `script_api.md` split the scripting layer the same way the other pairs
  split theirs: the first owns the binding **mechanism**, the second the per-name **inventory**
  (signature, owning datamap, handler address, call demand). A new engine-callable name goes in
  the second; a new fact about how binding works goes in the first. `vtmb-ui.md` ↔
  `ui-architecture.md` split the UI the same way — the first owns what VtMB's own screens are
  (which code owns them, the two schemes, the 1024×768 canvas law, the HUD class inventory), the
  second owns the Unreal re-skin's stack and design tokens.
- **Design intent for an Unreal system** → `runtime-architecture.md` (the spine: lifetimes, the
  frame, the player object, the session, the seams), `engine-core.md` (entity object model),
  `debug-tooling.md` (debug layers), `map-architecture.md` (map lifecycle),
  `rendering-perf.md` (render path + tuning), `asset-enhancement.md` (offline surface track),
  `input-architecture.md` (input path), `ui-architecture.md` (the CommonUI/Slate UI stack),
  `save-architecture.md` (persistence). Each has a VtMB-facts counterpart it must not duplicate —
  `input-architecture.md` ↔ `controls.md`, `ui-architecture.md` ↔ `vtmb-ui.md`,
  `save-architecture.md` ↔ `savegame_format.md`, `runtime-architecture.md` ↔ `game_runtime.md`.
  `runtime-architecture.md` is the one that *integrates* the others: it may state which system owns
  a seam, never how that system works internally.
- **What exists in code right now** → the directory `CLAUDE.md` next to that code
  (`Source/ElysiumUE/CLAUDE.md`, `tools/CLAUDE.md`, `Content/CLAUDE.md`), not a doc here.

## House rules

- **Facts, present tense.** These docs describe how VtMB works and what the design is — not
  how a doc changed, not what a decision replaced. Migration narrative and rationale-for-a-
  past-decision belong in `decisions.md`, if anywhere.
- **Mark confidence.** An unverified reconstruction says so in the doc, and states what would
  verify it (the `recovered/` folder holds these; each carries a status line — e.g.
  `recovered/dice-system.md` records that it was verified by decompilation + the shipped
  `DiceRolls.txt`, so no running-game golden test was needed).
- **Correct in place.** When RE contradicts a doc, fix the doc in the same pass and note the
  correction in the roadmap task that found it.

## `m0_menu_build.md`

Records what the original UI contains and why (the `GameUI.dll` decompile findings, scheme/
`.res` semantics, font roles) — the design intent the modern re-skin is checked against, not
a port target (Elysium-Unreal does not port VGUI; see `remaster-direction.md` axis 1). It is
**partly superseded**: it decompiles the menu VtMB links but does not present, so four of its
claims are corrected in `vtmb-ui.md` §6. Keep that banner when editing.

## Not in this repo

`tools/ghidra/README.md` documents the headless-Ghidra RE workspace. The whole
`tools/ghidra*/` and `tools/re/` trees are gitignored, local-only RE references — cite their
findings here, never commit their contents.
